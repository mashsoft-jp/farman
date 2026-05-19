#include "FileListPane.h"
#include "FileListDelegate.h"
#include "FileListView.h"
#include "FileListThumbnailView.h"
#include "ClickableLabel.h"
#include "core/FileItem.h"
#include "BookmarkEditDialog.h"
#include "model/FileListModel.h"
#include "settings/Settings.h"
#include "core/BookmarkManager.h"
#include "utils/Dialogs.h"
#include "types.h"
#include <QApplication>
#include <QCompleter>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QStackedWidget>
#include <QStyle>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace Farman {

// アドレスバー入力欄のパス補完で使う QCompleter サブクラス。
// QFileSystemModel と組み合わせる。先頭の `~` / `~/` は QDir::homePath() に
// 展開してから親クラスの splitPath に渡し、ホームディレクトリ以下の候補が
// 出るようにする。
class PathCompleter : public QCompleter {
public:
  using QCompleter::QCompleter;

  QStringList splitPath(const QString& path) const override {
    QString p = path;
    if (p == QStringLiteral("~")) {
      p = QDir::homePath();
    } else if (p.startsWith(QStringLiteral("~/"))) {
      p.replace(0, 1, QDir::homePath());
    }
    return QCompleter::splitPath(p);
  }
};

FileListPane::FileListPane(QWidget* parent)
  : QWidget(parent)
  , m_addressEdit(nullptr)
  , m_bookmarkLabel(nullptr)
  , m_folderButton(nullptr)
  , m_view(nullptr)
  , m_sortFilterStatusLabel(nullptr)
  , m_model(nullptr)
  , m_delegate(nullptr) {

  setupUi();

  connect(&BookmarkManager::instance(), &BookmarkManager::bookmarksChanged,
          this, &FileListPane::refreshBookmarkIndicator);

  // 外部 (Finder 等) からのカレントディレクトリ変更を検知して自動更新する。
  // QFileSystemWatcher の directoryChanged は短時間に複数回飛ぶことがあるので
  // 単発タイマーで debounce してから model を refresh する。
  m_dirWatcher = new QFileSystemWatcher(this);
  m_refreshDebounce = new QTimer(this);
  m_refreshDebounce->setSingleShot(true);
  m_refreshDebounce->setInterval(150);
  connect(m_dirWatcher, &QFileSystemWatcher::directoryChanged, this,
          [this](const QString&) { m_refreshDebounce->start(); });
  connect(m_refreshDebounce, &QTimer::timeout, this,
          &FileListPane::onExternalDirectoryChanged);
}

FileListPane::~FileListPane() = default;

