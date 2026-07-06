#include "FileManagerPanel.h"
#include "FileListPane.h"
#include "FileListThumbnailView.h"
#include "LogPane.h"
#include "PreviewPane.h"
#include "PreviewController.h"
#include "ProgressDialog.h"
#include "core/Logger.h"
#include "SortFilterDialog.h"
#include "TransferConfirmDialog.h"
#include "OverwriteDialog.h"
#include "PropertiesDialog.h"
#include "BulkRenameDialog.h"
#include "DeleteConfirmDialog.h"
#include "CreateArchiveDialog.h"
#include "ExtractArchiveDialog.h"
#include "core/workers/ArchiveCreateWorker.h"
#include "core/workers/ArchiveExtractWorker.h"
#include "core/workers/ArchiveExtractEntriesWorker.h"
#include "core/workers/DirectoryCompareWorker.h"
#include "DirectoryCompareDialog.h"
#include "model/FileListModel.h"
#include "core/FileItem.h"
#include "core/ArchiveContext.h"
#include "core/ArchiveEntry.h"
#include "utils/ArchivePath.h"
#include "core/workers/CopyWorker.h"
#include "core/workers/MoveWorker.h"
#include "core/workers/RemoveWorker.h"
#include "settings/Settings.h"
#include "utils/Dialogs.h"
#include <QAbstractItemView>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QStorageInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QLocale>
#include <QSet>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTimer>
#include <QTemporaryDir>
#include <QUrl>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QTableView>
#include <QHeaderView>
#include <QFile>