void FileListPane::setupUi() {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(2);

  // パスラベルとフォルダボタンを横並びに配置
  QWidget* pathWidget = new QWidget(this);
  QHBoxLayout* pathLayout = new QHBoxLayout(pathWidget);
  pathLayout->setContentsMargins(0, 0, 0, 0);
  pathLayout->setSpacing(0);

  // ブックマーク登録/解除トグル用の★ラベル。常に表示。
  // QLabel 派生にすることで隣の pathLabel と同じ高さ・paddingに揃う。
  // 登録状態はスタイルシートで色分けする（refreshBookmarkIndicator）。
  m_bookmarkLabel = new ClickableLabel(this);
  m_bookmarkLabel->setText(QStringLiteral("★"));
  m_bookmarkLabel->setCursor(Qt::PointingHandCursor);
  // Tab で到達できるようにし、Enter / Return でブックマーク登録/解除を
  // トグル (eventFilter で実装)。
  m_bookmarkLabel->setFocusPolicy(Qt::StrongFocus);
  m_bookmarkLabel->installEventFilter(this);
  connect(m_bookmarkLabel, &ClickableLabel::clicked,
          this, &FileListPane::onBookmarkButtonClicked);
  pathLayout->addWidget(m_bookmarkLabel, 0);

  // アドレスバー: QLineEdit を読取専用 + フレーム無しで配置し、
  // 表示中はラベルのように見せる。クリックすると編集モードに入り
  // フレームを出して直接パスを入力できるようにする (Finder / Explorer
  // からのコピペ用)。
  m_addressEdit = new QLineEdit(this);
  m_addressEdit->setReadOnly(true);
  m_addressEdit->setFrame(false);
  m_addressEdit->setCursor(Qt::IBeamCursor);
  m_addressEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_addressEdit->setMinimumWidth(0);
  m_addressEdit->setToolTip(
    tr("Click to edit the path. Press Enter to navigate, Esc to cancel."));
  m_addressEdit->installEventFilter(this);
  connect(m_addressEdit, &QLineEdit::returnPressed,
          this, &FileListPane::commitAddressEdit);
  pathLayout->addWidget(m_addressEdit, 1);

  // パス補完: 編集モードで途中まで打つと、続くディレクトリ候補が
  // ドロップダウン (QCompleter::PopupCompletion) に出る。
  // - QDir::Dirs だけ通すのでファイル名は候補に出ない (ナビゲーション専用)
  // - macOS / Windows は大小文字を区別しない FS なので CaseInsensitive
  // - 隠しディレクトリも候補に含める ("~/.config" 等を打鍵省略するため)
  // - Drives も含めて Windows の C:/ 等にも対応
  // - 先頭 ~ は PathCompleter::splitPath で $HOME に展開
  // 読取専用 (= 通常の表示モード) では QLineEdit が補完を発火しないので
  // 編集モードのときだけ自動的にポップアップが出る。
  auto* fsModel = new QFileSystemModel(this);
  fsModel->setRootPath(QString());
  fsModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::Drives);
  auto* completer = new PathCompleter(this);
  completer->setModel(fsModel);
  completer->setCaseSensitivity(Qt::CaseInsensitive);
  completer->setCompletionMode(QCompleter::PopupCompletion);
  completer->setMaxVisibleItems(15);
  m_addressEdit->setCompleter(completer);

  m_folderButton = new QToolButton(this);
  m_folderButton->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
  m_folderButton->setToolTip(tr("Browse folder..."));
  // Tab で到達できる + フォーカス時に視覚的に強調する。Enter は eventFilter
  // 側でクリックに変換 (macOS では QToolButton の標準 Enter ハンドリングが
  // 弱いことがあるため明示)。
  m_folderButton->setFocusPolicy(Qt::StrongFocus);
  m_folderButton->installEventFilter(this);
  connect(m_folderButton, &QToolButton::clicked, this, &FileListPane::onFolderButtonClicked);
  pathLayout->addWidget(m_folderButton);

  // ★ → アドレスバー → フォルダボタン → ファイルリスト の Tab 連鎖は
  // eventFilter で明示的に組み立てる (macOS の Tab ナビゲーション設定に
  // 依存せず動かすため)。setTabOrder は補助。

  mainLayout->addWidget(pathWidget);

  // テーブルビュー (D&D 対応の FileListView を使う)
  m_view = new FileListView(this);
  // Drag-out 用の URL プロバイダ。farman の選択状態 (FileItem::isSelected) を
  // 優先し、選択が無ければカーソル行 1 件を返す。'..' は除外。
  // List / Thumbnail どちらのビューからも同じロジックで呼べるようにラムダ化。
  auto urlsProvider = [this]() -> QList<QUrl> {
    QList<QUrl> urls;
    if (!m_model) return urls;
    bool anySelected = false;
    const int rows = m_model->rowCount();
    for (int i = 0; i < rows; ++i) {
      const FileItem* it = m_model->itemAt(i);
      if (it && it->isSelected()) { anySelected = true; break; }
    }
    if (anySelected) {
      for (int i = 0; i < rows; ++i) {
        const FileItem* it = m_model->itemAt(i);
        if (it && it->isSelected() && !it->isDotDot()) {
          urls.append(QUrl::fromLocalFile(it->absolutePath()));
        }
      }
    } else {
      QAbstractItemView* av = activeView();
      const QModelIndex cur = av ? av->currentIndex() : QModelIndex();
      if (cur.isValid()) {
        const FileItem* it = m_model->itemAt(cur.row());
        if (it && !it->isDotDot()) {
          urls.append(QUrl::fromLocalFile(it->absolutePath()));
        }
      }
    }
    return urls;
  };
  m_view->setUrlsProvider(urlsProvider);
  // Drop 受信時は FileManagerPanel まで bubble させる
  connect(m_view, &FileListView::externalUrlsDropped, this,
          &FileListPane::externalUrlsDropped);
  m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_view->setSelectionMode(QAbstractItemView::NoSelection);
  m_view->setAlternatingRowColors(false);
  m_view->setFrameShape(QFrame::NoFrame);
  m_view->setShowGrid(false);
  m_view->horizontalHeader()->setStretchLastSection(true);
  m_view->horizontalHeader()->setSectionsClickable(true);
  m_view->horizontalHeader()->setSortIndicatorShown(true);
  m_view->verticalHeader()->setVisible(false);

  m_model = new FileListModel(this);
  m_view->setModel(m_model);

  m_delegate = new FileListDelegate(this);
  m_view->setItemDelegate(m_delegate);

  // サムネイル表示モード用ビュー。同じ FileListModel と同じ selectionModel を
  // 共有するので、モード切替で選択状態 / カーソル行は維持される (Phase 1 以降で
  // model 側の DecorationRole にサムネイル pixmap を載せる)。Phase 0 では空の
  // QListView::IconMode として並べておくだけ。
  m_thumbnailView = new FileListThumbnailView(this);
  m_thumbnailView->setModel(m_model);
  m_thumbnailView->setSelectionModel(m_view->selectionModel());
  m_thumbnailView->setSelectionMode(QAbstractItemView::NoSelection);
  m_thumbnailView->setIconSize(QSize(static_cast<int>(ThumbnailSize::Medium),
                                    static_cast<int>(ThumbnailSize::Medium)));
  m_thumbnailView->setUrlsProvider(urlsProvider);
  connect(m_thumbnailView, &FileListThumbnailView::externalUrlsDropped, this,
          &FileListPane::externalUrlsDropped);

  connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged,
          this, &FileListPane::onCurrentChanged);

  connect(m_view->horizontalHeader(), &QHeaderView::sectionClicked,
          this, &FileListPane::onHeaderClicked);

  m_view->setColumnWidth(FileListModel::Name,         250);
  m_view->setColumnWidth(FileListModel::Type,          80);
  m_view->setColumnWidth(FileListModel::Size,         100);
  m_view->setColumnWidth(FileListModel::LastModified, 150);
  m_view->setColumnWidth(FileListModel::Created,      150);
  m_view->setColumnWidth(FileListModel::Permissions,  100);
  m_view->setColumnWidth(FileListModel::Attributes,    60);
  m_view->setColumnWidth(FileListModel::Owner,        100);
  m_view->setColumnWidth(FileListModel::Group,        100);
  m_view->setColumnWidth(FileListModel::LinkTarget,   200);

  // 列表示の初期適用 (デュアルペインモード)。
  // 後で setSinglePaneMode から呼ばれて切り替わる。
  applyColumnVisibility(/*singlePane=*/false);

  // 構築時の Qt 既定の行高を覚えておく。Settings で行高 = 0 (Auto) を
  // 選んだときにこの値に戻す。
  m_defaultRowHeight = m_view->verticalHeader()->defaultSectionSize();

  // Tab 順 (補助): アドレスバー → フォルダボタン → ★ → ファイルリスト本体
  // (本体はアクティブペインに切替えるロジックも絡むので、最終的な遷移は
  // eventFilter / FileManagerPanel::handleKeyEvent 側で制御する)。
  setTabOrder(m_addressEdit, m_folderButton);
  setTabOrder(m_folderButton, m_bookmarkLabel);
  setTabOrder(m_bookmarkLabel, m_view);

  // StackedWidget で List ビュー / Thumbnail ビューを切替える。
  // 初期は List (index 0)。setViewMode() で currentIndex を切替えるだけ。
  m_viewStack = new QStackedWidget(this);
  m_viewStack->addWidget(m_view);            // index 0: List
  m_viewStack->addWidget(m_thumbnailView);   // index 1: Thumbnail
  m_viewStack->setCurrentIndex(0);
  mainLayout->addWidget(m_viewStack);

  // 即時フィルタ (Quick Filter Bar): デフォルト非表示。`Ctrl+I`
  // (= view.quick_filter コマンド) で toggleQuickFilter() が呼ばれて表示される。
  // 入力中の textEdited で model->setLiveFilter を呼んでリアルタイム絞り込み、
  // Esc / Enter は eventFilter で捕まえて閉じる / フォーカス戻しに振る。
  m_quickFilterEdit = new QLineEdit(this);
  m_quickFilterEdit->setPlaceholderText(tr("Quick filter (substring, case-insensitive)"));
  m_quickFilterEdit->setClearButtonEnabled(true);
  m_quickFilterEdit->hide();
  m_quickFilterEdit->installEventFilter(this);
  connect(m_quickFilterEdit, &QLineEdit::textEdited,
          this, &FileListPane::onQuickFilterTextEdited);
  mainLayout->addWidget(m_quickFilterEdit);

  // ソート・フィルタ条件の現在値をリスト下部に表示
  m_sortFilterStatusLabel = new QLabel(this);
  m_sortFilterStatusLabel->setStyleSheet(
    "QLabel { background-color: #e8e8e8; color: #333; padding: 2px 5px; }"
  );
  m_sortFilterStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont statusFont = m_sortFilterStatusLabel->font();
  statusFont.setPointSize(qMax(statusFont.pointSize() - 1, 9));
  m_sortFilterStatusLabel->setFont(statusFont);
  mainLayout->addWidget(m_sortFilterStatusLabel);

  refreshSortFilterStatus();
  refreshAppearance();
}