namespace Farman {

FileManagerPanel::FileManagerPanel(QWidget* parent)
  : QWidget(parent)
  , m_splitter(nullptr)
  , m_leftPane(nullptr)
  , m_rightPane(nullptr)
  , m_activePane(PaneType::Left)
  , m_layoutMode(LayoutMode::Dual) {

  setupUi();
}

FileManagerPanel::~FileManagerPanel() = default;

void FileManagerPanel::setupUi() {
  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  // Splitter for two panes
  m_splitter = new QSplitter(Qt::Horizontal, this);
  layout->addWidget(m_splitter);

  // ===== Left Pane =====
  m_leftPane = new FileListPane(this);
  m_leftPane->setPaneType(PaneType::Left);
  connect(m_leftPane, &FileListPane::currentChanged, this, &FileManagerPanel::onLeftPaneCurrentChanged);
  connect(m_leftPane, &FileListPane::folderButtonClicked, this, &FileManagerPanel::onLeftFolderButtonClicked);
  // Tab 連鎖の端 (mode + Tab / ★ + Shift+Tab) → ルータで対向側へ振り分け
  connect(m_leftPane, &FileListPane::focusExitForward, this,
          [this]() { onPaneFocusExit(PaneType::Left, /*forward=*/true); });
  connect(m_leftPane, &FileListPane::focusExitBackward, this,
          [this]() { onPaneFocusExit(PaneType::Left, /*forward=*/false); });
  // ステータスバー更新: カーソル移動・モデル変化・選択変化を監視
  connect(m_leftPane, &FileListPane::currentChanged, this, [this](const QModelIndex&, const QModelIndex&) {
    emitActivePaneStatus();
  });
  connect(m_leftPane->model(), &QAbstractItemModel::dataChanged, this, [this](auto&&...) {
    emitActivePaneStatus();
  });
  connect(m_leftPane->model(), &QAbstractItemModel::modelReset, this, [this] {
    emitActivePaneStatus();
  });
  m_splitter->addWidget(m_leftPane);

  // ===== Right Pane =====
  m_rightPane = new FileListPane(this);
  m_rightPane->setPaneType(PaneType::Right);
  connect(m_rightPane, &FileListPane::currentChanged, this, &FileManagerPanel::onRightPaneCurrentChanged);
  connect(m_rightPane, &FileListPane::folderButtonClicked, this, &FileManagerPanel::onRightFolderButtonClicked);
  connect(m_rightPane, &FileListPane::focusExitForward, this,
          [this]() { onPaneFocusExit(PaneType::Right, /*forward=*/true); });
  connect(m_rightPane, &FileListPane::focusExitBackward, this,
          [this]() { onPaneFocusExit(PaneType::Right, /*forward=*/false); });
  connect(m_rightPane, &FileListPane::currentChanged, this, [this](const QModelIndex&, const QModelIndex&) {
    emitActivePaneStatus();
  });
  connect(m_rightPane->model(), &QAbstractItemModel::dataChanged, this, [this](auto&&...) {
    emitActivePaneStatus();
  });
  connect(m_rightPane->model(), &QAbstractItemModel::modelReset, this, [this] {
    emitActivePaneStatus();
  });

  // 外部 (Finder 等) や反対側ペインからのドロップを受信して
  // Copy / Move / Cancel をユーザーに尋ねるハンドラへ繋ぐ。
  connect(m_leftPane, &FileListPane::externalUrlsDropped, this,
          [this](const QList<QUrl>& urls) { handleExternalDrop(m_leftPane, urls); });
  connect(m_rightPane, &FileListPane::externalUrlsDropped, this,
          [this](const QList<QUrl>& urls) { handleExternalDrop(m_rightPane, urls); });

  m_splitter->addWidget(m_rightPane);

  // ===== Preview Pane (Phase 0 骨組み) =====
  // 右ペイン位置に常駐させておく (= Splitter の同じスロットに重ねる)。
  // 通常時 (Dual/Single) は hide() しておき、Preview モードに切替えた瞬間に
  // show() + m_rightPane->hide() で見せる。Splitter のサイズ記憶は
  // 「FileListPane 用」と「PreviewPane 用」が別物になるが、Phase 0 では
  // 共通サイズを使う (Phase 4 で別保存に分離)。
  m_previewPane = new PreviewPane(this);
  m_previewPane->hide();
  m_splitter->addWidget(m_previewPane);

  // Preview ペインで Esc が押されたら、ファイルリストの active pane へ
  // フォーカスを戻す (Tab で入った → テキスト選択した → Esc で戻る の動線)。
  connect(m_previewPane, &PreviewPane::escapePressed, this, [this]() {
    if (FileListPane* pane = activePane()) {
      if (auto* v = pane->view()) v->setFocus(Qt::OtherFocusReason);
    }
  });
  // PreviewPane の Tab 連鎖の端から呼ばれるルータ。プレビューを抜けたら
  // アクティブな FileListPane の ★ (forward) / mode (back) に戻す。
  connect(m_previewPane, &PreviewPane::focusExitForward, this,
          [this]() { onPreviewFocusExit(/*forward=*/true); });
  connect(m_previewPane, &PreviewPane::focusExitBackward, this,
          [this]() { onPreviewFocusExit(/*forward=*/false); });

  // 左ペインのカーソル変化 → PreviewController に通知 (Preview 中のみ実行)。
  m_previewController = new PreviewController(m_previewPane, this);

  // Splitterのサイズを均等に (Preview ペインは 3 番目のスロットだが
  // hide 中なので幅 0 扱いになる)。
  m_splitter->setSizes(QList<int>() << 600 << 600 << 600);

  // リサイズ時に各ペインが等しく伸縮するよう stretch を揃える。これをしないと
  // QTableView (Expanding) のファイルペインが拡大分の余白を総取りし、起動時の
  // 復元など「小さい幅で 50/50 を設定 → ウィンドウ拡大」の流れでプレビューが
  // 極端に狭くなる (Linux で顕著)。
  m_splitter->setStretchFactor(0, 1);  // 左ペイン
  m_splitter->setStretchFactor(1, 1);  // 右ペイン
  m_splitter->setStretchFactor(2, 1);  // プレビューペイン

  // スプリッタの Resize を捕まえて分割比を適用し直す (起動時に小さい幅で
  // 設定された比率を、最終サイズに広がってから正す)。下の eventFilter 参照。
  m_splitter->installEventFilter(this);
  // ユーザーが区切りをドラッグしたら比率を覚える。
  connect(m_splitter, &QSplitter::splitterMoved, this,
          [this](int, int) { updateFractionFromSizes(); });
  // 区切りのダブルクリックで 50/50 に戻す (handle に eventFilter を仕掛ける)。
  if (auto* h = m_splitter->handle(1)) h->installEventFilter(this);
  if (auto* h = m_splitter->handle(2)) h->installEventFilter(this);

  // 非アクティブペインのビューをマウスクリックされたら、そのペインを
  // アクティブに切り替える。QTableView はマウスイベントを viewport に
  // 配送するので、eventFilter は viewport 側に仕込む。
  if (m_leftPane && m_leftPane->view() && m_leftPane->view()->viewport()) {
    m_leftPane->view()->viewport()->installEventFilter(this);
  }
  if (m_rightPane && m_rightPane->view() && m_rightPane->view()->viewport()) {
    m_rightPane->view()->viewport()->installEventFilter(this);
  }

  // 行のダブルクリックで Shift+Enter (= file.execute) と同じ「OS 既定アプリ
  // で開く」処理を実行する。
  auto wireDoubleClick = [this](FileListPane* pane) {
    if (!pane || !pane->view()) return;
    connect(pane->view(), &QAbstractItemView::doubleClicked, this,
            [this, pane](const QModelIndex& idx) {
      if (!idx.isValid() || !pane->model()) return;
      const FileItem* item = pane->model()->itemAt(idx.row());
      if (!item || item->isDotDot()) return;
      const QString path = item->absolutePath();
      const bool ok = QDesktopServices::openUrl(QUrl::fromLocalFile(path));
      Logger::instance().log(ok ? Logger::Info : Logger::Warn,
        QStringLiteral("Execute: %1%2")
          .arg(path, ok ? QString() : QStringLiteral(" (failed)")));
    });
  };
  wireDoubleClick(m_leftPane);
  wireDoubleClick(m_rightPane);

  // ===== Log Pane =====
  m_logPane = new LogPane(this);
  m_logPane->setVisible(Settings::instance().logVisible());
  setLogPaneHeight(Settings::instance().logPaneHeight());
  layout->addWidget(m_logPane);
}

void FileManagerPanel::setLogPaneVisible(bool visible) {
  if (!m_logPane) return;
  // QWidget::isVisible() は祖先が show されていない段階だと false を返すため、
  // 起動シーケンス中の判定がブレる。`!isHidden()` は自身の visibility 設定
  // だけを見るので、親が show される前でも setVisible(true) 直後に true を
  // 返す。setupToolbar 等が show() 前に状態を読むケースで重要。
  const bool was = !m_logPane->isHidden();
  m_logPane->setVisible(visible);
  if (was != visible) {
    emit logPaneVisibleChanged(visible);
  }
}

bool FileManagerPanel::isLogPaneVisible() const {
  return m_logPane && !m_logPane->isHidden();
}

void FileManagerPanel::setLogPaneHeight(int px) {
  if (!m_logPane) return;
  if (px < 40) px = 40;
  m_logPane->setFixedHeight(px);
}

void FileManagerPanel::loadInitialPath() {
  auto& settings = Settings::instance();

  auto resolveInitialPath = [&](PaneType pane) -> QString {
    const QString home = QDir::homePath();
    QString primary;
    switch (settings.initialPathMode(pane)) {
      case InitialPathMode::Default:
        primary = home;
        break;
      case InitialPathMode::LastSession:
        primary = settings.paneSettings(pane).path;
        break;
      case InitialPathMode::Custom:
        primary = settings.customInitialPath(pane);
        break;
    }
    // 設定された初期ディレクトリ → ホーム → ルート の順にフォールバックし、
    // 必ず実在するディレクトリを返す。存在しないパスで起動して一覧が空に
    // なる (ネットワークドライブ切断・削除済みフォルダ等) のを防ぐ。
    // ルートは QDir::rootPath() でクロスプラットフォーム (Unix "/" /
    // Windows はシステムドライブのルート) に解決する。
    if (!primary.isEmpty() && QDir(primary).exists()) return primary;
    if (QDir(home).exists()) return home;
    return QDir::rootPath();
  };

  // 表示モード (List / Thumbnail) を Settings から復元する。navigatePane が
  // model->setPath を呼ぶよりも前にやれば、ディレクトリ読込時点で正しい
  // DecorationRole 経路 (thumbnail decode) が走る。
  m_leftPane->setViewMode(settings.paneViewMode(PaneType::Left));
  m_rightPane->setViewMode(settings.paneViewMode(PaneType::Right));

  navigatePane(PaneType::Left,  resolveInitialPath(PaneType::Left));
  navigatePane(PaneType::Right, resolveInitialPath(PaneType::Right));

  // 左ペインをアクティブに
  setActivePane(PaneType::Left);

  updatePathSignal();
}

void FileManagerPanel::applySettings() {
  // Behavior タブでデフォルト設定が変更された際の再適用。
  // setSortSettings / setAttrFilter / refresh は beginResetModel/endResetModel を
  // 呼ぶため selection model の currentIndex が無効化され、戻ってきたときに
  // カーソル下線が消えてしまう。前後でファイル名ベースに保存・復元する。
  auto saveCursor = [](FileListPane* pane) -> QString {
    auto* view = pane->view();
    const QModelIndex idx = view->currentIndex();
    if (!idx.isValid()) return {};
    return pane->model()->data(pane->model()->index(idx.row(), FileListModel::Name)).toString();
  };
  auto restoreCursor = [](FileListPane* pane, const QString& fileName, int fallbackRow) {
    auto* view  = pane->view();
    auto* model = pane->model();
    const int rows = model->rowCount();
    if (rows == 0) return;
    int target = -1;
    if (!fileName.isEmpty()) {
      for (int r = 0; r < rows; ++r) {
        if (model->data(model->index(r, FileListModel::Name)).toString() == fileName) {
          target = r;
          break;
        }
      }
    }
    if (target < 0) target = qBound(0, fallbackRow, rows - 1);
    const QModelIndex idx = model->index(target, FileListModel::Name);
    view->setCurrentIndex(idx);
  };

  const QString leftName  = saveCursor(m_leftPane);
  const QString rightName = saveCursor(m_rightPane);
  const int leftRow  = m_leftPane->view()->currentIndex().row();
  const int rightRow = m_rightPane->view()->currentIndex().row();

  // パス単位の override があればそちらを優先する。
  applyPathSortFilter(PaneType::Left);
  applyPathSortFilter(PaneType::Right);

  // パス表示・カーソル色など外観の再適用
  m_leftPane->refreshAppearance();
  m_rightPane->refreshAppearance();

  // 列表示 (Behavior の Columns 設定) を現在のモードで再適用
  m_leftPane->applyColumnVisibility(isSinglePaneMode());
  m_rightPane->applyColumnVisibility(isSinglePaneMode());

  // 表示モード (List / Thumbnail) を Settings から再適用。view.toggle_thumbnails
  // コマンド経由以外 (Settings ダイアログから直接編集された場合 / 起動直後の
  // 初回適用) も同じ経路に乗る。setViewMode は同一モードなら no-op。
  m_leftPane->setViewMode(Settings::instance().paneViewMode(PaneType::Left));
  m_rightPane->setViewMode(Settings::instance().paneViewMode(PaneType::Right));
  // サムネイルサイズ (グローバル) も再適用。Appearance タブからの変更を
  // 両ペインの delegate / view / model に反映する。
  m_leftPane->reapplyThumbnailMetrics();
  m_rightPane->reapplyThumbnailMetrics();

  m_leftPane->model()->refresh();
  m_rightPane->model()->refresh();

  restoreCursor(m_leftPane,  leftName,  leftRow);
  restoreCursor(m_rightPane, rightName, rightRow);

  // ダイアログから戻った後にアクティブペインへフォーカスを戻す
  activePane()->view()->setFocus();
}

void FileManagerPanel::applyPathSortFilter(PaneType paneType) {
  FileListPane* pane = (paneType == PaneType::Left) ? m_leftPane : m_rightPane;
  FileListModel* model = pane->model();
  auto& settings = Settings::instance();
  const QString path = pane->currentPath();

  PaneSettings s = settings.hasPathOverride(path)
                   ? settings.pathOverride(path)
                   : settings.paneSettings(paneType);

  model->setSortSettings(
    s.sortKey, s.sortOrder, s.sortKey2nd,
    s.sortDirsType, s.sortDotFirst, s.sortCS
  );
  model->setAttrFilter(s.attrFilter);
  model->setNameFilters(s.nameFilters);

  // Header sort indicator を現在のソート状態に合わせる
  int section = -1;
  switch (s.sortKey) {
    case SortKey::Name:         section = FileListModel::Name;         break;
    case SortKey::Size:         section = FileListModel::Size;         break;
    case SortKey::Type:         section = FileListModel::Type;         break;
    case SortKey::LastModified: section = FileListModel::LastModified; break;
    case SortKey::None: break;
  }
  if (section >= 0) {
    pane->view()->horizontalHeader()->setSortIndicator(section, s.sortOrder);
  }

  pane->refreshSortFilterStatus();
}

bool FileManagerPanel::navigatePane(PaneType paneType, const QString& path) {
  // ディレクトリ比較モード中にパス遷移されたら自動 OFF。
  // ただし Sync Browse が ON の場合は、反対ペインも追従するので比較関係が
  // 維持される (同名サブツリーを並行に walk して差分を見る使い方)。
  // その場合は maybeSyncFollow が完了後に recompareIfActive を呼んで overlay を
  // 新しいペアで更新する。
  if (m_compareMode && !m_syncBrowseEnabled && !m_syncBrowseInProgress) {
    Logger::instance().info(tr("Directory compare: disabled (navigation)"));
    stopDirectoryCompare();
  }
  FileListPane* pane = (paneType == PaneType::Left) ? m_leftPane : m_rightPane;
  FileListModel* model = pane->model();
  auto& settings = Settings::instance();

  // setSortSettings / setAttrFilter / setNameFilters は beginResetModel /
  // endResetModel を起こすので currentIndex が無効化される。setPath が失敗
  // すると古いディレクトリの内容のまま戻ってくるが、currentIndex は無くなる
  // ため、ユーザーから「カーソルが消えた」ように見える。失敗時の復元用に
  // 現在のカーソル名を保存しておく。
  QString savedCursorName;
  if (const FileItem* it = model->itemAt(pane->view()->currentIndex())) {
    savedCursorName = it->name();
  }

  PaneSettings s = settings.hasPathOverride(path)
                   ? settings.pathOverride(path)
                   : settings.paneSettings(paneType);

  model->setSortSettings(
    s.sortKey, s.sortOrder, s.sortKey2nd,
    s.sortDirsType, s.sortDotFirst, s.sortCS
  );
  model->setAttrFilter(s.attrFilter);
  model->setNameFilters(s.nameFilters);

  // 同期ブラウズの「相対変化」計算用に、setPath 前のパスを取っておく。
  const QString oldPath = pane->currentPath();
  const bool ok = pane->setPath(path);
  if (ok) {
    DirectoryHistory& hist = (paneType == PaneType::Left) ? m_leftHistory : m_rightHistory;
    hist.push(pane->currentPath());
    Logger::instance().info(QStringLiteral("%1 pane: %2")
      .arg(paneType == PaneType::Left ? QStringLiteral("Left") : QStringLiteral("Right"))
      .arg(pane->currentPath()));
    // 同期ブラウズが有効ならもう一方のペインに同じ相対変化を適用する。
    maybeSyncFollow(paneType, oldPath, pane->currentPath());
  } else if (!savedCursorName.isEmpty()) {
    // setPath が失敗 → 古いディレクトリの内容で残るが currentIndex が無いので
    // 元のファイル名で復元する。
    for (int i = 0; i < model->rowCount(); ++i) {
      const FileItem* it = model->itemAt(i);
      if (it && it->name() == savedCursorName) {
        pane->view()->setCurrentIndex(model->index(i, 0));
        break;
      }
    }
  }
  return ok;
}

void FileManagerPanel::maybeSyncFollow(PaneType navigatedPane,
                                       const QString& oldPath,
                                       const QString& newPath) {
  if (!m_syncBrowseEnabled)    return;
  if (m_syncBrowseInProgress)  return;  // 反対ペイン navigate からの再帰を防ぐ
  if (!isDualPaneMode())       return;  // Single / Preview では相方が居ない
  if (oldPath == newPath)      return;  // 実質変化なし

  const PaneType otherType = (navigatedPane == PaneType::Left) ? PaneType::Right : PaneType::Left;
  FileListPane* other = (otherType == PaneType::Left) ? m_leftPane : m_rightPane;
  if (!other) return;

  // 「片方のペインが oldPath → newPath で進んだ相対変化」を、もう一方の
  // 現在地に同じく適用して目的地を作る。
  //   例 1: cd into "src"
  //         oldPath=/old/farman   newPath=/old/farman/src
  //         relative="src"   other_current=/new/farman   target=/new/farman/src
  //   例 2: cd .. (親へ)
  //         oldPath=/old/farman/src   newPath=/old/farman
  //         relative=".."   other_current=/new/farman/src   target=/new/farman
  //   例 3: 全く関係ない場所への絶対ジャンプ
  //         oldPath=/old/farman   newPath=/tmp/foo
  //         relative="../../tmp/foo" を /new/farman に適用 → /tmp/foo
  //         多くの場合 other 側に存在しないため "skipped" でログのみ。
  const QString relative = QDir(oldPath).relativeFilePath(newPath);
  const QString target   = QDir::cleanPath(QDir(other->currentPath()).absoluteFilePath(relative));

  if (!QFileInfo(target).isDir()) {
    // 追従先が存在しない時点で「同期できなくなった」と扱い、同期を自動解除する。
    // 据え置きを続けると左右がどんどんズレた状態でユーザーがそれに気付かない
    // 恐れがあるため、明示的に OFF にしてユーザーに状態を知らせる。
    Logger::instance().info(
      QStringLiteral("Sync Browse: disabled (target not found: %1)").arg(target));
    setSyncBrowseEnabled(false);
    // 比較モードも同時に OFF にしないと、左ペインだけ overlay が消えて右だけ
    // 残るような不整合状態になる。
    if (m_compareMode) {
      Logger::instance().info(tr("Directory compare: disabled (sync browse target missing)"));
      stopDirectoryCompare();
    }

    // 通知ダイアログ (Settings で抑制可)。ダイアログ内の「次回以降表示しない」
    // チェックを ON にすると、その瞬間 Settings を更新して以後表示しなくなる。
    auto& settings = Settings::instance();
    if (settings.syncBrowseShowDisabledDialog()) {
      const bool keep = informWithSuppress(
        this,
        tr("Sync Browse Disabled"),
        tr("Sync Browse was turned off because the matching directory "
           "was not found in the other pane:\n\n%1").arg(target),
        /*initiallyShow=*/true);
      if (!keep) {
        settings.setSyncBrowseShowDisabledDialog(false);
        settings.save();
      }
    }
    return;
  }

  m_syncBrowseInProgress = true;
  navigatePane(otherType, target);
  m_syncBrowseInProgress = false;

  // 同期追従が成功したので、比較モード中なら新しい左右ペアで再比較。
  // 「対応するサブツリーを階層ごとに歩いて差分を見る」運用を成立させる。
  if (m_compareMode) {
    // ただし追従の結果、左右が同一ディレクトリになった場合は比較する意味が
    // 無いので自動解除 (startDirectoryCompare の同パスガードと同じ理屈)。
    const QString lp = QDir::cleanPath(m_leftPane->currentPath());
    const QString rp = QDir::cleanPath(m_rightPane->currentPath());
    if (lp == rp) {
      Logger::instance().info(
        tr("Directory compare: disabled (panes converged to same directory)"));
      stopDirectoryCompare();
    } else {
      recompareIfActive();
    }
  }
}

void FileManagerPanel::setSyncBrowseEnabled(bool enabled) {
  if (m_syncBrowseEnabled == enabled) return;

  if (enabled && !isDualPaneMode()) {
    Logger::instance().warn(
      tr("Sync Browse cannot be enabled while single pane mode is active."));
    return;
  }

  m_syncBrowseEnabled = enabled;
  Logger::instance().info(enabled
    ? QStringLiteral("Sync Browse: ON")
    : QStringLiteral("Sync Browse: OFF"));

  emit syncBrowseChanged(enabled);
}

void FileManagerPanel::toggleSyncBrowse() {
  if (!isDualPaneMode()) {
    Logger::instance().info(
      tr("Sync Browse toggle ignored: single pane mode is active."));
    return;
  }
  setSyncBrowseEnabled(!m_syncBrowseEnabled);
}


DirectoryHistory& FileManagerPanel::history(PaneType pane) {
  return (pane == PaneType::Left) ? m_leftHistory : m_rightHistory;
}

const DirectoryHistory& FileManagerPanel::history(PaneType pane) const {
  return (pane == PaneType::Left) ? m_leftHistory : m_rightHistory;
}

bool FileManagerPanel::navigateActivePaneTo(const QString& path) {
  if (path.isEmpty()) return false;

  // パスがファイルなら、その親ディレクトリへ遷移したうえでカーソルを
  // そのファイルに合わせる。ディレクトリならそのまま遷移。
  QFileInfo info(path);
  QString dirPath = path;
  QString fileName;
  if (info.exists() && !info.isDir()) {
    dirPath  = info.absolutePath();
    fileName = info.fileName();
  }

  if (!navigatePane(m_activePane, dirPath)) return false;

  if (!fileName.isEmpty()) {
    FileListPane* pane = activePane();
    FileListModel* model = pane->model();
    for (int i = 0; i < model->rowCount(); ++i) {
      const FileItem* item = model->itemAt(i);
      if (item && item->name() == fileName) {
        pane->view()->setCurrentIndex(model->index(i, 0));
        break;
      }
    }
  }
  updatePathSignal();
  return true;
}

void FileManagerPanel::handleExternalDrop(FileListPane* destPane,
                                          const QList<QUrl>& urls) {
  if (!destPane || urls.isEmpty()) return;

  // URL → ローカルパス。リモート URL はサポート外なので落とす。
  QStringList srcPaths;
  for (const QUrl& u : urls) {
    if (u.isLocalFile()) srcPaths.append(u.toLocalFile());
  }
  if (srcPaths.isEmpty()) return;

  const QString destDir = destPane->currentPath();

  // 自分自身のディレクトリへの drop は無視する (同名衝突を防ぐ)
  // (反対ペインから別ディレクトリへの drop は OK)
  for (const QString& src : srcPaths) {
    if (QFileInfo(src).absolutePath() == destDir) {
      return;
    }
  }

  // ドロップを受けたペインをアクティブ化しておく
  setActivePane(destPane == m_leftPane ? PaneType::Left : PaneType::Right);

  // Copy / Move / Cancel をユーザーに尋ねる
  const int choice = choose(this,
    tr("Drop Files"),
    tr("Drop %1 item(s) into\n%2\n\nWhat would you like to do?")
      .arg(srcPaths.size()).arg(destDir),
    {
      { tr("Copy"),   Qt::Key_C },
      { tr("Move"),   Qt::Key_M },
      { tr("Cancel"), Qt::Key_X },
    },
    /*defaultIndex=*/0,
    /*cancelIndex=*/2,
    DialogIcon::Question);
  if (choice != 0 && choice != 1) return;

  const bool isMove = (choice == 1);

  // Worker + ProgressDialog を起動。OverwriteMode は Ask。
  const QString tmpl = Settings::instance().autoRenameTemplate();
  WorkerBase* worker = nullptr;
  if (isMove) {
    auto* mv = new MoveWorker(srcPaths, destDir, this);
    mv->setOverwriteMode(OverwriteMode::Ask);
    mv->setAutoRenameTemplate(tmpl);
    worker = mv;
  } else {
    auto* cp = new CopyWorker(srcPaths, destDir, this);
    cp->setOverwriteMode(OverwriteMode::Ask);
    cp->setAutoRenameTemplate(tmpl);
    worker = cp;
  }
  connect(worker, &WorkerBase::overwriteRequired, this,
    [this](const QString& src, const QString& dst, OverwriteDecision* decision) {
      OverwriteDialog dlg(src, dst, this);
      dlg.exec();
      *decision = dlg.decision();
    },
    Qt::BlockingQueuedConnection);

  ProgressDialog* dialog = new ProgressDialog(
    isMove ? tr("Moving files...") : tr("Copying files..."), this);
  dialog->setWorker(worker);

  const int srcCount = srcPaths.size();
  connect(worker, &WorkerBase::finished, this,
    [this, isMove, srcCount, destDir](bool success) {
      Logger::instance().log(success ? Logger::Info : Logger::Error,
        QStringLiteral("%1 %2: %3 item(s) → %4")
          .arg(isMove ? QStringLiteral("Move") : QStringLiteral("Copy"))
          .arg(success ? QStringLiteral("done (drop)") : QStringLiteral("failed (drop)"))
          .arg(srcCount).arg(destDir));
      // 両ペインを再読込 (移動元・移動先のどちらが反対側ペインかは不明なので両方)
      m_leftPane->setPath(m_leftPane->currentPath());
      m_rightPane->setPath(m_rightPane->currentPath());
      activePane()->view()->setFocus();
    });

  worker->start();
  dialog->exec();
  worker->wait();
  worker->deleteLater();
  dialog->deleteLater();
}

void FileManagerPanel::syncOtherToActive() {
  const PaneType other = (m_activePane == PaneType::Left) ? PaneType::Right : PaneType::Left;
  const QString path = activePane()->currentPath();
  if (path.isEmpty()) return;
  navigatePane(other, path);
  updatePathSignal();
}

bool FileManagerPanel::eventFilter(QObject* watched, QEvent* event) {
  // 区切り (QSplitterHandle) のダブルクリックで 50/50 に戻す。
  if (event->type() == QEvent::MouseButtonDblClick
      && qobject_cast<QSplitterHandle*>(watched)) {
    m_leftPaneFraction = 0.5;
    applyFraction();
    return true;
  }

  // スプリッタがリサイズされたら分割比を適用し直す。Resize ハンドラより後の
  // QSplitter 自身の再配分で上書きされないよう、次のイベントループに遅延させる。
  // 起動時は Preview 復元がウィンドウ最終サイズより前に走り、その時点では幅 0 で
  // applyFraction が効かないが、ここでの遅延適用が最終サイズで確実に効く。
  if (watched == m_splitter && event->type() == QEvent::Resize) {
    scheduleApplyFraction();
    return QWidget::eventFilter(watched, event);
  }

  if (event->type() == QEvent::MouseButtonPress) {
    // クリックされた viewport がどちらのペインかを判定して、非アクティブ側
    // だった場合はそのペインをアクティブに切り替える。実際のクリック
    // (カーソル移動など) は通常通り続行させたいので return false。
    QObject* leftVp  = (m_leftPane && m_leftPane->view())   ? m_leftPane->view()->viewport()  : nullptr;
    QObject* rightVp = (m_rightPane && m_rightPane->view()) ? m_rightPane->view()->viewport() : nullptr;
    if (leftVp && watched == leftVp && m_activePane != PaneType::Left) {
      setActivePane(PaneType::Left);
    } else if (rightVp && watched == rightVp && m_activePane != PaneType::Right) {
      setActivePane(PaneType::Right);
    }
  }
  return QWidget::eventFilter(watched, event);
}

void FileManagerPanel::syncActiveToOther() {
  FileListPane* otherPane = (m_activePane == PaneType::Left) ? m_rightPane : m_leftPane;
  const QString path = otherPane->currentPath();
  if (path.isEmpty()) return;
  navigatePane(m_activePane, path);
  updatePathSignal();
}

bool FileManagerPanel::handleKeyEvent(QKeyEvent* event) {
  // Tab 連鎖 (左ペインの場合): ★ → addr → 📁 → view → mode → <対向ペイン>
  //   - view 上で Tab 押下 → mode へ
  //   - view 上で Shift+Tab 押下 → 📁 へ
  //   - mode + Tab → 対向ペイン (FileListPane::focusExitForward シグナル経由)
  //   - ★ + Shift+Tab → 対向ペイン (focusExitBackward シグナル経由)
  // 対向 = Dual: 反対 FileListPane / Preview: PreviewPane / Single: 自分にラップ
  const bool shifted = (event->modifiers() & Qt::ShiftModifier);
  if (event->key() == Qt::Key_Tab && !shifted) {
    if (FileListPane* pane = activePane()) {
      pane->focusModeButton();
      return true;
    }
  }
  if (event->key() == Qt::Key_Backtab
      || (event->key() == Qt::Key_Tab && shifted)) {
    if (FileListPane* pane = activePane()) {
      pane->focusFolderButton();
      return true;
    }
  }

  // ここから先は矢印 / 文字キーなどカーソル操作。List / Thumbnail 両対応。
  // 両ビューは selectionModel を共有するので、active view に setCurrentIndex
  // すれば非 active 側も同期する (scrollTo は active 側でのみ走る)。
  FileListPane* pane = activePane();
  if (!pane) return false;
  FileListModel* model = pane->model();
  QAbstractItemView* av = pane->activeView();
  if (!av) av = pane->view();
  // サムネイル (グリッド) 表示では左右=横移動 / 上下=縦移動にする。列数を実
  // レイアウトから取得し、端 (左右端の列 / 上下端の行) を越える方向キーのとき
  // だけ従来のペイン移動・親移動にフォールバックする。
  auto* thumbView = qobject_cast<FileListThumbnailView*>(av);
  const bool grid = (thumbView != nullptr);
  const int cols  = grid ? thumbView->gridColumnCount() : 1;

  // 左キーの処理
  if (event->key() == Qt::Key_Left) {
    const QModelIndex cur = av->currentIndex();
    // グリッドで左端列より右にいるなら横移動。
    if (grid && cur.isValid() && (cur.row() % cols) > 0) {
      av->setCurrentIndex(model->index(cur.row() - 1, 0));
      return true;
    }
    // 左端列 (または List 表示) → 従来動作。
    //   シングル / プレビュー、または左ペイン: 親ディレクトリへ
    //   右ペイン: 左ペインへ切り替え
    if (!isDualPaneMode() || m_activePane == PaneType::Left) {
      handleBackspaceKey();
    } else {
      setActivePane(PaneType::Left);
    }
    return true;
  }

  // 右キーの処理
  if (event->key() == Qt::Key_Right) {
    const QModelIndex cur = av->currentIndex();
    // グリッドで右端列より左、かつ最終要素でないなら横移動。
    if (grid && cur.isValid()) {
      const int r = cur.row();
      const int count = model->rowCount();
      if ((r % cols) < cols - 1 && r < count - 1) {
        av->setCurrentIndex(model->index(r + 1, 0));
        return true;
      }
    }
    // 右端列 / 最終要素 (または List 表示) → 従来動作。
    //   シングル / プレビュー: 相方ペインが居ないので何もしない
    //   右ペイン: 親ディレクトリへ / 左ペイン: 右ペインへ切り替え
    if (!isDualPaneMode()) {
      return true;
    }
    if (m_activePane == PaneType::Right) {
      handleBackspaceKey();
    } else {
      setActivePane(PaneType::Right);
    }
    return true;
  }

  // Ctrl+A (Windows/Linux) or Cmd+A (Mac) for select all
  if ((event->modifiers() & Qt::ControlModifier || event->modifiers() & Qt::MetaModifier)
      && event->key() == Qt::Key_A) {
    handleSelectAllKey();
    return true;
  }

  switch (event->key()) {
    case Qt::Key_Up: {
      QModelIndex current = av->currentIndex();
      if (!current.isValid()) {
        return true;
      }
      const int rows = model->rowCount();
      // グリッドでは 1 行 (= cols 個) 上へ。List では 1 個上へ。
      const int step = grid ? cols : 1;
      if (current.row() - step >= 0) {
        av->setCurrentIndex(model->index(current.row() - step, 0));
      } else if (!grid && Settings::instance().cursorLoop() && rows > 0) {
        // List の最上段ループのみ維持 (グリッドの縦ループは直感的でないので無効)。
        av->setCurrentIndex(model->index(rows - 1, 0));
      }
      return true;
    }

    case Qt::Key_Down: {
      QModelIndex current = av->currentIndex();
      if (!current.isValid()) {
        return true;
      }
      const int rows = model->rowCount();
      const int step = grid ? cols : 1;
      if (current.row() + step < rows) {
        av->setCurrentIndex(model->index(current.row() + step, 0));
      } else if (!grid && Settings::instance().cursorLoop() && rows > 0) {
        av->setCurrentIndex(model->index(0, 0));
      }
      return true;
    }

    case Qt::Key_Home: {
      if (model->rowCount() > 0) {
        av->setCurrentIndex(model->index(0, 0));
      }
      return true;
    }

    case Qt::Key_End: {
      int lastRow = model->rowCount() - 1;
      if (lastRow >= 0) {
        av->setCurrentIndex(model->index(lastRow, 0));
      }
      return true;
    }

    case Qt::Key_Return:
    case Qt::Key_Enter:
      handleEnterKey();
      return true;

    case Qt::Key_Backspace:
      handleBackspaceKey();
      return true;

    case Qt::Key_Space:
      handleSpaceKey();
      return true;

    case Qt::Key_Asterisk:
      handleAsteriskKey();
      return true;

    default:
      break;
  }

  // 印字可能な文字キー: Shift キー押下時のみ頭文字検索を発動させる
  // (Qt 標準の keyboardSearch は短時間内の入力を蓄積するため連打時に挙動が崩れる)
  if (!event->text().isEmpty()) {
    const QChar ch = event->text().at(0);
    if (ch.isPrint()) {
      if (event->modifiers() & Qt::ShiftModifier) {
        const int rowCount = model->rowCount();
        if (rowCount > 0) {
          const int startRow = av->currentIndex().isValid()
                                   ? av->currentIndex().row()
                                   : -1;
          for (int i = 1; i <= rowCount; ++i) {
            const int row = (startRow + i) % rowCount;
            const QString name = model->data(model->index(row, 0), Qt::DisplayRole).toString();
            bool match = name.startsWith(ch, Qt::CaseInsensitive);
            // ドットから始まるファイル/ディレクトリ (.gitignore, .claude 等) は
            // 設定で有効な場合のみ 2 文字目もマッチ対象にする
            if (!match && Settings::instance().typeAheadIncludeDotfiles()
                && name.size() >= 2 && name.at(0) == QLatin1Char('.')) {
              match = name.at(1).toLower() == ch.toLower();
            }
            if (match) {
              av->setCurrentIndex(model->index(row, 0));
              break;
            }
          }
        }
      }
      // Shift の有無に関わらず文字キーは消費 (Qt 標準の検索を発動させない)
      return true;
    }
  }

  return false;
}

void FileManagerPanel::onPaneFocusExit(PaneType from, bool forward) {
  // Tab 連鎖の端 (mode + Tab / ★ + Shift+Tab) → レイアウトに応じた行き先へ。
  //   - Dual    : 対向ペインの ★ (forward) / mode (back)
  //   - Preview : PreviewPane の先頭 (forward) / 末尾 (back)。
  //               プレビュー側に focusable が無ければ同ペインにラップ。
  //   - Single  : 反対ペインが居ない → 同ペインの中でラップ
  switch (m_layoutMode) {
    case LayoutMode::Single: {
      FileListPane* self = (from == PaneType::Left) ? m_leftPane : m_rightPane;
      if (!self) return;
      if (forward) self->focusBookmarkLabel();
      else         self->focusModeButton();
      return;
    }
    case LayoutMode::Dual: {
      const PaneType otherType =
        (from == PaneType::Left) ? PaneType::Right : PaneType::Left;
      FileListPane* other =
        (otherType == PaneType::Left) ? m_leftPane : m_rightPane;
      if (!other) return;
      // アクティブペインも反対側に切替える (setActivePane 内で view への
      // setFocus が走るが、続けて bookmark / mode に setFocus し直すので OK)。
      setActivePane(otherType);
      if (forward) other->focusBookmarkLabel();
      else         other->focusModeButton();
      return;
    }
    case LayoutMode::Preview: {
      // Preview 中は from は通常 Left (setLayoutMode が強制している) だが、
      // どちらから呼ばれてもプレビュー側へ。
      if (!m_previewPane) return;
      const bool ok = forward ? m_previewPane->focusFirstControl()
                              : m_previewPane->focusLastControl();
      if (!ok) {
        // プレビュー側に focusable な相手が無い (Empty / Loading 等)。
        // 自ペインの反対端にラップして「Tab で永久ループしない」状態を作る。
        if (FileListPane* self = activePane()) {
          if (forward) self->focusBookmarkLabel();
          else         self->focusModeButton();
        }
      }
      return;
    }
  }
}

void FileManagerPanel::onPreviewFocusExit(bool forward) {
  // PreviewPane を抜ける → アクティブな FileListPane (= 左) の頭 / 末尾に戻す。
  // forward の連鎖を維持したいので、forward なら ★、back なら mode。
  FileListPane* pane = activePane();
  if (!pane) return;
  if (forward) pane->focusBookmarkLabel();
  else         pane->focusModeButton();
}

void FileManagerPanel::onLeftPaneCurrentChanged(const QModelIndex& current, const QModelIndex& previous) {
  Q_UNUSED(previous);
  m_leftPane->view()->viewport()->update();

  // Preview レイアウト中だけ、カーソル変化をプレビューコントローラに転送する。
  // (Single / Dual では右ペインはファイルリストなのでプレビュー対象外)
  if (m_layoutMode != LayoutMode::Preview || !m_previewController) {
    return;
  }
  if (!current.isValid() || !m_leftPane->model()) {
    m_previewController->clearPreview();
    return;
  }
  const FileItem* item = m_leftPane->model()->itemAt(current);
  if (!item) {
    m_previewController->clearPreview();
    return;
  }
  FileListModel* lmodel = m_leftPane->model();
  // アーカイブ内ブラウジング中なら、エントリを一時展開して通常の prepareLoad
  // 経路に流す経路を取る。ディレクトリ / `..` は通常 API で状態表示に流す。
  if (lmodel->isInArchiveMode() && item->isInArchive()
      && !item->isDir() && !item->isDotDot()) {
    const ArchiveEntry*   ae  = item->archiveEntry();
    const ArchiveContext* ctx = lmodel->archiveContext();
    if (ae && ctx) {
      m_previewController->requestArchivePreview(
        ctx,
        ae->pathInArchive,
        /*displayPath=*/ item->absolutePath(),   // "<archive>!/<inner>"
        ae->size);
      return;
    }
    // ae / ctx が取れない異常系は通常経路にフォールバック
  }
  m_previewController->requestPreview(
    item->absolutePath(),
    /*displayPath=*/ QString(),
    item->isDir(),
    item->isDotDot(),
    item->size());
}

void FileManagerPanel::onRightPaneCurrentChanged(const QModelIndex& current, const QModelIndex& previous) {
  Q_UNUSED(current);
  Q_UNUSED(previous);
  m_rightPane->view()->viewport()->update();
}

void FileManagerPanel::handleEnterKey() {
  FileListPane* pane = activePane();
  FileListModel* model = pane->model();

  QModelIndex currentIndex = pane->view()->currentIndex();
  if (!currentIndex.isValid()) {
    return;
  }

  const FileItem* item = model->itemAt(currentIndex);
  if (!item) {
    return;
  }

  // アーカイブ内ブラウジングからの ".." はアーカイブを抜けて元 FS に戻る
  if (item->isDotDot() && model->isInArchiveMode()) {
    handleBackspaceKey();
    return;
  }

  if (item->isDir()) {
    // ディレクトリに入る
    QString newPath = item->absolutePath();
    if (navigatePane(m_activePane, newPath)) {
      updatePathSignal();
    }
    return;
  }

  // ファイル: アーカイブ拡張子なら中に潜る (通常 FS 上のアーカイブのみ)。
  if (!model->isInArchiveMode() &&
      ArchivePath::isArchiveExtension(item->absolutePath())) {
    const QString archiveRootPath =
      ArchivePath::joinArchivePath(item->absolutePath(), QStringLiteral("/"));
    if (navigatePane(m_activePane, archiveRootPath)) {
      updatePathSignal();
    } else {
      // open 失敗 (パスワード付き / フォーマット非対応 / キャンセル等)。
      // FileListModel が lastLoadError に詳細メッセージを残しているので、
      // それを使ったエラーダイアログを出してユーザーに知らせる。
      const QString reason = model->lastLoadError();
      warn(this, tr("Cannot Open Archive"),
        reason.isEmpty() ? tr("Failed to open archive: %1").arg(item->absolutePath())
                         : reason);
      Logger::instance().warn(
        tr("Archive open failed: %1 (%2)")
          .arg(item->absolutePath(), reason));
      // ProgressDialog → QMessageBox を経由するとファイルリストから focus が
      // 外れて、currentIndex は残っていてもカーソル行のハイライトが描画され
      // なくなる。明示的に focus を戻す。
      pane->view()->setFocus();
    }
    return;
  }

  // アーカイブ内のファイル: 一時ファイルに展開してビュアーで開く (Phase B)。
  // 一時ファイルはセッション一時ディレクトリ (QTemporaryDir) 配下に置き、
  // アプリ終了時に丸ごと削除する (= QTemporaryDir のデストラクタに任せる)。
  // 同じエントリを再度開いた場合は既存ファイルを上書き再展開する (シンプル化)。
  if (model->isInArchiveMode() && !item->isDir()) {
    const ArchiveEntry* ae = item->archiveEntry();
    const ArchiveContext* ctx = model->archiveContext();
    if (!ae || !ctx) return;

    // セッション一時ディレクトリ (アプリ生存中だけ存在、終了時に削除)。
    static QTemporaryDir sessionTempDir(
      QDir::tempPath() + QStringLiteral("/farman-arch-XXXXXX"));
    if (!sessionTempDir.isValid()) {
      Logger::instance().error(
        tr("Failed to create temp directory for archive extract"));
      return;
    }

    // アーカイブごとに sub directory を作っておく (複数アーカイブ並行表示時の
    // パス衝突回避 + ファイル名・パス階層をなるべく保存して viewer に渡す)。
    const QByteArray hash = QCryptographicHash::hash(
      ctx->archivePath.toUtf8(), QCryptographicHash::Sha1).toHex().left(8);
    const QString tempRoot = sessionTempDir.path()
      + QStringLiteral("/") + QString::fromLatin1(hash);
    // Zip Slip 対策: ビュアー用 temp 展開先も `..` / 絶対パス / `\` 経由の
    // 脱出を拒否する safeJoinExtractPath 経由にする。展開 worker 側と同じ
    // 防御線を temp 展開経路にも揃える狙い。
    const QString tempPath = ArchivePath::safeJoinExtractPath(
      tempRoot, ae->pathInArchive);
    if (tempPath.isEmpty()) {
      Logger::instance().error(
        tr("Refused unsafe archive entry path: %1").arg(ae->pathInArchive));
      return;
    }

    if (!ctx->extractEntryTo(ae->pathInArchive, tempPath)) {
      Logger::instance().error(
        tr("Failed to extract '%1' from archive").arg(ae->pathInArchive));
      return;
    }
    Logger::instance().info(
      tr("Extracted archive entry to temp: %1").arg(ae->pathInArchive));
    // 表示用パスはアーカイブ内パス "<archive>!/<inner>" にする (ステータス
    // バーやビュアーのタイトルに一時パスが見えるのを避ける)。
    emit fileActivated(tempPath, item->absolutePath());
    return;
  }

  // 通常ファイル: シグナルを発行 (MainWindow がビュアーを開く)
  emit fileActivated(item->absolutePath());
}

void FileManagerPanel::handleBackspaceKey() {
  FileListPane* pane = activePane();
  FileListModel* model = pane->model();

  QString currentPath = model->currentPath();
  if (currentPath.isEmpty()) {
    return;
  }

  // ── アーカイブ内ブラウジング ──────────────────
  // アーカイブモードの場合は `!` セパレータベースで親を計算する。
  //   - サブディレクトリにいる → 1 段親へ (まだアーカイブ内)
  //   - ルート "/" にいる       → アーカイブを抜けて元 FS の親ディレクトリへ、
  //                              カーソルをアーカイブファイル自身に合わせる
  if (model->isInArchiveMode()) {
    const QString archiveAbs    = model->archivePath();
    const QString innerCurrent  = model->archiveInnerPath();
    if (innerCurrent.isEmpty() || innerCurrent == QStringLiteral("/")) {
      // アーカイブを抜ける。
      // ".zip" を持つファイルと同名のディレクトリ ("foo.zip" と "foo/") が
      // 並んでいる場合、name() 比較だと拡張子付きのファイル名と
      // 拡張子無しのフォルダ名が別物なので一意に決まるはずだが、
      // パス全体の絶対パス比較の方が確実なので absolutePath() で照合する。
      const QString parentDir = QFileInfo(archiveAbs).absolutePath();
      if (navigatePane(m_activePane, parentDir)) {
        FileListModel* parentModel = pane->model();
        for (int i = 0; i < parentModel->rowCount(); ++i) {
          const FileItem* it = parentModel->itemAt(i);
          if (it && it->absolutePath() == archiveAbs) {
            const QModelIndex idx = parentModel->index(i, 0);
            pane->view()->setCurrentIndex(idx);
            pane->view()->scrollTo(idx, QAbstractItemView::EnsureVisible);
            break;
          }
        }
        updatePathSignal();
      }
    } else {
      // アーカイブ内の親へ。子ディレクトリ名で行を探す。
      const QString innerParent = ArchivePath::parentInnerPath(innerCurrent);
      const QString childName   = ArchivePath::innerBaseName(innerCurrent);
      const QString target      = ArchivePath::joinArchivePath(archiveAbs, innerParent);
      if (navigatePane(m_activePane, target)) {
        if (!childName.isEmpty()) {
          FileListModel* parentModel = pane->model();
          for (int i = 0; i < parentModel->rowCount(); ++i) {
            const FileItem* it = parentModel->itemAt(i);
            if (it && it->name() == childName) {
              const QModelIndex idx = parentModel->index(i, 0);
              pane->view()->setCurrentIndex(idx);
              pane->view()->scrollTo(idx, QAbstractItemView::EnsureVisible);
              break;
            }
          }
        }
        updatePathSignal();
      }
    }
    return;
  }

  // ── 通常 FS ──────────────────
  // 親に戻った際にカーソルを元いた子ディレクトリに合わせる
  const QString childDirName = QFileInfo(currentPath).fileName();

  QDir dir(currentPath);
  if (!dir.cdUp()) {
    // ルートディレクトリにいる場合は何もしない
    return;
  }

  QString parentPath = dir.absolutePath();
  if (navigatePane(m_activePane, parentPath)) {
    if (!childDirName.isEmpty()) {
      FileListModel* parentModel = pane->model();
      for (int i = 0; i < parentModel->rowCount(); ++i) {
        const FileItem* item = parentModel->itemAt(i);
        if (item && item->name() == childDirName) {
          pane->view()->setCurrentIndex(parentModel->index(i, 0));
          break;
        }
      }
    }
    updatePathSignal();
  }
}

void FileManagerPanel::handleSpaceKey() {
  FileListPane* pane = activePane();
  FileListModel* model = pane->model();

  QModelIndex currentIndex = pane->view()->currentIndex();
  if (!currentIndex.isValid()) {
    return;
  }

  int row = currentIndex.row();
  model->toggleSelected(row);

  // カーソルを次の行に移動
  int nextRow = row + 1;
  if (nextRow < model->rowCount()) {
    QModelIndex nextIndex = model->index(nextRow, 0);
    pane->view()->setCurrentIndex(nextIndex);
  }
}

void FileManagerPanel::toggleSelection() {
  FileListPane* pane = activePane();
  FileListModel* model = pane->model();

  QModelIndex currentIndex = pane->view()->currentIndex();
  if (!currentIndex.isValid()) {
    return;
  }

  int row = currentIndex.row();
  model->toggleSelected(row);

  // カーソルは移動しない（Space との違い）
}

void FileManagerPanel::handleAsteriskKey() {
  FileListModel* model = activePane()->model();
  // 選択を反転
  model->invertSelection();
}

void FileManagerPanel::handleSelectAllKey() {
  FileListModel* model = activePane()->model();
  // 全て選択されている場合は全解除、それ以外は全選択
  if (model->isAllSelected()) {
    model->setSelectedAll(false);
  } else {
    model->setSelectedAll(true);
  }
}

void FileManagerPanel::handleTabKey() {
  // アクティブペインを切り替え（2ペイン表示時のみ）。
  // Single / Preview では反対側にファイルリストが居ないので no-op。
  if (m_layoutMode != LayoutMode::Dual) {
    return;
  }
  if (m_activePane == PaneType::Left) {
    setActivePane(PaneType::Right);
  } else {
    setActivePane(PaneType::Left);
  }
}

void FileManagerPanel::togglePaneMode() {
  // 互換: 既存呼び出しは Single ↔ Dual の二値トグルのまま。
  // Preview 中だった場合は Dual に戻して終わり (Preview ↔ X はメニュー側で
  // 明示的に setLayoutMode() を呼ぶ運用)。
  if (m_layoutMode == LayoutMode::Preview) {
    setLayoutMode(LayoutMode::Dual);
  } else if (m_layoutMode == LayoutMode::Single) {
    setLayoutMode(LayoutMode::Dual);
  } else {
    setLayoutMode(LayoutMode::Single);
  }
}

void FileManagerPanel::setSinglePaneMode(bool single) {
  setLayoutMode(single ? LayoutMode::Single : LayoutMode::Dual);
}

void FileManagerPanel::updateFractionFromSizes() {
  if (!m_splitter) return;
  // 表示中の右スロット (Dual=右ペイン / Preview=プレビュー) を可視状態で判定する。
  // setLayoutMode の保存タイミングでは m_layoutMode は既に新モードだが表示は
  // まだ旧モードなので、m_layoutMode ではなく isVisible() で見るのが正しい。
  const QList<int> sizes = m_splitter->sizes();
  const int left = sizes.value(0);
  int right = 0;
  if (m_previewPane && m_previewPane->isVisible())      right = sizes.value(2);
  else if (m_rightPane && m_rightPane->isVisible())     right = sizes.value(1);
  else return;  // Single (片側のみ表示): 比率は更新しない
  const int visible = left + right;
  if (visible > 0) {
    m_leftPaneFraction = qBound(0.1, static_cast<double>(left) / visible, 0.9);
  }
}

void FileManagerPanel::applyFraction() {
  if (!m_splitter || m_layoutMode == LayoutMode::Single) return;
  const int total = m_splitter->width();
  if (total <= 0) return;  // 未レイアウト時は適用しない (Resize で再適用される)
  const int leftW = qRound(total * m_leftPaneFraction);
  QList<int> sizes;
  if (m_layoutMode == LayoutMode::Dual) {
    sizes = QList<int>() << leftW << (total - leftW) << 0;   // 左 | 右 | (preview)
  } else {  // Preview
    sizes = QList<int>() << leftW << 0 << (total - leftW);   // 左 | (right) | preview
  }
  m_splitter->setSizes(sizes);
}

void FileManagerPanel::scheduleApplyFraction() {
  if (m_applyFractionScheduled) return;
  m_applyFractionScheduled = true;
  QTimer::singleShot(0, this, [this]() {
    m_applyFractionScheduled = false;
    applyFraction();
  });
}

void FileManagerPanel::setLayoutMode(LayoutMode mode) {
  const LayoutMode prev = m_layoutMode;
  if (prev == mode) return;
  m_layoutMode = mode;

  const bool singleLike  = (mode != LayoutMode::Dual);  // Single または Preview
  const bool previewMode = (mode == LayoutMode::Preview);

  // Dual 以外に入る瞬間に Sync Browse は強制 OFF
  // (相方ペインが居ないので意味を成さない)。
  if (singleLike && m_syncBrowseEnabled) {
    setSyncBrowseEnabled(false);
  }
  // Preview に入る瞬間は Directory Compare も停止 (右ペインがプレビュー化する)。
  if (previewMode && m_compareMode) {
    stopDirectoryCompare();
  }

  // 分割比 (m_leftPaneFraction) は Dual / Preview で共有する。切替前に現在の
  // サイズから比率を覚えておき、切替後に同じ比率を適用することで、Preview ↔
  // Dual を行き来しても左右 (左 / プレビュー) の比率が維持される。
  if (m_splitter && prev != LayoutMode::Single) {
    updateFractionFromSizes();
  }

  // ── ウィジェットの可視性 ──
  // 右ペイン位置の物理スロットは「FileListPane (m_rightPane)」と
  // 「PreviewPane (m_previewPane)」の 2 種を排他で見せる。
  switch (mode) {
    case LayoutMode::Dual:
      m_leftPane->show();
      m_rightPane->show();
      if (m_previewPane) m_previewPane->hide();
      break;
    case LayoutMode::Single:
      if (m_activePane == PaneType::Left) {
        m_leftPane->show();
        m_rightPane->hide();
      } else {
        m_leftPane->hide();
        m_rightPane->show();
      }
      if (m_previewPane) m_previewPane->hide();
      break;
    case LayoutMode::Preview:
      // Preview は常に左にファイル一覧、右にプレビュー。アクティブペインは
      // 強制で Left に揃える。
      if (m_activePane != PaneType::Left) {
        setActivePane(PaneType::Left);
      }
      m_leftPane->show();
      m_rightPane->hide();
      if (m_previewPane) m_previewPane->show();
      break;
  }

  // 切替先のサイズを適用。保持している分割比 (m_leftPaneFraction) を現在の幅に
  // 当てはめる。これで PreviewPane が 0 幅のまま (特に Linux/X11) や全幅になる
  // 不具合を防ぎ、Dual ↔ Preview で比率も維持される。起動時の Preview 復元では
  // この時点でまだ幅 0 のことがあるが、その場合は applyFraction が何もせず、
  // スプリッタが最終サイズに広がった Resize (scheduleApplyFraction) で確実に
  // 適用される。
  applyFraction();

  // 列表示・size/mtime のフォーマットは「ペインが全幅かどうか」で決まる。
  // Single だけが全幅 (= 広い表示)。Dual / Preview はどちらも半幅 (= 狭い)。
  const bool fullWidth = (mode == LayoutMode::Single);
  if (m_leftPane && m_leftPane->model())
    m_leftPane->model()->setSinglePaneMode(fullWidth);
  if (m_rightPane && m_rightPane->model())
    m_rightPane->model()->setSinglePaneMode(fullWidth);
  if (m_leftPane)  m_leftPane->applyColumnVisibility(fullWidth);
  if (m_rightPane) m_rightPane->applyColumnVisibility(fullWidth);

  // Preview に入った瞬間 / 抜けた瞬間のプレビュー表示制御
  if (m_previewController) {
    if (previewMode) {
      // Preview に入った直後の左ペインカーソルに対して 1 回プレビューを発火。
      if (m_leftPane && m_leftPane->view() && m_leftPane->model()) {
        const QModelIndex idx = m_leftPane->view()->currentIndex();
        FileListModel* lmodel = m_leftPane->model();
        if (idx.isValid()) {
          if (const FileItem* item = lmodel->itemAt(idx)) {
            if (lmodel->isInArchiveMode() && item->isInArchive()
                && !item->isDir() && !item->isDotDot()) {
              const ArchiveEntry*   ae  = item->archiveEntry();
              const ArchiveContext* ctx = lmodel->archiveContext();
              if (ae && ctx) {
                m_previewController->requestArchivePreview(
                  ctx, ae->pathInArchive, item->absolutePath(),
                  ae->size);
              } else {
                m_previewController->clearPreview();
              }
            } else {
              m_previewController->requestPreview(
                item->absolutePath(),
                /*displayPath=*/ QString(),
                item->isDir(),
                item->isDotDot(),
                item->size());
            }
          } else {
            m_previewController->clearPreview();
          }
        } else {
          m_previewController->clearPreview();
        }
      }
    } else {
      m_previewController->clearPreview();
    }
  }

  // 互換シグナル: Single ⇔ それ以外 の bool 変化があった場合に emit。
  const bool prevSingleLike = (prev != LayoutMode::Dual);
  if (prevSingleLike != singleLike) {
    emit singlePaneModeChanged(singleLike);
  }
  emit layoutModeChanged(mode);
}

FileListPane* FileManagerPanel::activePane() const {
  return (m_activePane == PaneType::Left) ? m_leftPane : m_rightPane;
}

void FileManagerPanel::setActivePane(PaneType pane) {
  m_activePane = pane;

  // ペインのアクティブ状態を更新
  if (pane == PaneType::Left) {
    m_leftPane->setActive(true);
    m_rightPane->setActive(false);
  } else {
    m_leftPane->setActive(false);
    m_rightPane->setActive(true);
  }

  // アクティブペインにフォーカスを設定
  activePane()->view()->setFocus();

  // カーソル (currentIndex) が表示範囲外なら、見える位置までスクロール
  // する。ユーザーがマウスでスクロールしてカーソルを画面外に追いやった
  // まま反対ペインへ抜け、戻ってきたときに「カーソルが消えている」状態
  // にしないため。EnsureVisible で最小限の移動に留める。
  if (auto* view = activePane()->view()) {
    const QModelIndex idx = view->currentIndex();
    if (idx.isValid()) {
      view->scrollTo(idx, QAbstractItemView::EnsureVisible);
    }
  }

  // ステータスバーをアクティブペインの状態に追従
  emitActivePaneStatus();
}

void FileManagerPanel::emitActivePaneStatus() {
  FileListPane* pane = activePane();
  if (!pane) {
    emit activeFocusedPathChanged(QString());
    emit activeSummaryChanged(QString());
    return;
  }
  FileListModel* model = pane->model();
  if (!model) {
    emit activeFocusedPathChanged(QString());
    emit activeSummaryChanged(QString());
    return;
  }

  // フォーカス中ファイルの絶対パス
  QString focusedPath;
  const QModelIndex idx = pane->view()->currentIndex();
  if (idx.isValid()) {
    if (const FileItem* item = model->itemAt(idx)) {
      focusedPath = item->absolutePath();
    }
  }
  emit activeFocusedPathChanged(focusedPath);

  // 要約: 総アイテム数 ('..' 除外) / 選択数 / 選択合計サイズ
  int totalCount = 0;
  int selectedCount = 0;
  qint64 selectedBytes = 0;
  const int rows = model->rowCount();
  for (int r = 0; r < rows; ++r) {
    const FileItem* item = model->itemAt(r);
    if (!item || item->isDotDot()) continue;
    ++totalCount;
    if (item->isSelected()) {
      ++selectedCount;
      const qint64 sz = item->size();
      if (sz > 0) selectedBytes += sz;
    }
  }
  QString summary;
  if (selectedCount > 0) {
    summary = tr("%1 / %2 items selected (%3)")
                .arg(selectedCount)
                .arg(totalCount)
                .arg(QLocale(QLocale::English).formattedDataSize(selectedBytes));
  } else {
    summary = tr("%1 items").arg(totalCount);
  }
  emit activeSummaryChanged(summary);
}

void FileManagerPanel::onLeftFolderButtonClicked() {
  QString currentPath = m_leftPane->currentPath();
  QString selectedPath = QFileDialog::getExistingDirectory(
    this,
    tr("Select Folder"),
    currentPath,
    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
  );

  if (!selectedPath.isEmpty() && selectedPath != currentPath) {
    if (navigatePane(PaneType::Left, selectedPath)) {
      updatePathSignal();
    }
  }
}

void FileManagerPanel::onRightFolderButtonClicked() {
  QString currentPath = m_rightPane->currentPath();
  QString selectedPath = QFileDialog::getExistingDirectory(
    this,
    tr("Select Folder"),
    currentPath,
    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
  );

  if (!selectedPath.isEmpty() && selectedPath != currentPath) {
    if (navigatePane(PaneType::Right, selectedPath)) {
      updatePathSignal();
    }
  }
}

QString FileManagerPanel::currentPath() const {
  return activePane()->currentPath();
}

QString FileManagerPanel::leftPath() const {
  return m_leftPane->currentPath();
}

QString FileManagerPanel::rightPath() const {
  return m_rightPane->currentPath();
}

void FileManagerPanel::updatePathSignal() {
  emit pathChanged(m_leftPane->currentPath(), m_rightPane->currentPath());
}

void FileManagerPanel::copySelectedFiles() {
  FileListPane* srcPane = activePane();
  FileListPane* destPane = (m_activePane == PaneType::Left) ? m_rightPane : m_leftPane;

  FileListModel* srcModel  = srcPane->model();
  FileListModel* destModel = destPane->model();
  // 既定の出力先:
  //   - 2 ペイン時: 相手ペインのカレント
  //   - 1 ペイン時 (通常): アクティブペインのカレント
  //   - 1 ペイン時 (アーカイブモード): アーカイブが置かれている実 FS の
  //     親ディレクトリ (currentPath() は archive.zip!/inner 形式なので
  //     そのままだと QFile 系操作の宛先には使えない)
  QString destDir;
  if (isDualPaneMode()) {
    destDir = destPane->currentPath();
  } else if (srcModel->isInArchiveMode()) {
    destDir = QFileInfo(srcModel->archivePath()).absolutePath();
  } else {
    destDir = srcPane->currentPath();
  }

  // ── アーカイブ書込ガード ──────────────────
  // 反対パネルがアーカイブモードのときは「アーカイブ内に書き込み」になるので拒否。
  // ただし 1 ペイン時は相手ペインが hide() されているだけで、過去のアーカイブ
  // 表示状態を残しているだけのことがある。1 ペイン時の出力先は srcPane なので
  // 相手ペインの状態は無関係 → ガードをスキップする。
  if (isDualPaneMode() && destModel->isInArchiveMode()) {
    warn(this, tr("Read-only archive"),
      tr("Cannot copy into a read-only archive."));
    return;
  }

  // ── アーカイブからの抽出コピー ──────────────────
  // srcPane がアーカイブモードのときは、選択エントリだけを反対パネルに展開する。
  if (srcModel->isInArchiveMode()) {
    // 選択エントリを集める (なければカーソル行 1 件、".." 除く)。
    // アーカイブモードでは selectedFilePaths() が "<archive>!/..." 形式の絶対パスを
    // 返してくるので、ここでは ArchiveEntry 経由で pathInArchive を集める。
    QStringList selFiles;
    QStringList selDirs;
    auto addItem = [&](const FileItem* it) {
      if (!it || it->isDotDot()) return;
      const ArchiveEntry* ae = it->archiveEntry();
      if (!ae) return;
      if (ae->isDir) selDirs.append(ae->pathInArchive);
      else           selFiles.append(ae->pathInArchive);
    };
    bool anySelected = false;
    for (const FileItem* it : srcModel->selectedItems()) {
      anySelected = true;
      addItem(it);
    }
    if (!anySelected) {
      const QModelIndex cur = srcPane->view()->currentIndex();
      if (cur.isValid()) addItem(srcModel->itemAt(cur));
    }
    if (selFiles.isEmpty() && selDirs.isEmpty()) return;

    const QString archiveAbs   = srcModel->archivePath();
    // カレントアーカイブ内ディレクトリを worker に渡す (先頭 '/' は付かない形式)。
    QString innerCurrent = srcModel->archiveInnerPath();
    while (innerCurrent.startsWith(QLatin1Char('/'))) innerCurrent.remove(0, 1);
    while (innerCurrent.endsWith(QLatin1Char('/')))   innerCurrent.chop(1);

    // 進捗ダイアログの「N / M files」表示用に、展開対象エントリ数を事前計算。
    // ArchiveContext のメモリ上 entries を走査するだけなので軽量。
    // 判定ロジックは worker と一致させる: ファイル単独 / ディレクトリ単独 /
    // ディレクトリ prefix 配下のいずれかにマッチすればカウント。
    const QSet<QString> fileSet(selFiles.cbegin(), selFiles.cend());
    const QSet<QString> dirSet(selDirs.cbegin(),  selDirs.cend());
    QStringList dirPrefixes;
    for (const QString& d : selDirs) dirPrefixes.append(d + QLatin1Char('/'));
    int filesTotal = 0;
    if (const ArchiveContext* ctx = srcModel->archiveContext()) {
      for (auto it = ctx->entries.cbegin(); it != ctx->entries.cend(); ++it) {
        const QString& p = it.key();
        bool match = fileSet.contains(p) || dirSet.contains(p);
        if (!match) {
          for (const QString& pre : dirPrefixes) {
            if (p.startsWith(pre)) { match = true; break; }
          }
        }
        if (match) ++filesTotal;
      }
    }

    const int totalCount = selFiles.size() + selDirs.size();

    // 暗号化アーカイブの場合は ctx->password を worker に渡して libarchive で
    // 復号させる。通常アーカイブでは空文字列 = no-op。
    const QString archivePassword = srcModel->archiveContext()
                                      ? srcModel->archiveContext()->password
                                      : QString();
    auto* worker = new ArchiveExtractEntriesWorker(
      archiveAbs, selFiles, selDirs, innerCurrent, destDir, filesTotal,
      archivePassword, this);
    ProgressDialog* dialog = new ProgressDialog(tr("Extracting files..."), this);
    dialog->setWorker(worker);

    connect(worker, &WorkerBase::finished, this,
      [this, srcPane, destPane, archiveAbs, totalCount](bool ok) {
        Logger::instance().log(ok ? Logger::Info : Logger::Error,
          QStringLiteral("Archive copy %1: %2 entry/entries from %3")
            .arg(ok ? QStringLiteral("done") : QStringLiteral("failed"))
            .arg(totalCount)
            .arg(archiveAbs));
        // 反対パネル (展開先) だけ refresh。srcPane はアーカイブのままなので
        // 内容は変わらない。
        destPane->setPath(destPane->currentPath());
        srcPane->model()->setSelectedAll(false);
        activePane()->view()->setFocus();
      });

    worker->start();
    dialog->exec();
    return;
  }

  // Get selected files, or current item if no selection
  QStringList selectedFiles = srcModel->selectedFilePaths();
  if (selectedFiles.isEmpty()) {
    QModelIndex currentIndex = srcPane->view()->currentIndex();
    if (currentIndex.isValid()) {
      const FileItem* item = srcModel->itemAt(currentIndex);
      if (item && !item->isDotDot()) {
        selectedFiles.append(item->absolutePath());
      }
    }
  }

  if (selectedFiles.isEmpty()) {
    return;
  }

  // 確認ダイアログ（コピー元・一覧・コピー先・上書きモード）
  TransferConfirmDialog confirm(
    TransferConfirmDialog::Copy,
    srcPane->currentPath(),
    selectedFiles,
    destDir,
    this
  );
  if (confirm.exec() != QDialog::Accepted) {
    return;
  }
  const OverwriteMode overwriteMode = confirm.overwriteMode();
  // ユーザーがダイアログ上で編集した宛先を使う。空なら反対側ペインに戻す。
  const QString actualDest = confirm.destinationDir().isEmpty()
                               ? destDir
                               : confirm.destinationDir();

  // Create worker and dialog
  CopyWorker* worker = new CopyWorker(selectedFiles, actualDest, this);
  worker->setOverwriteMode(overwriteMode);
  worker->setAutoRenameTemplate(confirm.autoRenameTemplate());
  connect(worker, &WorkerBase::overwriteRequired, this,
    [this](const QString& srcPath, const QString& dstPath, OverwriteDecision* decision) {
      OverwriteDialog dlg(srcPath, dstPath, this);
      dlg.exec();
      *decision = dlg.decision();
    },
    Qt::BlockingQueuedConnection
  );
  ProgressDialog* dialog = new ProgressDialog(tr("Copying files..."), this);
  dialog->setWorker(worker);

  const int copiedCount = selectedFiles.size();
  const QString copiedDest = actualDest;

  connect(worker, &WorkerBase::finished, this,
      [this, dialog, srcPane, destPane, copiedCount, copiedDest](bool success) {
    // 完了時のダイアログ開閉は ProgressDialog 側 (auto-close チェックの状態で判断) に任せる
    Logger::instance().log(success ? Logger::Info : Logger::Error,
      QStringLiteral("Copy %1: %2 item(s) → %3")
        .arg(success ? QStringLiteral("done") : QStringLiteral("failed"))
        .arg(copiedCount)
        .arg(copiedDest));

    // Save cursor positions
    QModelIndex srcCurrentIndex = srcPane->view()->currentIndex();
    QModelIndex destCurrentIndex = destPane->view()->currentIndex();
    QString srcCurrentFile;
    QString destCurrentFile;
    int srcCurrentRow = srcCurrentIndex.row();
    int destCurrentRow = destCurrentIndex.row();

    if (srcCurrentIndex.isValid()) {
      const FileItem* item = srcPane->model()->itemAt(srcCurrentIndex);
      if (item) srcCurrentFile = item->name();
    }
    if (destCurrentIndex.isValid()) {
      const FileItem* item = destPane->model()->itemAt(destCurrentIndex);
      if (item) destCurrentFile = item->name();
    }

    // Refresh both panes
    QString srcPath = srcPane->currentPath();
    QString destPath = destPane->currentPath();
    srcPane->setPath(srcPath);
    destPane->setPath(destPath);

    // Restore cursor positions
    auto restoreCursor = [](FileListPane* pane, const QString& fileName, int fallbackRow) {
      FileListModel* model = pane->model();

      // Try to find the file by name
      if (!fileName.isEmpty()) {
        for (int i = 0; i < model->rowCount(); ++i) {
          const FileItem* item = model->itemAt(i);
          if (item && item->name() == fileName) {
            pane->view()->setCurrentIndex(model->index(i, 0));
            return;
          }
        }
      }

      // Fallback to row number (clamped to valid range)
      if (model->rowCount() > 0) {
        int row = qMin(fallbackRow, model->rowCount() - 1);
        pane->view()->setCurrentIndex(model->index(row, 0));
      }
    };

    restoreCursor(srcPane, srcCurrentFile, srcCurrentRow);
    restoreCursor(destPane, destCurrentFile, destCurrentRow);

    // Clear selections
    srcPane->model()->setSelectedAll(false);

    // Restore focus to active pane
    activePane()->view()->setFocus();

    // 比較モード中ならファイル操作後に再比較して overlay を最新化する。
    // (setPath で overlay 自体は保持されるが、内容は古いままなので。)
    recompareIfActive();
  });

  worker->start();
  dialog->exec();
}

void FileManagerPanel::moveSelectedFiles() {
  FileListPane* srcPane = activePane();
  FileListPane* destPane = (m_activePane == PaneType::Left) ? m_rightPane : m_leftPane;

  FileListModel* srcModel = srcPane->model();
  // アーカイブモードでは「移動」は両側成立しない (アーカイブが read-only)。
  // ただし 1 ペイン時の destPane は hide() されているだけで実際の出力先は
  // srcPane なので、destPane のアーカイブ状態は無関係 → ガードをスキップ。
  if (srcModel->isInArchiveMode()
      || (isDualPaneMode() && destPane->model()->isInArchiveMode())) {
    warn(this, tr("Read-only archive"),
      tr("Cannot move files to or from a read-only archive."));
    return;
  }
  // 1 ペイン時はアクティブペインを既定出力先に (相手ペインは見えないため)。
  // 1 ペイン + アーカイブモードの場合は仮想パス (archive.zip!/inner) を
  // そのまま実 FS の宛先には使えないので、アーカイブが置かれている実
  // ディレクトリにフォールバックする。
  QString destDir;
  if (isDualPaneMode()) {
    destDir = destPane->currentPath();
  } else if (srcModel->isInArchiveMode()) {
    destDir = QFileInfo(srcModel->archivePath()).absolutePath();
  } else {
    destDir = srcPane->currentPath();
  }

  // Get selected files, or current item if no selection
  QStringList selectedFiles = srcModel->selectedFilePaths();
  if (selectedFiles.isEmpty()) {
    QModelIndex currentIndex = srcPane->view()->currentIndex();
    if (currentIndex.isValid()) {
      const FileItem* item = srcModel->itemAt(currentIndex);
      if (item && !item->isDotDot()) {
        selectedFiles.append(item->absolutePath());
      }
    }
  }

  if (selectedFiles.isEmpty()) {
    return;
  }

  // 確認ダイアログ
  TransferConfirmDialog confirm(
    TransferConfirmDialog::Move,
    srcPane->currentPath(),
    selectedFiles,
    destDir,
    this
  );
  if (confirm.exec() != QDialog::Accepted) {
    return;
  }
  const OverwriteMode overwriteMode = confirm.overwriteMode();
  // ユーザーがダイアログ上で編集した宛先を使う。空なら反対側ペインに戻す。
  const QString actualDest = confirm.destinationDir().isEmpty()
                               ? destDir
                               : confirm.destinationDir();

  // Create worker and dialog
  MoveWorker* worker = new MoveWorker(selectedFiles, actualDest, this);
  worker->setOverwriteMode(overwriteMode);
  worker->setAutoRenameTemplate(confirm.autoRenameTemplate());
  connect(worker, &WorkerBase::overwriteRequired, this,
    [this](const QString& srcPath, const QString& dstPath, OverwriteDecision* decision) {
      OverwriteDialog dlg(srcPath, dstPath, this);
      dlg.exec();
      *decision = dlg.decision();
    },
    Qt::BlockingQueuedConnection
  );
  ProgressDialog* dialog = new ProgressDialog(tr("Moving files..."), this);
  dialog->setWorker(worker);

  const int movedCount = selectedFiles.size();
  const QString movedDest = actualDest;

  connect(worker, &WorkerBase::finished, this,
      [this, dialog, srcPane, destPane, movedCount, movedDest](bool success) {
    // 完了時のダイアログ開閉は ProgressDialog 側 (auto-close チェックの状態で判断) に任せる
    Logger::instance().log(success ? Logger::Info : Logger::Error,
      QStringLiteral("Move %1: %2 item(s) → %3")
        .arg(success ? QStringLiteral("done") : QStringLiteral("failed"))
        .arg(movedCount)
        .arg(movedDest));

    // Save cursor positions
    QModelIndex srcCurrentIndex = srcPane->view()->currentIndex();
    QModelIndex destCurrentIndex = destPane->view()->currentIndex();
    QString srcCurrentFile;
    QString destCurrentFile;
    int srcCurrentRow = srcCurrentIndex.row();
    int destCurrentRow = destCurrentIndex.row();

    if (srcCurrentIndex.isValid()) {
      const FileItem* item = srcPane->model()->itemAt(srcCurrentIndex);
      if (item) srcCurrentFile = item->name();
    }
    if (destCurrentIndex.isValid()) {
      const FileItem* item = destPane->model()->itemAt(destCurrentIndex);
      if (item) destCurrentFile = item->name();
    }

    // Refresh both panes
    QString srcPath = srcPane->currentPath();
    QString destPath = destPane->currentPath();
    srcPane->setPath(srcPath);
    destPane->setPath(destPath);

    // Restore cursor positions
    auto restoreCursor = [](FileListPane* pane, const QString& fileName, int fallbackRow) {
      FileListModel* model = pane->model();

      // Try to find the file by name
      if (!fileName.isEmpty()) {
        for (int i = 0; i < model->rowCount(); ++i) {
          const FileItem* item = model->itemAt(i);
          if (item && item->name() == fileName) {
            pane->view()->setCurrentIndex(model->index(i, 0));
            return;
          }
        }
      }

      // Fallback to row number (clamped to valid range)
      if (model->rowCount() > 0) {
        int row = qMin(fallbackRow, model->rowCount() - 1);
        pane->view()->setCurrentIndex(model->index(row, 0));
      }
    };

    restoreCursor(srcPane, srcCurrentFile, srcCurrentRow);
    restoreCursor(destPane, destCurrentFile, destCurrentRow);

    // Clear selections
    srcPane->model()->setSelectedAll(false);

    // Restore focus to active pane
    activePane()->view()->setFocus();

    // 比較モード中ならファイル操作後に再比較して overlay を最新化する。
    // (setPath で overlay 自体は保持されるが、内容は古いままなので。)
    recompareIfActive();
  });

  worker->start();
  dialog->exec();
}

void FileManagerPanel::deleteSelectedFiles() {
  FileListPane* srcPane = activePane();
  FileListModel* srcModel = srcPane->model();

  if (srcModel->isInArchiveMode()) {
    warn(this, tr("Read-only archive"),
      tr("Cannot delete entries inside a read-only archive."));
    return;
  }

  // Get selected files, or current item if no selection
  QStringList selectedFiles = srcModel->selectedFilePaths();
  if (selectedFiles.isEmpty()) {
    QModelIndex currentIndex = srcPane->view()->currentIndex();
    if (currentIndex.isValid()) {
      const FileItem* item = srcModel->itemAt(currentIndex);
      if (item && !item->isDotDot()) {
        selectedFiles.append(item->absolutePath());
      }
    }
  }

  if (selectedFiles.isEmpty()) {
    return;
  }

  // Ask for confirmation
  QString message;
  QString detailTooltip;
  if (selectedFiles.size() == 1) {
    QFileInfo info(selectedFiles[0]);
    const QString name = info.fileName();
    // 極端に長いファイル名は中央省略して 1〜2 行に収め、全文はツールチップへ回す
    // (スペースの無い名前が文字の途中で折り返されて読みにくくなるのを避ける)。
    QString shown = name;
    const int kMaxChars = 72;
    if (name.size() > kMaxChars) {
      const int head = (kMaxChars - 1) / 2;
      const int tail = (kMaxChars - 1) - head;
      shown = name.left(head) + QChar(0x2026) + name.right(tail);  // … で中央省略
      detailTooltip = name;
    }
    message = tr("Are you sure you want to delete '%1'?").arg(shown);
  } else {
    message = tr("Are you sure you want to delete %1 items?").arg(selectedFiles.size());
  }

  // ネットワーク / リムーバブルドライブでは moveToTrash がゴミ箱を作れず常に失敗する
  // ため、対象ボリュームのファイルシステム種別から事前に判定し、ゴミ箱を使えないなら
  // 確認ダイアログ側でゴミ箱オプションを無効化する。代表として先頭ファイルで判定する。
  bool trashAvailable = true;
  {
    const QStorageInfo vol(QFileInfo(selectedFiles.first()).absolutePath());
    const QString fsType = QString::fromLatin1(vol.fileSystemType()).toLower();
    static const char* const kNetworkFs[] = {
      "smb", "afp", "nfs", "cifs", "webdav", "ftp", "sshfs", "fuse"
    };
    for (const char* t : kNetworkFs) {
      if (fsType.contains(QLatin1String(t))) { trashAvailable = false; break; }
    }
  }

  DeleteConfirmDialog confirmDlg(
    message,
    Settings::instance().defaultDeleteToTrash() && trashAvailable,
    this, detailTooltip, trashAvailable);
  if (confirmDlg.exec() != QDialog::Accepted) {
    return;
  }
  const bool toTrash = confirmDlg.toTrash();

  // Create worker and dialog
  RemoveWorker* worker = new RemoveWorker(selectedFiles, toTrash, this);
  ProgressDialog* dialog = new ProgressDialog(tr("Deleting files..."), this);
  dialog->setWorker(worker);

  const int delCount = selectedFiles.size();
  const bool delToTrash = toTrash;

  connect(worker, &WorkerBase::finished, this,
      [this, dialog, srcPane, delCount, delToTrash](bool success) {
    // 完了時のダイアログ開閉は ProgressDialog 側 (auto-close チェックの状態で判断) に任せる
    Logger::instance().log(success ? Logger::Info : Logger::Error,
      QStringLiteral("%1 %2: %3 item(s)")
        .arg(delToTrash ? QStringLiteral("Trash") : QStringLiteral("Delete"))
        .arg(success ? QStringLiteral("done") : QStringLiteral("failed"))
        .arg(delCount));

    // Save cursor position
    QModelIndex srcCurrentIndex = srcPane->view()->currentIndex();
    QString srcCurrentFile;
    int srcCurrentRow = srcCurrentIndex.row();

    if (srcCurrentIndex.isValid()) {
      const FileItem* item = srcPane->model()->itemAt(srcCurrentIndex);
      if (item) srcCurrentFile = item->name();
    }

    // Refresh the pane
    QString srcPath = srcPane->currentPath();
    srcPane->setPath(srcPath);

    // Restore cursor position
    auto restoreCursor = [](FileListPane* pane, const QString& fileName, int fallbackRow) {
      FileListModel* model = pane->model();

      // Try to find the file by name
      if (!fileName.isEmpty()) {
        for (int i = 0; i < model->rowCount(); ++i) {
          const FileItem* item = model->itemAt(i);
          if (item && item->name() == fileName) {
            pane->view()->setCurrentIndex(model->index(i, 0));
            return;
          }
        }
      }

      // Fallback to row number (clamped to valid range)
      if (model->rowCount() > 0) {
        int row = qMin(fallbackRow, model->rowCount() - 1);
        pane->view()->setCurrentIndex(model->index(row, 0));
      }
    };

    restoreCursor(srcPane, srcCurrentFile, srcCurrentRow);

    // Clear selections
    srcPane->model()->setSelectedAll(false);

    // Restore focus to active pane
    activePane()->view()->setFocus();

    // 比較モード中ならファイル操作後に再比較して overlay を最新化する。
    // (setPath で overlay 自体は保持されるが、内容は古いままなので。)
    recompareIfActive();
  });

  worker->start();
  dialog->exec();
}

void FileManagerPanel::createDirectory() {
  FileListPane* srcPane = activePane();
  if (srcPane->model()->isInArchiveMode()) {
    warn(this, tr("Read-only archive"),
      tr("Cannot create a directory inside a read-only archive."));
    return;
  }
  QString currentPath = srcPane->currentPath();

  // Ask for directory name
  bool ok = false;
  QString dirName = inputText(
    this,
    tr("Create Directory"),
    tr("Enter directory name:"),
    QString(),
    &ok
  );

  if (!ok || dirName.isEmpty()) {
    return;
  }

  // Create directory
  QString newDirPath = currentPath + "/" + dirName;
  QDir dir;
  if (!dir.mkpath(newDirPath)) {
    Logger::instance().error(QStringLiteral("Mkdir failed: %1").arg(newDirPath));
    critical(
      this,
      tr("Error"),
      tr("Failed to create directory: %1").arg(dirName)
    );
    return;
  }
  Logger::instance().info(QStringLiteral("Mkdir: %1").arg(newDirPath));

  // Refresh and move cursor to new directory
  srcPane->setPath(currentPath);

  // Find and select the new directory
  FileListModel* model = srcPane->model();
  for (int i = 0; i < model->rowCount(); ++i) {
    const FileItem* item = model->itemAt(i);
    if (item && item->name() == dirName) {
      srcPane->view()->setCurrentIndex(model->index(i, 0));
      break;
    }
  }

  srcPane->view()->setFocus();
}

void FileManagerPanel::createFile() {
  FileListPane* srcPane = activePane();
  if (srcPane->model()->isInArchiveMode()) {
    warn(this, tr("Read-only archive"),
      tr("Cannot create a new file inside a read-only archive."));
    return;
  }
  QString currentPath = srcPane->currentPath();

  bool ok = false;
  QString fileName = inputText(
    this,
    tr("Create File"),
    tr("Enter file name:"),
    QString(),
    &ok
  );

  if (!ok || fileName.isEmpty()) {
    return;
  }

  const QString newFilePath = currentPath + "/" + fileName;
  if (QFileInfo::exists(newFilePath)) {
    critical(
      this, tr("Error"),
      tr("A file or directory with the name '%1' already exists.").arg(fileName)
    );
    return;
  }

  // 空ファイルを作成
  QFile file(newFilePath);
  if (!file.open(QIODevice::WriteOnly)) {
    Logger::instance().error(QStringLiteral("Create file failed: %1").arg(newFilePath));
    critical(
      this, tr("Error"),
      tr("Failed to create file: %1").arg(fileName)
    );
    return;
  }
  file.close();
  Logger::instance().info(QStringLiteral("Created file: %1").arg(newFilePath));

  // リフレッシュして新規ファイルにカーソルを移動
  srcPane->setPath(currentPath);
  FileListModel* model = srcPane->model();
  for (int i = 0; i < model->rowCount(); ++i) {
    const FileItem* item = model->itemAt(i);
    if (item && item->name() == fileName) {
      srcPane->view()->setCurrentIndex(model->index(i, 0));
      break;
    }
  }

  srcPane->view()->setFocus();
}

void FileManagerPanel::changeAttributes() {
  FileListPane* srcPane = activePane();
  FileListModel* model = srcPane->model();

  if (model->isInArchiveMode()) {
    warn(this, tr("Read-only archive"),
      tr("Cannot change attributes of entries inside a read-only archive."));
    return;
  }

  // 選択 or カレントのファイル/ディレクトリのパスを収集
  QStringList paths = model->selectedFilePaths();
  if (paths.isEmpty()) {
    QModelIndex currentIndex = srcPane->view()->currentIndex();
    if (currentIndex.isValid()) {
      const FileItem* item = model->itemAt(currentIndex);
      if (item && !item->isDotDot()) {
        paths.append(item->absolutePath());
      }
    }
  }
  if (paths.isEmpty()) return;

  PropertiesDialog dlg(paths, this);
  if (dlg.exec() == QDialog::Accepted) {
    // カーソルが乗っていたファイル名を控えてからリフレッシュし、同名の行を再選択
    QString currentName;
    const QModelIndex currentIdx = srcPane->view()->currentIndex();
    if (currentIdx.isValid()) {
      const FileItem* item = model->itemAt(currentIdx);
      if (item) currentName = item->name();
    }
    model->refresh();
    if (!currentName.isEmpty()) {
      for (int i = 0; i < model->rowCount(); ++i) {
        const FileItem* item = model->itemAt(i);
        if (item && item->name() == currentName) {
          srcPane->view()->setCurrentIndex(model->index(i, 0));
          break;
        }
      }
    }
    srcPane->view()->setFocus();
  }
}

void FileManagerPanel::createArchive() {
  FileListPane* srcPane  = activePane();
  FileListPane* destPane = (m_activePane == PaneType::Left) ? m_rightPane : m_leftPane;
  FileListModel* srcModel = srcPane->model();

  // アーカイブモード中はソースが仮想 FS の中なので圧縮対象を選べない。
  // 反対パネル側がアーカイブモードなら書き込み先として無効。
  if (srcModel->isInArchiveMode()) {
    warn(this, tr("Read-only archive"),
      tr("Cannot create an archive from entries inside another archive."));
    return;
  }
  if (isDualPaneMode() && destPane->model()->isInArchiveMode()) {
    warn(this, tr("Read-only archive"),
      tr("Cannot write a new archive into a read-only archive."));
    return;
  }

  // 選択ファイル or カレント
  QStringList paths = srcModel->selectedFilePaths();
  if (paths.isEmpty()) {
    QModelIndex currentIndex = srcPane->view()->currentIndex();
    if (currentIndex.isValid()) {
      const FileItem* item = srcModel->itemAt(currentIndex);
      if (item && !item->isDotDot()) paths.append(item->absolutePath());
    }
  }
  if (paths.isEmpty()) return;

  // 既定の出力先は「相手ペイン」のカレント (Copy/Move と同じ UX)。
  // ダイアログ内で ↑/↓ により「自分ペイン (srcPane)」とトグルできる。
  // 1 ペイン / プレビュー時はアクティブペインを既定出力先に
  // (相手ペインがファイルリストではないため)。
  const QString defaultOutputDir = isDualPaneMode()
                                 ? destPane->currentPath()
                                 : srcPane->currentPath();
  CreateArchiveDialog dlg(paths,
                          defaultOutputDir,
                          srcPane->currentPath(),
                          this);
  if (dlg.exec() != QDialog::Accepted) return;

  QString     outputPath = dlg.outputPath();
  const auto  format     = dlg.format();

  if (outputPath.isEmpty()) return;

  // 既存ファイルがある場合はリネーム入力を求める。キャンセル（空 or reject）で中止。
  while (QFileInfo::exists(outputPath)) {
    const QString currentName = QFileInfo(outputPath).fileName();
    bool ok = false;
    const QString newName = inputText(
      this, tr("File Exists"),
      tr("'%1' already exists. Enter a different name:").arg(currentName),
      currentName, &ok);
    if (!ok || newName.trimmed().isEmpty()) return;
    outputPath = QDir(QFileInfo(outputPath).absolutePath())
                   .absoluteFilePath(newName.trimmed());
  }

  ArchiveCreateWorker* worker = new ArchiveCreateWorker(
    outputPath, format, paths, this);
  // 作成オプション (圧縮レベル / zip 暗号化) をダイアログから引き継ぐ。
  worker->setCompressionLevel(dlg.compressionLevel());
  worker->setEncryption(dlg.encryption());
  worker->setPassphrase(dlg.passphrase());
  ProgressDialog* dialog = new ProgressDialog(tr("Creating archive..."), this);
  dialog->setWorker(worker);

  connect(worker, &WorkerBase::finished, this,
    [this, dialog, srcPane, destPane, outputPath](bool ok) {
    // 完了時のダイアログ開閉は ProgressDialog 側 (auto-close チェックの状態で判断) に任せる
    Logger::instance().log(ok ? Logger::Info : Logger::Error,
      QStringLiteral("Archive %1: %2")
        .arg(ok ? QStringLiteral("created") : QStringLiteral("create failed"))
        .arg(outputPath));
    // 出力先が src/dest どちらかのペインと一致していれば refresh してカーソル移動
    const QString outputDir = QFileInfo(outputPath).absolutePath();
    const QString fileName  = QFileInfo(outputPath).fileName();
    auto refreshIfMatches = [&](FileListPane* pane) {
      if (pane->currentPath() != outputDir) return;
      pane->setPath(outputDir);
      FileListModel* model = pane->model();
      for (int i = 0; i < model->rowCount(); ++i) {
        const FileItem* item = model->itemAt(i);
        if (item && item->name() == fileName) {
          pane->view()->setCurrentIndex(model->index(i, 0));
          break;
        }
      }
    };
    refreshIfMatches(srcPane);
    refreshIfMatches(destPane);
    activePane()->view()->setFocus();
  });
  worker->start();
  dialog->exec();
}

void FileManagerPanel::extractArchive() {
  FileListPane* srcPane  = activePane();
  FileListPane* destPane = (m_activePane == PaneType::Left) ? m_rightPane : m_leftPane;
  FileListModel* srcModel = srcPane->model();

  // アーカイブの中の "アーカイブ" を `u` で展開しようとしているケース。
  // 仮想エントリを直接 libarchive に渡せないので拒否する。
  // (アーカイブ内のサブアーカイブを取り出したい場合は、まず c で
  //  反対パネルに展開してからそちらで u する)。
  if (srcModel->isInArchiveMode()) {
    warn(this, tr("Read-only archive"),
      tr("Cannot extract an archive that lives inside another archive."));
    return;
  }
  if (isDualPaneMode() && destPane->model()->isInArchiveMode()) {
    warn(this, tr("Read-only archive"),
      tr("Cannot extract into a read-only archive."));
    return;
  }

  QModelIndex currentIndex = srcPane->view()->currentIndex();
  if (!currentIndex.isValid()) return;
  const FileItem* item = srcModel->itemAt(currentIndex);
  if (!item || item->isDir() || item->isDotDot()) {
    inform(
      this, tr("Extract Archive"),
      tr("Select an archive file to extract."));
    return;
  }

  const QString archivePath = item->absolutePath();
  // 展開先の既定は「相手ペイン」(Copy/Move と同じ UX)。↑/↓ でアーカイブが
  // 置かれているディレクトリ (= srcPane のカレント) にトグルできる。
  // 1 ペイン / プレビュー時はアクティブペインを既定展開先に
  // (相手ペインがファイルリストではないため)。
  const QString defaultOutputDir = isDualPaneMode()
                                 ? destPane->currentPath()
                                 : srcPane->currentPath();
  ExtractArchiveDialog dlg(archivePath,
                           defaultOutputDir,
                           srcPane->currentPath(),
                           this);
  if (dlg.exec() != QDialog::Accepted) return;

  const QString outputDir = dlg.outputDirectory();
  if (outputDir.isEmpty()) return;

  // アーカイブ名から拡張子を剥がしてベース名を作る
  QString baseName = QFileInfo(archivePath).fileName();
  static const QStringList kKnownExts = {
    QStringLiteral(".tar.gz"),  QStringLiteral(".tar.bz2"),
    QStringLiteral(".tar.xz"),  QStringLiteral(".tgz"),
    QStringLiteral(".tbz2"),    QStringLiteral(".txz"),
    QStringLiteral(".tar"),     QStringLiteral(".zip"),
  };
  for (const QString& e : kKnownExts) {
    if (baseName.endsWith(e, Qt::CaseInsensitive)) {
      baseName.chop(e.size());
      break;
    }
  }
  if (baseName.isEmpty()) baseName = QStringLiteral("extracted");

  // サブディレクトリを確定。既存ならリネーム入力、キャンセルで中止。
  QString targetDir = QDir(outputDir).absoluteFilePath(baseName);
  while (QFileInfo::exists(targetDir)) {
    bool ok = false;
    const QString newName = inputText(
      this, tr("Directory Exists"),
      tr("'%1' already exists. Enter a different name:").arg(baseName),
      baseName, &ok);
    if (!ok || newName.trimmed().isEmpty()) return;
    baseName  = newName.trimmed();
    targetDir = QDir(outputDir).absoluteFilePath(baseName);
  }

  // 暗号化アーカイブの場合はパスワードを入力させて検証する。
  // verifyPassword(_, "") は「暗号化なし、もしくは空パスワードで復号できる」
  // ときに true を返すので、false なら password 入力が必要。
  // 個別エントリ展開 (FileListModel::setPath 側) と同じ 3 回リトライ仕様。
  QString password;
  if (!ArchiveContext::verifyPassword(archivePath, QString())) {
    const QString archiveName = QFileInfo(archivePath).fileName();
    QString prompt = tr("Enter password for %1:").arg(archiveName);
    for (int attempt = 0; attempt < 3; ++attempt) {
      bool ok = false;
      const QString pw = QInputDialog::getText(this,
        tr("Password Required"),
        prompt,
        QLineEdit::Password,
        QString(),
        &ok);
      if (!ok) return;
      if (ArchiveContext::verifyPassword(archivePath, pw)) {
        password = pw;
        break;
      }
      prompt = tr("Wrong password. Enter password for %1:").arg(archiveName);
      if (attempt == 2) {
        warn(this,
          tr("Cannot Extract Archive"),
          tr("Wrong password (3 attempts). Giving up."));
        return;
      }
    }
  }

  ArchiveExtractWorker* worker = new ArchiveExtractWorker(
    archivePath, targetDir, password, this);
  ProgressDialog* dialog = new ProgressDialog(tr("Extracting archive..."), this);
  dialog->setWorker(worker);

  connect(worker, &WorkerBase::finished, this,
    [this, dialog, srcPane, destPane, outputDir, baseName, archivePath](bool ok) {
    Logger::instance().log(ok ? Logger::Info : Logger::Error,
      QStringLiteral("Archive %1: %2")
        .arg(ok ? QStringLiteral("extracted") : QStringLiteral("extract failed"))
        .arg(archivePath));
    // 完了時のダイアログ開閉は ProgressDialog 側 (auto-close チェックの状態で判断) に任せる
    // 展開したサブディレクトリを含む親ディレクトリと一致するペインを refresh し、
    // サブディレクトリ名にカーソルを合わせる
    auto refreshIfMatches = [&](FileListPane* pane) {
      if (pane->currentPath() != outputDir) return;
      pane->setPath(outputDir);
      FileListModel* model = pane->model();
      for (int i = 0; i < model->rowCount(); ++i) {
        const FileItem* item = model->itemAt(i);
        if (item && item->name() == baseName) {
          pane->view()->setCurrentIndex(model->index(i, 0));
          break;
        }
      }
    };
    refreshIfMatches(srcPane);
    refreshIfMatches(destPane);
    activePane()->view()->setFocus();
  });
  worker->start();
  dialog->exec();
}

void FileManagerPanel::openSortFilterDialog() {
  FileListPane* pane = activePane();
  const QString path = pane->currentPath();
  if (path.isEmpty()) {
    return;
  }

  auto& settings = Settings::instance();
  const bool alreadySaved = settings.hasPathOverride(path);

  // 初期値は override があればそれ、なければペインの既定
  PaneSettings initial = alreadySaved
                         ? settings.pathOverride(path)
                         : settings.paneSettings(m_activePane);

  // ダイアログ表示前のカーソル位置 (= ファイル名) を覚えておく。
  // ソート / フィルタを適用すると beginResetModel/endResetModel で currentIndex が
  // クリアされるため、適用後に同名行へカーソルを戻す。フィルタで非表示になった
  // 場合は復元できないので先頭 (".." 行) に置く。
  QString savedCursorName;
  if (const FileItem* it = pane->model()->itemAt(pane->view()->currentIndex())) {
    savedCursorName = it->name();
  }

  SortFilterDialog dialog(path, initial, alreadySaved, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  PaneSettings result = dialog.result();

  // 保存 or 削除
  if (dialog.saveForDirectory()) {
    settings.setPathOverride(path, result);
    settings.save();
  } else if (alreadySaved) {
    settings.removePathOverride(path);
    settings.save();
  }

  // 現在のビューに即座に適用
  FileListModel* model = pane->model();
  model->setSortSettings(
    result.sortKey, result.sortOrder, result.sortKey2nd,
    result.sortDirsType, result.sortDotFirst, result.sortCS
  );
  model->setAttrFilter(result.attrFilter);
  model->setNameFilters(result.nameFilters);

  int section = -1;
  switch (result.sortKey) {
    case SortKey::Name:         section = FileListModel::Name;         break;
    case SortKey::Size:         section = FileListModel::Size;         break;
    case SortKey::Type:         section = FileListModel::Type;         break;
    case SortKey::LastModified: section = FileListModel::LastModified; break;
    case SortKey::None: break;
  }
  if (section >= 0) {
    pane->view()->horizontalHeader()->setSortIndicator(section, result.sortOrder);
  }

  // カーソル復元: 適用後の (filter + sort 後の) m_entries から同名行を探す。
  // 見つかれば setCurrentIndex + scrollTo、見つからなければ先頭に置く。
  if (model->rowCount() > 0) {
    int targetRow = 0;
    if (!savedCursorName.isEmpty()) {
      for (int i = 0; i < model->rowCount(); ++i) {
        const FileItem* it = model->itemAt(i);
        if (it && it->name() == savedCursorName) {
          targetRow = i;
          break;
        }
      }
    }
    const QModelIndex idx = model->index(targetRow, 0);
    pane->view()->setCurrentIndex(idx);
    pane->view()->scrollTo(idx, QAbstractItemView::EnsureVisible);
  }

  pane->refreshSortFilterStatus();
  pane->view()->setFocus();
}

void FileManagerPanel::bulkRenameItems() {
  FileListPane* srcPane  = activePane();
  FileListModel* srcModel = srcPane->model();

  if (srcModel->isInArchiveMode()) {
    warn(this, tr("Read-only archive"),
      tr("Cannot rename entries inside a read-only archive."));
    return;
  }

  // 対象集め: 選択ファイルがあればそれら、無ければカーソル行 1 件
  QStringList names;
  for (int i = 0; i < srcModel->rowCount(); ++i) {
    const FileItem* it = srcModel->itemAt(i);
    if (!it || it->isDotDot()) continue;
    if (it->isSelected()) names.append(it->name());
  }
  if (names.isEmpty()) {
    QModelIndex cur = srcPane->view()->currentIndex();
    if (cur.isValid()) {
      const FileItem* it = srcModel->itemAt(cur);
      if (it && !it->isDotDot()) names.append(it->name());
    }
  }
  if (names.isEmpty()) return;

  const QString dirPath = srcPane->currentPath();

  BulkRenameDialog dialog(dirPath, names, this);
  if (dialog.exec() != QDialog::Accepted) return;

  const auto pairs = dialog.renames();
  int ok = 0;
  int ng = 0;
  for (const auto& p : pairs) {
    const QString oldPath = dirPath + QLatin1Char('/') + p.first;
    const QString newPath = dirPath + QLatin1Char('/') + p.second;
    QFileInfo fi(oldPath);
    bool success = false;
    if (fi.isDir()) {
      QDir d;
      success = d.rename(oldPath, newPath);
    } else {
      QFile f;
      success = f.rename(oldPath, newPath);
    }
    if (success) {
      Logger::instance().info(QStringLiteral("Bulk rename: %1 → %2")
                                .arg(p.first, p.second));
      ++ok;
    } else {
      Logger::instance().error(QStringLiteral("Bulk rename failed: %1 → %2")
                                 .arg(p.first, p.second));
      ++ng;
    }
  }
  if (ng > 0) {
    warn(this, tr("Bulk Rename"),
      tr("%1 file(s) renamed, %2 failed.").arg(ok).arg(ng));
  }

  // ペインを再読み込みしてカーソルを保持
  const QString currentPath = srcPane->currentPath();
  srcPane->setPath(currentPath);
}

void FileManagerPanel::renameItem() {
  FileListPane* srcPane = activePane();
  FileListModel* srcModel = srcPane->model();

  if (srcModel->isInArchiveMode()) {
    warn(this, tr("Read-only archive"),
      tr("Cannot rename an entry inside a read-only archive."));
    return;
  }

  QModelIndex currentIndex = srcPane->view()->currentIndex();
  if (!currentIndex.isValid()) {
    return;
  }

  const FileItem* item = srcModel->itemAt(currentIndex);
  if (!item || item->isDotDot()) {
    return;
  }

  QString oldName = item->name();
  QString oldPath = item->absolutePath();
  // isDir も含めて必要な情報はダイアログを開く前に退避する。
  // inputText() はネストしたイベントループを回すため、その間に
  // QFileSystemWatcher 経由でモデルが refresh されると `item` が破棄され得る
  // (Google ドライブ等のバックグラウンド同期で発生しやすい)。ダイアログ後に
  // `item` を参照すると use-after-free になるので、以降は退避値のみを使う。
  const bool itemIsDir = item->isDir();

  // Ask for new name. リネームではカーソルを末尾ではなく拡張子の手前に
  // 置く (例: "foo.txt" を選んだ状態で `r` を押すと、カーソルは "foo" の
  // 直後)。コピー衝突時の OverwriteDialog のリネーム入力欄と同じ挙動。
  bool ok = false;
  QString newName = inputText(
    this,
    tr("Rename"),
    tr("Enter new name:"),
    oldName,
    &ok,
    TextInputCursor::BeforeExtension
  );

  if (!ok || newName.isEmpty() || newName == oldName) {
    return;
  }

  // Check if new name already exists
  QString parentPath = QFileInfo(oldPath).path();
  QString newPath = parentPath + "/" + newName;

  if (QFileInfo::exists(newPath)) {
    critical(
      this,
      tr("Error"),
      tr("A file or directory with the name '%1' already exists.").arg(newName)
    );
    return;
  }

  // Rename (item はダイアログ後に破棄され得るため退避した itemIsDir を使う)
  bool success = false;
  if (itemIsDir) {
    QDir dir;
    success = dir.rename(oldPath, newPath);
  } else {
    QFile file;
    success = file.rename(oldPath, newPath);
  }

  if (!success) {
    Logger::instance().error(QStringLiteral("Rename failed: %1 → %2")
      .arg(oldPath, newPath));
    critical(
      this,
      tr("Error"),
      tr("Failed to rename '%1' to '%2'.").arg(oldName, newName)
    );
    return;
  }
  Logger::instance().info(QStringLiteral("Rename: %1 → %2").arg(oldName, newName));

  // Refresh and move cursor to renamed item
  QString currentPath = srcPane->currentPath();
  srcPane->setPath(currentPath);

  // Find and select the renamed item
  for (int i = 0; i < srcModel->rowCount(); ++i) {
    const FileItem* renamedItem = srcModel->itemAt(i);
    if (renamedItem && renamedItem->name() == newName) {
      srcPane->view()->setCurrentIndex(srcModel->index(i, 0));
      break;
    }
  }

  srcPane->view()->setFocus();
}

// ── ディレクトリ比較 ────────────────────────────────────────────
// 左右ペインのカレントを突き合わせ、結果オーバーレイを各 model に流し込んで
// 着色表示する。NameOnly / SizeMtime 粒度と再帰比較に対応する。
void FileManagerPanel::startDirectoryCompare() {
  // アーカイブモード中は対象にできない (libarchive エントリは QDir で読めない)
  if (m_leftPane->model()->isInArchiveMode() ||
      m_rightPane->model()->isInArchiveMode()) {
    warn(this, tr("Cannot compare"),
      tr("Directory compare is not available while one of the panes is "
         "browsing an archive."));
    return;
  }
  if (!isDualPaneMode()) {
    warn(this, tr("Cannot compare"),
      tr("Directory compare requires two panes."));
    return;
  }

  const QString leftPath  = m_leftPane->currentPath();
  const QString rightPath = m_rightPane->currentPath();

  // 左右が同じディレクトリを開いていたら比較しても全件 Same になるだけなので
  // 早期 return。両ペインのパスを QDir::cleanPath で正規化して比較する
  // (末尾スラッシュや "/foo/./bar" のような表記揺れを吸収するため)。
  if (QDir::cleanPath(leftPath) == QDir::cleanPath(rightPath)) {
    warn(this, tr("Cannot compare"),
      tr("Both panes are showing the same directory; nothing to compare."));
    return;
  }

  DirectoryCompareDialog dlg(leftPath, rightPath, this);
  if (dlg.exec() != QDialog::Accepted) return;
  m_compareOptions = dlg.options();

  // 比較は QDir 列挙ベースで、NameOnly/SizeMtime なら軽量。再帰 ON 時も
  // 同名ディレクトリの Same/Differ 集約だけを行うため、進捗ダイアログは出さない。
  auto* worker = new DirectoryCompareWorker(leftPath, rightPath, m_compareOptions, this);
  connect(worker, &WorkerBase::finished, this,
    [this, worker, leftPath, rightPath](bool ok) {
      if (!ok) {
        worker->deleteLater();
        return;
      }
      const CompareResult result = worker->result();
      m_leftPane->model()->setCompareOverlay(result.left);
      m_rightPane->model()->setCompareOverlay(result.right);
      m_compareMode = true;
      emit directoryCompareChanged(true);
      Logger::instance().info(
        tr("Directory compare done: %1 ↔ %2 (%3 / %4 items)")
          .arg(leftPath, rightPath)
          .arg(result.left.size())
          .arg(result.right.size()));
      worker->deleteLater();
    });
  worker->start();
}

void FileManagerPanel::stopDirectoryCompare() {
  if (!m_compareMode &&
      m_leftPane->model()->inCompareMode() == false &&
      m_rightPane->model()->inCompareMode() == false) {
    return;
  }
  m_leftPane->model()->clearCompareOverlay();
  m_rightPane->model()->clearCompareOverlay();
  m_compareMode = false;
  emit directoryCompareChanged(false);
}

// ── 比較状態に基づく選択補助 ────────────────────────────────────
// 比較モード中、ユーザーが既存の c / d / m 等を「特定の差分グループだけ」に
// 適用したいときに、対象行を一発で選択する。Plan A: 一括同期ボタンではなく
// 普通のファイラ操作 (Space + c など) を選り好みできるようにする方針。
void FileManagerPanel::selectCompareDiffer() {
  if (!m_compareMode) return;
  const int n = activePane()->model()->selectByCompareStatus(DiffStatus::Differ);
  Logger::instance().info(
    tr("Compare select: Differ rows (%1 newly selected)").arg(n));
}

void FileManagerPanel::selectCompareOnlyHere() {
  if (!m_compareMode) return;
  const int n = activePane()->model()->selectByCompareStatus(DiffStatus::OnlyHere);
  Logger::instance().info(
    tr("Compare select: Only-Here rows (%1 newly selected)").arg(n));
}

void FileManagerPanel::recompareIfActive() {
  if (!m_compareMode) return;
  if (m_leftPane->model()->isInArchiveMode() ||
      m_rightPane->model()->isInArchiveMode()) {
    return;  // 念のため (アーカイブ入った場合は navigatePane で既に解除されている)
  }
  const QString leftPath  = m_leftPane->currentPath();
  const QString rightPath = m_rightPane->currentPath();
  auto* worker = new DirectoryCompareWorker(leftPath, rightPath, m_compareOptions, this);
  connect(worker, &WorkerBase::finished, this,
    [this, worker](bool ok) {
      if (!ok) { worker->deleteLater(); return; }
      const CompareResult result = worker->result();
      m_leftPane->model()->setCompareOverlay(result.left);
      m_rightPane->model()->setCompareOverlay(result.right);
      m_compareMode = true;  // setCompareOverlay は内部で m_compareMode=true にする
      emit directoryCompareChanged(true);
      worker->deleteLater();
    });
  worker->start();
}

void FileManagerPanel::selectCompareNewer() {
  if (!m_compareMode) return;
  FileListPane*  pane      = activePane();
  FileListPane*  otherPane = (m_activePane == PaneType::Left) ? m_rightPane : m_leftPane;
  FileListModel* model     = pane->model();
  const QString  selfDir   = pane->currentPath();
  const QString  otherDir  = otherPane->currentPath();
  const CompareOverlay& ov = model->compareOverlay();

  int selected = 0;
  for (int i = 0; i < model->rowCount(); ++i) {
    const FileItem* it = model->itemAt(i);
    if (!it || it->isDotDot()) continue;
    auto ovi = ov.constFind(it->name());
    if (ovi == ov.cend() || ovi.value() != DiffStatus::Differ) continue;
    const QFileInfo selfInfo (selfDir  + QStringLiteral("/") + it->name());
    const QFileInfo otherInfo(otherDir + QStringLiteral("/") + it->name());
    const qint64 st = selfInfo.lastModified().toSecsSinceEpoch();
    const qint64 ot = otherInfo.lastModified().toSecsSinceEpoch();
    if (st > ot && !it->isSelected()) {
      model->setSelected(i, true);
      ++selected;
    }
  }
  Logger::instance().info(
    tr("Compare select: Newer-than-other rows (%1 newly selected)").arg(selected));
}

} // namespace Farman