void FileListPane::refreshAppearance() {
  // アーカイブ内ブラウジング中は別カラーを採用 (「いま中にいる」ことの視覚提示)。
  const bool   inArchive = m_model && m_model->isInArchiveMode();
  const QColor fg = inArchive ? Settings::instance().archiveAddressForeground()
                              : Settings::instance().addressForeground();
  const QColor bg = inArchive ? Settings::instance().archiveAddressBackground()
                              : Settings::instance().addressBackground();
  // QLineEdit はフレーム無し + 同じ padding で QLabel 風の見た目にする。
  // 編集モードに入ると QLineEdit の通常 frame が出るので視覚的にも分かる。
  const QString addressStyle = QString(
    "QLineEdit { color: %1; background-color: %2; padding: 2px 5px; }"
    "QLabel    { color: %1; background-color: %2; padding: 2px 5px; }")
                                 .arg(fg.name(), bg.name());
  // フォーカス時 (Tab で到達したとき) にハイライト枠を出して視認性を上げる。
  const QString buttonStyle = QString(
    "QToolButton { background-color: %1; border: 1px solid transparent; padding: 2px; }"
    "QToolButton:focus { border: 2px solid palette(highlight); }")
                                .arg(bg.name());
  if (m_addressEdit) {
    m_addressEdit->setStyleSheet(addressStyle);
    m_addressEdit->setFont(Settings::instance().addressFont());
  }
  if (m_folderButton) m_folderButton->setStyleSheet(buttonStyle);
  if (m_view) {
    m_view->setFont(Settings::instance().font());
    // Settings の行高を反映 (0 = Auto → 構築時のデフォルトに戻す)
    const int customH = Settings::instance().fileListRowHeight();
    const int targetH = (customH > 0) ? customH : m_defaultRowHeight;
    if (targetH > 0) {
      m_view->verticalHeader()->setDefaultSectionSize(targetH);
    }
    m_view->viewport()->update();  // cursor 再描画
  }

  // ブックマークボタンは登録状態で色が変わるため、実スタイル適用は
  // refreshBookmarkIndicator() 側で行う。背景色だけは揃えておきたいので
  // ここで呼び出しておく。
  refreshBookmarkIndicator();
}

void FileListPane::applyColumnVisibility(bool singlePane) {
  if (!m_view) return;
  const auto v = singlePane
    ? Settings::instance().listColumnVisibilitySingle()
    : Settings::instance().listColumnVisibilityDual();

  // Name は常に表示。
  m_view->setColumnHidden(FileListModel::Name,         false);
  m_view->setColumnHidden(FileListModel::Type,         !v.type);
  m_view->setColumnHidden(FileListModel::Size,         !v.size);
  m_view->setColumnHidden(FileListModel::LastModified, !v.lastModified);
  m_view->setColumnHidden(FileListModel::Created,      !v.created);
#ifdef Q_OS_WIN
  // Windows: Permissions / Owner / Group は概念が違うので強制的に非表示。
  // Attributes だけ意味があるのでユーザー設定に従う。
  m_view->setColumnHidden(FileListModel::Permissions,  true);
  m_view->setColumnHidden(FileListModel::Attributes,   !v.attributes);
  m_view->setColumnHidden(FileListModel::Owner,        true);
  m_view->setColumnHidden(FileListModel::Group,        true);
#else
  // macOS / Linux: Attributes は Permissions と冗長になるので強制非表示。
  m_view->setColumnHidden(FileListModel::Permissions,  !v.permissions);
  m_view->setColumnHidden(FileListModel::Attributes,   true);
  m_view->setColumnHidden(FileListModel::Owner,        !v.owner);
  m_view->setColumnHidden(FileListModel::Group,        !v.group);
#endif
  m_view->setColumnHidden(FileListModel::LinkTarget,   !v.linkTarget);
}

QString FileListPane::currentPath() const {
  return m_model->currentPath();
}

// ── アドレスバー編集 ───────────────────────────────
//
// QLineEdit を「読取専用 + フレーム無し」で常設し、クリックや Tab 経由で
// フォーカスを得たタイミングで編集モードに切り替える。
// - 編集モード中: フレーム可視 + 編集可、テキストを全選択
// - Enter: commitAddressEdit でパスを解決して移動
// - Esc / focus-out: cancelAddressEdit で元のパスに戻して読取専用へ

void FileListPane::enterAddressEdit() {
  if (!m_addressEdit || m_addressEditing) return;
  m_addressEditing = true;
  m_addressEdit->setReadOnly(false);
  m_addressEdit->setFrame(true);
  m_addressEdit->setCursor(Qt::IBeamCursor);
  m_addressEdit->setFocus(Qt::OtherFocusReason);
  m_addressEdit->selectAll();
}

void FileListPane::leaveAddressEdit(bool restoreText) {
  if (!m_addressEdit) return;
  m_addressEditing = false;
  m_addressEdit->setReadOnly(true);
  m_addressEdit->setFrame(false);
  if (restoreText && m_model) {
    m_addressEdit->setText(m_model->currentPath());
  }
  m_addressEdit->deselect();
}

void FileListPane::cancelAddressEdit() {
  leaveAddressEdit(/*restoreText=*/true);
  if (m_view) m_view->setFocus();
}

void FileListPane::focusAddressBar() {
  // 公開 API。enterAddressEdit が編集モード化とフォーカス移動・全選択を
  // まとめて行う。
  enterAddressEdit();
}

void FileListPane::focusBookmarkLabel() {
  // 公開 API (Tab 連鎖の起点)。
  if (m_bookmarkLabel) {
    m_bookmarkLabel->setFocus(Qt::TabFocusReason);
  }
}

void FileListPane::toggleQuickFilter() {
  if (!m_quickFilterEdit) return;
  if (m_quickFilterEdit->isVisible()) {
    // 表示中 → 非表示にするだけ。フィルタとバーの入力内容は保持する
    // (= Enter と同じ動作)。完全に解除したいときは Esc を使う。
    m_quickFilterEdit->hide();
    if (m_view) m_view->setFocus(Qt::OtherFocusReason);
    refreshSortFilterStatus();
    return;
  }

  // 非表示 → 開く。直前のフィルタが残っていれば、その文字列を復元して
  // 全選択する (そのまま打ち直し / 部分編集 / Enter で確定 / Esc で消去 /
  // すべて選択した状態から好きに進められる)。
  const QString existing = m_model ? m_model->liveFilter() : QString();
  m_quickFilterEdit->setText(existing);
  m_quickFilterEdit->show();
  m_quickFilterEdit->setFocus(Qt::OtherFocusReason);
  if (!existing.isEmpty()) {
    m_quickFilterEdit->selectAll();
  }
  refreshSortFilterStatus();
}

void FileListPane::hideQuickFilter() {
  if (!m_quickFilterEdit) return;
  m_quickFilterEdit->hide();
  if (m_view) m_view->setFocus(Qt::OtherFocusReason);
  refreshSortFilterStatus();
}

void FileListPane::onQuickFilterTextEdited(const QString& text) {
  if (!m_model) return;
  m_model->setLiveFilter(text);
  refreshSortFilterStatus();
}

void FileListPane::commitAddressEdit() {
  if (!m_addressEdit) return;
  QString text = m_addressEdit->text().trimmed();

  // ~ / ~/ を $HOME に展開 (タイル単独もサポート)。
  if (text == QStringLiteral("~")) {
    text = QDir::homePath();
  } else if (text.startsWith(QStringLiteral("~/"))) {
    text.replace(0, 1, QDir::homePath());
  }

  // file:// URI ペーストへの簡易対応 (Finder からのコピーで来るケースあり)。
  if (text.startsWith(QStringLiteral("file://"))) {
    const QUrl url(text);
    if (url.isLocalFile()) text = url.toLocalFile();
  }

  if (text.isEmpty()) {
    cancelAddressEdit();
    return;
  }

  QFileInfo info(text);
  // ファイルが指定されたら親ディレクトリを採用 (Finder で .app をコピーした
  // ようなケース)。それ以外で存在しないパスはエラー扱い。
  if (info.exists() && info.isFile()) {
    text = info.absolutePath();
    info.setFile(text);
  }
  if (!info.exists() || !info.isDir()) {
    QApplication::beep();
    if (m_addressEdit) m_addressEdit->selectAll();
    return;
  }

  // 現在パスと一致しても setPath で no-op 扱いされるので問題無い。
  setPath(info.absoluteFilePath());
  leaveAddressEdit(/*restoreText=*/false);  // setPath で更新済み
  if (m_view) m_view->setFocus();
}

bool FileListPane::eventFilter(QObject* watched, QEvent* event) {
  // ★ ブックマーク / アドレスバー / フォルダボタン の Tab 連鎖と
  // 各ウィジェットの Enter / Return ハンドリングを明示的に処理する。
  // 連鎖の順序: ★ → addressEdit → folderButton → view → ★ (循環)。
  // macOS の Tab ナビゲーション設定に依存せず動かすため Qt 既定では
  // なく eventFilter で実装する。
  const bool isKeyPress = (event->type() == QEvent::KeyPress);
  const bool isShortcutOverride = (event->type() == QEvent::ShortcutOverride);
  if (isKeyPress || isShortcutOverride) {
    auto* ke = static_cast<QKeyEvent*>(event);
    const int key = ke->key();
    const auto mods = ke->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier
                                          | Qt::AltModifier  | Qt::MetaModifier);

    // Tab / Backtab の連鎖
    if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
      const bool back = (key == Qt::Key_Backtab) || (mods & Qt::ShiftModifier);
      QWidget* next = nullptr;
      if (watched == m_bookmarkLabel) {
        next = back ? static_cast<QWidget*>(m_view) : static_cast<QWidget*>(m_addressEdit);
      } else if (watched == m_addressEdit) {
        next = back ? static_cast<QWidget*>(m_bookmarkLabel) : static_cast<QWidget*>(m_folderButton);
      } else if (watched == m_folderButton) {
        next = back ? static_cast<QWidget*>(m_addressEdit) : static_cast<QWidget*>(m_view);
      }
      if (next) {
        if (isKeyPress) {
          if (next == m_addressEdit) {
            // アドレスバーは編集モードに入って全選択
            enterAddressEdit();
          } else {
            next->setFocus(Qt::TabFocusReason);
          }
        }
        event->accept();
        return true;
      }
    }

    // Enter / Return
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      if (watched == m_bookmarkLabel) {
        if (isKeyPress) toggleBookmarkForCurrentPath();
        event->accept();
        return true;
      }
      if (watched == m_folderButton) {
        if (isKeyPress && m_folderButton) m_folderButton->click();
        event->accept();
        return true;
      }
    }

    // ── Quick Filter Bar ─────────────────────────────
    // フィルタバー入力欄の Esc / Enter を eventFilter で吸収する。
    // - Esc: 入力をクリアしてバーを閉じ、フォーカスを view に戻す。
    //   (フィルタも空に戻る = 全件表示)
    // - Enter / Return: 現在のフィルタを保ったままバーを閉じ、フォーカスを
    //   view に戻す (絞り込み結果上で矢印キー操作したい用)。
    // - `/` の ShortcutOverride を accept しておくと、view.quick_filter の
    //   グローバルショートカットが横取りせずに QLineEdit に普通に "/" 文字が
    //   入力される。バーを閉じたいときは Esc を使う流儀に揃える。
    if (watched == m_quickFilterEdit) {
      if (isShortcutOverride && key == Qt::Key_Slash) {
        event->accept();
        return true;
      }
      if (key == Qt::Key_Escape) {
        if (isKeyPress) {
          m_quickFilterEdit->clear();
          if (m_model) m_model->setLiveFilter(QString());
          hideQuickFilter();
        }
        event->accept();
        return true;
      }
      if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (isKeyPress) {
          // フィルタは保ったまま閉じる
          m_quickFilterEdit->hide();
          if (m_view) m_view->setFocus(Qt::OtherFocusReason);
          refreshSortFilterStatus();
        }
        event->accept();
        return true;
      }
    }
  }

  if (watched != m_addressEdit) {
    return QWidget::eventFilter(watched, event);
  }

  switch (event->type()) {
    case QEvent::MouseButtonPress: {
      // 読取専用状態でクリックされたら編集モードに入る。
      if (!m_addressEditing) {
        enterAddressEdit();
        // クリックそのものはそのまま QLineEdit に流して、
        // クリック位置にカーソルを置けるようにする。
      }
      break;
    }
    case QEvent::FocusIn: {
      // Tab 経由などでフォーカスが入ったときも編集モードに入る。
      if (!m_addressEditing) {
        enterAddressEdit();
      }
      break;
    }
    case QEvent::FocusOut: {
      // 編集途中でフォーカスを失ったら破棄して元の表示に戻す。
      // (commitAddressEdit が成功したときは leaveAddressEdit 後に
      //  setFocus を view に移すので、その時点で既に編集モード OFF)
      if (m_addressEditing) {
        leaveAddressEdit(/*restoreText=*/true);
      }
      break;
    }
    case QEvent::KeyPress: {
      auto* ke = static_cast<QKeyEvent*>(event);
      if (m_addressEditing && ke->key() == Qt::Key_Escape) {
        cancelAddressEdit();
        return true;
      }
      break;
    }
    default:
      break;
  }
  return QWidget::eventFilter(watched, event);
}

QTableView* FileListPane::view() const {
  return m_view;
}

QAbstractItemView* FileListPane::activeView() const {
  if (m_viewMode == ListViewMode::Thumbnail && m_thumbnailView) {
    return m_thumbnailView;
  }
  return m_view;
}

void FileListPane::setViewMode(ListViewMode mode) {
  if (mode == m_viewMode) return;
  m_viewMode = mode;
  if (m_viewStack) {
    m_viewStack->setCurrentIndex(mode == ListViewMode::Thumbnail ? 1 : 0);
  }
  // 切替先のビューにフォーカスを渡しておく (キーナビゲーションを継続できるように)。
  if (QAbstractItemView* av = activeView()) {
    av->setFocus(Qt::OtherFocusReason);
  }
}

void FileListPane::onExternalDirectoryChanged() {
  if (!m_model) return;
  // 外部からファイル/ディレクトリが追加・削除されたあと、model を再読み込み。
  // model::refresh() は beginResetModel/endResetModel を呼ぶため currentIndex
  // が無効化されるので、ファイル名ベースでカーソルを保存・復元する。
  // ── 重要 ──
  // 比較には Name 列の **表示テキスト** ではなく `FileItem::name()` (= 拡張子
  // 込みのファイル名) を使う。Name 列の display は通常ファイルだと拡張子を
  // 剥がして表示するので、"foo/" ディレクトリと "foo.zip" ファイルが
  // 同じ親に並んでいると Name 列が両方 "foo" となり、復元時に意図しない
  // 行 (Dirs First ソートで先に出るフォルダ) が選ばれてしまう。
  QString cursorName;
  int     cursorRow = -1;
  if (m_view) {
    const QModelIndex idx = m_view->currentIndex();
    if (idx.isValid()) {
      cursorRow = idx.row();
      if (const FileItem* it = m_model->itemAt(idx)) {
        cursorName = it->name();
      }
    }
  }

  m_model->refresh();
  refreshSortFilterStatus();

  if (m_view) {
    const int rows = m_model->rowCount();
    if (rows > 0) {
      int target = -1;
      if (!cursorName.isEmpty()) {
        for (int r = 0; r < rows; ++r) {
          const FileItem* it = m_model->itemAt(r);
          if (it && it->name() == cursorName) {
            target = r;
            break;
          }
        }
      }
      if (target < 0) target = qBound(0, cursorRow, rows - 1);
      m_view->setCurrentIndex(m_model->index(target, FileListModel::Name));
    }
  }
}

bool FileListPane::setPath(const QString& path) {
  const bool wasInArchive = m_model && m_model->isInArchiveMode();
  bool result = m_model->setPath(path);
  if (result) {
    m_addressEdit->setText(m_model->currentPath());
    refreshBookmarkIndicator();
    // アーカイブモード切替に伴うアドレスバー色変更を反映
    const bool nowInArchive = m_model->isInArchiveMode();
    if (wasInArchive != nowInArchive) {
      refreshAppearance();
    }
    // 外部変更ウォッチャの監視対象を新しいパスに切り替える。
    // アーカイブ内 (仮想 FS) は実 FS ではないのでウォッチしない。
    if (m_dirWatcher) {
      const QStringList prev = m_dirWatcher->directories();
      if (!prev.isEmpty()) m_dirWatcher->removePaths(prev);
      if (!nowInArchive) {
        const QString cur = m_model->currentPath();
        if (!cur.isEmpty()) m_dirWatcher->addPath(cur);
      }
    }
    // カーソルを先頭に移動
    if (m_model->rowCount() > 0) {
      m_view->setCurrentIndex(m_model->index(0, 0));
    }

    // 現在のソート状態をヘッダーに反映
    int section = -1;
    switch (m_model->sortKey()) {
      case SortKey::None:
        // None is not a valid primary sort key, don't set indicator
        break;
      case SortKey::Name:
        section = FileListModel::Name;
        break;
      case SortKey::Size:
        section = FileListModel::Size;
        break;
      case SortKey::Type:
        section = FileListModel::Type;
        break;
      case SortKey::LastModified:
        section = FileListModel::LastModified;
        break;
    }
    if (section >= 0) {
      m_view->horizontalHeader()->setSortIndicator(section, m_model->sortOrder());
    }

    refreshSortFilterStatus();
  }
  return result;
}

namespace {

QString sortKeyLabel(SortKey k) {
  switch (k) {
    case SortKey::Name:         return QObject::tr("Name");
    case SortKey::Size:         return QObject::tr("Size");
    case SortKey::Type:         return QObject::tr("Type");
    case SortKey::LastModified: return QObject::tr("Modified");
    case SortKey::None:         return {};
  }
  return {};
}

QString dirsPlacementLabel(SortDirsType t) {
  switch (t) {
    case SortDirsType::First: return QObject::tr("Dirs First");
    case SortDirsType::Last:  return QObject::tr("Dirs Last");
    case SortDirsType::Mixed: return QObject::tr("Mixed");
  }
  return {};
}

} // anonymous namespace

void FileListPane::refreshSortFilterStatus() {
  if (!m_sortFilterStatusLabel || !m_model) return;

  const QString arrow = (m_model->sortOrder() == Qt::AscendingOrder)
                        ? QStringLiteral("↑")
                        : QStringLiteral("↓");

  // Sort 部
  QStringList sortKeys;
  sortKeys << QString("%1 %2").arg(sortKeyLabel(m_model->sortKey()), arrow);
  if (m_model->sortKey2nd() != SortKey::None) {
    sortKeys << QString("%1 %2").arg(sortKeyLabel(m_model->sortKey2nd()), arrow);
  }

  QStringList sortTail;
  sortTail << dirsPlacementLabel(m_model->sortDirsType());
  if (m_model->sortCS() == Qt::CaseSensitive) sortTail << tr("CS");
  if (m_model->sortDotFirst())                sortTail << tr("Dot-first");

  const QString sortStr = sortKeys.join(" / ") + QStringLiteral(" · ") + sortTail.join(" · ");

  // Filter 部
  QStringList filterParts;
  AttrFilterFlags attr = m_model->attrFilter();
  if (attr & AttrFilter::ShowHidden) filterParts << tr("Hidden");
  if (attr & AttrFilter::DirsOnly)   filterParts << tr("Dirs only");
  if (attr & AttrFilter::FilesOnly)  filterParts << tr("Files only");

  const QStringList names = m_model->nameFilters();
  if (!names.isEmpty()) {
    filterParts << names.join(' ');
  }

  // 即時フィルタ (Quick Filter Bar): 文字列があれば、その内容と「N / M items」を
  // フィルタ部の末尾に追加する。N は表示件数 (".." 除く)、M は全件 (".." 除く)。
  const QString liveText = m_model->liveFilter();
  if (!liveText.isEmpty()) {
    int visible = 0;
    for (int i = 0; i < m_model->rowCount(); ++i) {
      const FileItem* it = m_model->itemAt(i);
      if (it && !it->isDotDot()) ++visible;
    }
    int total = 0;
    const int totalAll = m_model->totalCount();
    // 全件側にも ".." が含まれているので 1 件引く (".." がある場合のみ)。
    // 親ディレクトリの判定は totalAll > 0 で簡略化: setPath で必ず先頭に
    // ".." を入れるので、ルート以外なら totalAll の最初の要素は ".."。
    // 簡単のため m_model->itemAt(0) を見る。
    if (totalAll > 0) {
      const FileItem* first = m_model->itemAt(0);
      // m_entries 側でフィルタされた場合 itemAt(0) が ".." とは限らないので、
      // 全件総数は totalCount() ベースで判断する。
      Q_UNUSED(first);
    }
    // 全件のうち ".." 1 件を引く。setPath 直後で ".." が必ず入る前提。
    // ルート (".." 無し) では引かない。簡単のため "親ディレクトリがあるか" は
    // m_currentPath の親の有無で判断。
    {
      const QString path = m_model->currentPath();
      if (!path.isEmpty() && QDir(path).cdUp()) {
        total = qMax(0, totalAll - 1);
      } else {
        total = totalAll;
      }
    }
    filterParts << tr("Quick: \"%1\" (%2 / %3 items)").arg(liveText)
                                                       .arg(visible)
                                                       .arg(total);
  }

  const QString filterStr = filterParts.isEmpty() ? tr("(none)") : filterParts.join(" · ");

  m_sortFilterStatusLabel->setText(tr("Sort: %1  │  Filter: %2").arg(sortStr, filterStr));
}

void FileListPane::setActive(bool active) {
  m_delegate->setActive(active);
  m_model->setActive(active);
  m_view->viewport()->update();
}

void FileListPane::onFolderButtonClicked() {
  emit folderButtonClicked();
}

void FileListPane::onCurrentChanged(const QModelIndex& current, const QModelIndex& previous) {
  emit currentChanged(current, previous);
}

void FileListPane::refreshBookmarkIndicator() {
  if (!m_bookmarkLabel) return;
  const QString path = currentPath();
  const bool marked = !path.isEmpty() && BookmarkManager::instance().contains(path);

  const QColor bg = Settings::instance().addressBackground();
  // 登録済み: ゴールド / 未登録: 背景に溶け込むグレーで無効化表示。
  // padding はアドレスラベルと揃え、left だけ広めにしてアイコン然と見せる。
  // フォーカス時 (Tab で到達したとき) はハイライト枠を出す。
  const QString color = marked ? QStringLiteral("#d4a017")
                               : QStringLiteral("#bdbdbd");
  const QString style = QString(
    "QLabel { color: %1; background-color: %2; padding: 2px 2px 2px 5px; "
    "         border: 1px solid transparent; }"
    "QLabel:focus { border: 2px solid palette(highlight); }")
    .arg(color, bg.name());
  m_bookmarkLabel->setStyleSheet(style);

  // デフォルトブックマークは削除不可のため tooltip を分ける
  bool isDefault = false;
  if (marked) {
    const int idx = BookmarkManager::instance().findByPath(path);
    if (idx >= 0) {
      const QList<Bookmark> all = BookmarkManager::instance().bookmarks();
      if (idx < all.size()) isDefault = all[idx].isDefault;
    }
  }
  QString tip;
  if (!marked)         tip = tr("Add bookmark for this directory");
  else if (isDefault)  tip = tr("Default bookmark (cannot be removed)");
  else                 tip = tr("Remove bookmark for this directory");
  m_bookmarkLabel->setToolTip(tip);
}

void FileListPane::onBookmarkButtonClicked() {
  toggleBookmarkForCurrentPath();
}

void FileListPane::toggleBookmarkForCurrentPath() {
  const QString path = currentPath();
  if (path.isEmpty()) return;

  const int existing = BookmarkManager::instance().findByPath(path);
  if (existing >= 0) {
    // 登録済み → 即削除
    BookmarkManager::instance().removeAt(existing);
    return;
  }

  // 未登録 → BookmarkEditDialog で追加（Edit と同じフォーム、パスも編集可能）
  QString defaultName;
  if (path == QDir::homePath()) {
    defaultName = tr("Home");
  } else {
    defaultName = QFileInfo(path).fileName();
    if (defaultName.isEmpty()) defaultName = path;
  }

  BookmarkEditDialog dlg(defaultName, path, this);
  dlg.setWindowTitle(tr("Add Bookmark"));
  if (dlg.exec() != QDialog::Accepted) return;
  BookmarkManager::instance().add(dlg.name(), dlg.path());
}

void FileListPane::onHeaderClicked(int section) {
  // 列番号をSortKeyに変換
  SortKey sortKey;
  switch (section) {
    case FileListModel::Name:
      sortKey = SortKey::Name;
      break;
    case FileListModel::Size:
      sortKey = SortKey::Size;
      break;
    case FileListModel::Type:
      sortKey = SortKey::Type;
      break;
    case FileListModel::LastModified:
      sortKey = SortKey::LastModified;
      break;
    default:
      return;  // 無効な列
  }

  // 同じ列がクリックされた場合はソート順を反転
  Qt::SortOrder newOrder = Qt::AscendingOrder;
  if (m_model->sortKey() == sortKey && m_model->sortOrder() == Qt::AscendingOrder) {
    newOrder = Qt::DescendingOrder;
  }

  // ソート設定を更新（第2ソートキーは現在の値を保持）
  m_model->setSortSettings(sortKey, newOrder, m_model->sortKey2nd());

  // ヘッダーにソートインジケーターを表示
  m_view->horizontalHeader()->setSortIndicator(section, newOrder);

  refreshSortFilterStatus();
}

} // namespace Farman
