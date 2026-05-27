#include "MainWindow.h"
#include "FileManagerPanel.h"
#include "FileListPane.h"
#include "ViewerPanel.h"
#include "SettingsDialog.h"
#include "ShortcutListDialog.h"
#include "PropertiesDialog.h"
#include "BookmarkListDialog.h"
#include "HistoryDialog.h"
#include "SearchDialog.h"
#include "../core/FileItem.h"
#include "../core/Logger.h"
#include "../core/UpdateChecker.h"
#include "../core/UpdateDownloader.h"
#include "../core/UserCommand.h"
#include "UpdateAvailableDialog.h"
#include "../core/UserCommandManager.h"
#include "../core/PlaceholderExpander.h"
#include "../keybinding/ICommand.h"
#include "../core/DirectoryHistory.h"
#include "../model/FileListModel.h"
#include "../utils/Dialogs.h"
#include "../utils/EnterClickFilter.h"
#include "../viewer/TextViewerWindow.h"
#include "../viewer/ImageViewerWindow.h"
#include "../viewer/BinaryViewerWindow.h"
#include "../viewer/MarkdownViewerWindow.h"
#include "../viewer/PdfViewerWindow.h"
#include "../viewer/CsvViewerWindow.h"
#include <QActionGroup>
#include <QDesktopServices>
#include <QDir>
#include <QLocale>
#include <QScreen>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStorageInfo>
#include <QTimer>
#include <QLabel>
#include <QFontMetrics>
#include <QUrl>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QTableView>
#include <QApplication>
#include <QProgressDialog>
#include <QClipboard>
#include <QMenuBar>
#include <QMenu>
#include <QToolButton>
#include <QAction>
#include <QToolBar>

namespace Farman {

MainWindow::MainWindow(QWidget* parent)
  : QMainWindow(parent)
  , m_stack(nullptr)
  , m_fileManagerPanel(nullptr)
  , m_viewerPanel(nullptr) {

  // Load settings first (before setupUi to apply window size/position)
  Settings::instance().load();

  // ロガーを設定 (ファイル出力 ON/OFF・出力先) し、起動の旨を 1 行残す
  {
    auto& s = Settings::instance();
    Logger::instance().setFileOutput(s.logToFile(), s.logDirectory(), s.logRetentionDays());
    Logger::instance().info(QStringLiteral("farman started"));
  }

  setupUi();

  // Register commands
  registerCommands();

  // 外部アプリ (UserCommand) の準備:
  // - PaneContext は MainWindow が握っている FileManagerPanel から組み立てる。
  //   ここでラムダだけ仕込み、実際の参照は実行時に毎回拾う (起動時点では
  //   m_fileManagerPanel は既に setupUi で生成済み)。
  // - sync() で組み込み terminal / editor が CommandRegistry に入る。
  //   これにより loadFromSettings 時にバインドされた T / E が解決可能になる。
  // - Settings 変更を受けて再 sync。
  UserCommandManager::instance().setContextProvider([this]() -> PaneContext {
    PaneContext ctx;
    if (!m_fileManagerPanel) return ctx;
    ctx.activeDir = m_fileManagerPanel->activePane()->currentPath();
    // 反対ペイン
    FileListPane* other = (m_fileManagerPanel->activePane() == m_fileManagerPanel->leftPane())
                          ? m_fileManagerPanel->rightPane()
                          : m_fileManagerPanel->leftPane();
    ctx.otherDir = other ? other->currentPath() : QString();

    // カーソル位置のファイル
    if (auto* model = m_fileManagerPanel->activePane()->model()) {
      const QModelIndex cur = m_fileManagerPanel->activePane()->view()->currentIndex();
      if (cur.isValid()) {
        if (const FileItem* item = model->itemAt(cur)) {
          if (!item->isDotDot()) {
            ctx.cursorPath = item->absolutePath();
            ctx.cursorName = item->name();
            ctx.cursorExt  = item->suffix();
          }
        }
      }
      ctx.selectedPaths = model->selectedFilePaths();
    }
    return ctx;
  });
  connect(&Settings::instance(), &Settings::settingsChanged,
          &UserCommandManager::instance(), &UserCommandManager::sync);
  UserCommandManager::instance().sync();

  // Load keybindings
  KeyBindingManager::instance().loadFromSettings();

  // メニューバーはキーバインドが確定した後に作る（右端にショートカットを表示するため）
  createMenus();

  // ツールバーも同様に、addCmd と同じ要領で QAction を生成するため
  // メニュー構築の後に作る (m_toolbar はトグル時の表示制御用に保持)。
  createMainToolBar();
  applyToolbarVisibility();

  // Tools メニューの中身は UserCommand の追加 / 削除に追従する。
  connect(&UserCommandManager::instance(), &UserCommandManager::userCommandsChanged,
          this, &MainWindow::rebuildToolsMenu);

  // 履歴は永続化が ON の場合のみ、初期パス読み込みの前に復元しておく。
  // loadInitialPath() が navigatePane() を呼び、現在パスが履歴の先頭に自然に入る。
  if (Settings::instance().persistHistory()) {
    m_fileManagerPanel->history(PaneType::Left).setEntries(
      Settings::instance().paneHistory(PaneType::Left));
    m_fileManagerPanel->history(PaneType::Right).setEntries(
      Settings::instance().paneHistory(PaneType::Right));
  }

  // Show file manager and load initial path
  m_stack->setCurrentWidget(m_fileManagerPanel);
  m_fileManagerPanel->loadInitialPath();
  // Settings に保存されているレイアウトを復元 (前回終了時が Preview なら Preview)。
  m_fileManagerPanel->setLayoutMode(Settings::instance().layoutMode());
  m_fileManagerPanel->activePane()->view()->setFocus();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
  // タイトルバーはアプリ名 + バージョンのみ。左右ペインのパスは
  // ステータスバーに既に出ているのでタイトルでは繰り返さない。
  // Sync Browse が ON のときは末尾 "[Sync]" を付けるため、ベース部分は別途保持。
  m_windowTitleBase = QStringLiteral("farman ") + QStringLiteral(QT_STRINGIFY(FARMAN_VERSION));
  updateWindowTitle();

  // 自動アップデートチェック (Settings::autoUpdateCheckOnStartup が ON で
  // 前回チェックから 24h 経過していれば実行)。失敗時はサイレント、新版が
  // あれば UpdateAvailableDialog を表示する。
  maybeCheckForUpdatesOnStartup();

  // Apply window size settings
  auto& settings = Settings::instance();
  QSize windowSize(1200, 600);  // Default size

  switch (settings.windowSizeMode()) {
    case WindowSizeMode::Default:
      windowSize = QSize(1200, 600);
      break;
    case WindowSizeMode::LastSession:
      windowSize = settings.lastWindowSize();
      break;
    case WindowSizeMode::Custom:
      windowSize = settings.customWindowSize();
      break;
  }

  resize(windowSize);

  // Apply window position settings
  switch (settings.windowPositionMode()) {
    case WindowPositionMode::Default:
      // Center window on screen (Qt default behavior)
      break;
    case WindowPositionMode::LastSession:
      move(settings.lastWindowPosition());
      break;
    case WindowPositionMode::Custom:
      move(settings.customWindowPosition());
      break;
  }

  // Central widget with stack
  QWidget* central = new QWidget(this);
  setCentralWidget(central);

  QVBoxLayout* layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);

  m_stack = new QStackedWidget(this);
  layout->addWidget(m_stack);

  // ===== File Manager Panel =====
  m_fileManagerPanel = new FileManagerPanel(this);
  connect(m_fileManagerPanel, &FileManagerPanel::fileActivated, this, &MainWindow::onFileActivated);
  // pathChanged はステータスバー側で扱う。タイトルには反映しない。

  // Install event filter on both panes. List ビュー (QTableView) と Thumbnail
  // ビュー (QListView) の両方に仕掛けないと、Thumbnail モード時にキーが届かず
  // 矢印キー等が機能しない。selectionModel は共有しているので、どちらに
  // setCurrentIndex してももう一方に同期する。
  m_fileManagerPanel->leftPane()->view()->installEventFilter(this);
  m_fileManagerPanel->rightPane()->view()->installEventFilter(this);
  if (auto* lt = m_fileManagerPanel->leftPane()->thumbnailView()) {
    lt->installEventFilter(this);
  }
  if (auto* rt = m_fileManagerPanel->rightPane()->thumbnailView()) {
    rt->installEventFilter(this);
  }

  m_stack->addWidget(m_fileManagerPanel);

  // ===== Viewer Panel =====
  m_viewerPanel = new ViewerPanel(this);
  m_viewerPanel->installEventFilter(this);
  m_stack->addWidget(m_viewerPanel);

  // ===== Status Bar =====
  m_statusPathLabel = new QLabel(this);
  m_statusPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  // 長いパスは末尾を表示できるよう中央付近を省略 (... で省略表示)
  m_statusPathLabel->setMinimumWidth(0);
  m_statusPathLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_statusSummaryLabel = new QLabel(this);
  m_statusSummaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  // Sync Browse の状態表示 (OFF 時は空文字 = 何も出ない)。
  // 件数の左隣に置くと「いま何モードか」をパスから視線を逸らさず確認できる。
  m_statusSyncBrowseLabel = new QLabel(this);
  m_statusSyncBrowseLabel->setObjectName(QStringLiteral("syncBrowseLabel"));
  // ディレクトリ比較モードのインジケータ。OFF 時は空文字列で何も表示しない。
  m_statusCompareLabel = new QLabel(this);
  m_statusCompareLabel->setObjectName(QStringLiteral("compareLabel"));
  // アクティブペインのボリューム使用量表示。空き / 全体 / 使用率を出す。
  m_statusDiskLabel = new QLabel(this);
  m_statusDiskLabel->setObjectName(QStringLiteral("diskLabel"));
  m_statusDiskLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  statusBar()->addWidget(m_statusPathLabel, /*stretch*/ 1);
  statusBar()->addPermanentWidget(m_statusCompareLabel);
  statusBar()->addPermanentWidget(m_statusSyncBrowseLabel);
  statusBar()->addPermanentWidget(m_statusDiskLabel);
  statusBar()->addPermanentWidget(m_statusSummaryLabel);

  // コピー / 移動 / 削除などで空き容量が変動するので、5 秒ごとに自動更新する。
  // ユーザー操作と独立なので軽量。activePane().currentPath() が変わったときは
  // activeFocusedPathChanged のハンドラ側で即時に updateDiskStatus を呼ぶ。
  m_diskUpdateTimer = new QTimer(this);
  m_diskUpdateTimer->setInterval(5000);
  connect(m_diskUpdateTimer, &QTimer::timeout, this, &MainWindow::updateDiskStatus);
  m_diskUpdateTimer->start();

  connect(m_fileManagerPanel, &FileManagerPanel::activeFocusedPathChanged,
          this, [this](const QString& path) {
    m_fmStatusPath = path;
    if (m_stack->currentWidget() == m_fileManagerPanel) updateStatusBar();
    // パスが変わるとボリュームが変わっている可能性があるので即時更新
    updateDiskStatus();
  });
  connect(m_fileManagerPanel, &FileManagerPanel::activeSummaryChanged,
          this, [this](const QString& summary) {
    m_fmStatusSummary = summary;
    if (m_stack->currentWidget() == m_fileManagerPanel) updateStatusBar();
  });
  connect(m_viewerPanel, &ViewerPanel::viewerStatusChanged,
          this, [this](const QString& path, const QString& summary) {
    m_viewerStatusPath = path;
    m_viewerStatusSummary = summary;
    if (m_stack->currentWidget() == m_viewerPanel) updateStatusBar();
  });
}

void MainWindow::updateWindowTitle() {
  // 同期ブラウズ / ディレクトリ比較が ON のときはタイトルバーにサフィックスを
  // 付けて、ユーザーがメニューを開かなくても現在のモードが分かるようにする。
  const bool syncOn    = m_fileManagerPanel && m_fileManagerPanel->isSyncBrowseEnabled();
  const bool compareOn = m_fileManagerPanel && m_fileManagerPanel->isDirectoryCompareActive();
  QStringList tags;
  if (syncOn)    tags << tr("Sync");
  if (compareOn) tags << tr("Compare");
  const QString title = tags.isEmpty()
    ? m_windowTitleBase
    : (m_windowTitleBase + QStringLiteral(" — [") + tags.join(QStringLiteral("] [")) + QStringLiteral("]"));
  setWindowTitle(title);
}

void MainWindow::updateStatusBar() {
  const bool fm = (m_stack && m_stack->currentWidget() == m_fileManagerPanel);
  const QString& path    = fm ? m_fmStatusPath    : m_viewerStatusPath;
  const QString& summary = fm ? m_fmStatusSummary : m_viewerStatusSummary;
  m_statusPathLabel->setText(path);
  m_statusPathLabel->setToolTip(path);
  m_statusSummaryLabel->setText(summary);
  // ディスク使用量はファイルマネージャ表示中だけ意味を持つ (ビュアーは単一
  // ファイルを開いているだけなのでボリューム情報は表示しない)。
  if (m_statusDiskLabel) m_statusDiskLabel->setVisible(fm);
}

namespace {

// ToolbarStyle (farman の enum) → Qt::ToolButtonStyle へのマップ。
// Settings::toolbarStyle() の値を QToolBar::setToolButtonStyle に渡すために使う。
Qt::ToolButtonStyle toQtToolButtonStyle(ToolbarStyle s) {
  switch (s) {
    case ToolbarStyle::TextOnly:        return Qt::ToolButtonTextOnly;
    case ToolbarStyle::IconBesideText:  return Qt::ToolButtonTextBesideIcon;
    case ToolbarStyle::IconOnly:
    default:                            return Qt::ToolButtonIconOnly;
  }
}

// パスがクラウド同期フォルダ (Google Drive / iCloud / OneDrive / Dropbox 等)
// 配下なら true。これらは OS から見ると「ローカル FS の一部のディレクトリ」
// として統合されていて、QStorageInfo はホストディスクの容量を返してしまう
// (= 期待した「クラウド側の使用量」にならない)。誤解を招くので容量表示を
// 抑止する。
bool isCloudSyncPath(const QString& path) {
  if (path.isEmpty()) return false;
  const QString cleaned = QDir::cleanPath(path);
  const QString home    = QDir::cleanPath(QDir::homePath());
  if (home.isEmpty()) return false;

  // ホーム配下に相対化する。ホーム外なら通常 FS 扱いで OK (例外: Windows の
  // %OneDrive% がホーム外を指すケースは別途検出)。
  QString rel;
  if (cleaned == home) {
    return false;
  } else if (cleaned.startsWith(home + QLatin1Char('/'))) {
    rel = cleaned.mid(home.size() + 1);
  } else {
#ifdef Q_OS_WIN
    // Windows: %OneDrive% / %OneDriveCommercial% が C:\Users 外を指すケース
    for (const char* var : { "OneDrive", "OneDriveConsumer", "OneDriveCommercial" }) {
      const QString od = QDir::cleanPath(qEnvironmentVariable(var));
      if (!od.isEmpty() && (cleaned == od || cleaned.startsWith(od + QLatin1Char('/')))) {
        return true;
      }
    }
#endif
    return false;
  }

  // ホーム直下の固定パターン (前方一致)
  static const QStringList prefixes = {
    QStringLiteral("Library/CloudStorage/"),                      // macOS File Provider (Google/OneDrive/Dropbox/Box etc.)
    QStringLiteral("Library/Mobile Documents/com~apple~CloudDocs/"),  // iCloud Drive (macOS)
    QStringLiteral("Dropbox/"),
    QStringLiteral("Dropbox ("),     // "Dropbox (Personal)" / "Dropbox (Company)" 等
    QStringLiteral("Dropbox - "),    // 一部の Dropbox Business レイアウト
    QStringLiteral("OneDrive/"),
    QStringLiteral("OneDrive - "),   // "OneDrive - Company" のように業務アカ用
    QStringLiteral("Google Drive/"), // 旧 Google Drive 統合
    QStringLiteral("Google Drive ("),
  };
  for (const QString& p : prefixes) {
    if (rel.startsWith(p)) return true;
  }
  // 名前そのもの (子のないルートディレクトリ表示時)
  static const QStringList exacts = {
    QStringLiteral("Dropbox"),
    QStringLiteral("OneDrive"),
    QStringLiteral("Google Drive"),
  };
  for (const QString& e : exacts) {
    if (rel == e) return true;
  }
  return false;
}

}  // anonymous namespace

void MainWindow::updateDiskStatus() {
  if (!m_statusDiskLabel || !m_fileManagerPanel) return;
  // ビュアー表示中はラベル自体を隠しているので更新は不要
  if (m_stack && m_stack->currentWidget() != m_fileManagerPanel) return;

  // アクティブペインのカレントディレクトリが属するボリュームを引く。
  // m_fmStatusPath (フォーカス中ファイルのパス) ではなく currentPath() を
  // 使うのは、ペインに何も選択していない (= 焦点無し) ケースでも表示が
  // 出るようにするため。
  FileListPane* pane = m_fileManagerPanel->activePane();
  if (!pane) return;
  const QString path = pane->currentPath();
  if (path.isEmpty()) {
    m_statusDiskLabel->clear();
    m_statusDiskLabel->setToolTip(QString());
    return;
  }

  // アーカイブ内パス (archive.zip!/inner) の場合は仮想 FS なので QStorage
  // Info が役に立たない。`!` の手前 (アーカイブが置かれている実 FS パス) を
  // 渡して、そのボリュームの情報を出す。
  QString fsPath = path;
  const int sep = fsPath.indexOf(QLatin1Char('!'));
  if (sep >= 0) fsPath = fsPath.left(sep);

  // クラウド同期フォルダは容量表示を抑止 (ホスト FS の値が出てしまうため)
  if (isCloudSyncPath(fsPath)) {
    m_statusDiskLabel->setText(tr("<cloud sync folder>"));
    m_statusDiskLabel->setToolTip(
      tr("This looks like a cloud sync folder (Google Drive / OneDrive / "
         "Dropbox / iCloud).\nLocal disk usage is not shown here because "
         "the host volume size would be misleading."));
    return;
  }

  QStorageInfo storage(fsPath);
  if (!storage.isValid() || !storage.isReady()) {
    m_statusDiskLabel->clear();
    m_statusDiskLabel->setToolTip(QString());
    return;
  }
  const qint64 total = storage.bytesTotal();
  const qint64 free  = storage.bytesAvailable();
  if (total <= 0) {
    m_statusDiskLabel->clear();
    m_statusDiskLabel->setToolTip(QString());
    return;
  }
  const int usedPct = static_cast<int>(((total - free) * 100) / total);
  // バイト単位は英語固定 (SPEC.md「バイトサイズの表記」と整合)。
  const QLocale en(QLocale::English);
  const QString freeStr  = en.formattedDataSize(free,  1,
                              QLocale::DataSizeTraditionalFormat);
  const QString totalStr = en.formattedDataSize(total, 0,
                              QLocale::DataSizeTraditionalFormat);
  m_statusDiskLabel->setText(
    tr("%1 free / %2 (%3%% used)").arg(freeStr, totalStr).arg(usedPct));
  // tooltip にボリューム名 / マウントポイント / ファイルシステム種別
  m_statusDiskLabel->setToolTip(
    tr("Volume: %1\nMount point: %2\nFile system: %3")
      .arg(storage.displayName(),
           storage.rootPath(),
           QString::fromUtf8(storage.fileSystemType())));
}

void MainWindow::showFileManager() {
  if (m_stack->currentWidget() != m_fileManagerPanel) {
    m_stack->setCurrentWidget(m_fileManagerPanel);
    m_viewerPanel->clear();

    // ファイルマネージャに戻ったので、Settings に従ってツールバーを再表示。
    // (Inline ビュアーで強制非表示にしたものを復元)
    applyToolbarVisibility();

    // フォーカスをアクティブペインに戻す
    m_fileManagerPanel->activePane()->view()->setFocus();
    updateStatusBar();
  }
}

void MainWindow::showViewer(const QString& filePath, const QString& displayPath) {
  showViewerWith(filePath, ViewerPanel::ViewerKind::Auto, displayPath);
}

void MainWindow::showViewerWith(const QString& filePath, ViewerPanel::ViewerKind kind,
                                const QString& displayPath) {
  // 表示用パス: 指定なしなら filePath をそのまま使う。
  // (外部ビュアーウィンドウのタイトルにも反映)
  const QString shownPath = displayPath.isEmpty() ? filePath : displayPath;
  // ビュアー表示モード (Inline / External) によって振り分ける。
  // External 時は独立 QMainWindow を起こす。Inline 時は従来通り内蔵パネル。
  const ViewerMode mode = Settings::instance().viewerMode();

  if (mode == ViewerMode::External) {
    // 拡張子 / MIME ルーティングは ViewerPanel と共通。Auto を解決して
    // どの *ViewerWindow を起こすか決める。
    ViewerPanel::ViewerKind effective = kind;
    if (effective == ViewerPanel::ViewerKind::Auto) {
      effective = ViewerPanel::resolveAuto(filePath);
    }

    // External ビュアーは同時に 1 つしか開かない方針。前のウィンドウが
    // まだ生きていれば、ジオメトリを引き継いでから破棄する (= ユーザーが
    // 「別ファイルを開いたら同じ場所・同じサイズで上書き」と感じる挙動)。
    QRect savedGeom;
    bool  hasSavedGeom = false;
    if (m_externalViewerWindow) {
      savedGeom    = m_externalViewerWindow->geometry();
      hasSavedGeom = true;
      // 同期 delete: 後続の new と同フレームに新ウィンドウを出すため、
      // deleteLater() ではなく即時 delete。QPointer なので m_external...
      // は自動で nullptr になる。
      delete m_externalViewerWindow;
    }

    QMainWindow* w = nullptr;
    switch (effective) {
      case ViewerPanel::ViewerKind::Text:
        w = new TextViewerWindow(filePath, shownPath, this);
        break;
      case ViewerPanel::ViewerKind::Image:
        w = new ImageViewerWindow(filePath, shownPath, this);
        break;
      case ViewerPanel::ViewerKind::Binary:
        w = new BinaryViewerWindow(filePath, shownPath, this);
        break;
      case ViewerPanel::ViewerKind::Markdown:
        w = new MarkdownViewerWindow(filePath, shownPath, this);
        break;
      case ViewerPanel::ViewerKind::Pdf:
        w = new PdfViewerWindow(filePath, shownPath, this);
        break;
      case ViewerPanel::ViewerKind::Csv:
        w = new CsvViewerWindow(filePath, shownPath, this);
        break;
      case ViewerPanel::ViewerKind::Auto:
        /* unreachable */ break;
    }
    if (w) {
      // 閉じたら自前で破棄。MainWindow を親にしておくのは、明示的に閉じずに
      // farman 全体が終了する場合に Qt の親子関係でクリーンアップさせるため。
      w->setAttribute(Qt::WA_DeleteOnClose);
      // 独立ウィンドウとして表示。アプリのメインから切り離して別ディスプレイ
      // へドラッグできるよう、Qt::Window フラグでトップレベルを保証。
      w->setWindowFlag(Qt::Window, true);
      // 前回のジオメトリを引き継ぎ。初回は *ViewerWindow の setupUi 側で
      // resize(800, 600) してくれる。
      if (hasSavedGeom) {
        w->setGeometry(savedGeom);
      }
      w->show();
      w->raise();
      w->activateWindow();
      m_externalViewerWindow = w;
    }
    // External モードではメインウィンドウのレイアウトは触らない (= ファイル
    // マネージャパネルのまま)。ビュアーパネルへの切替は行わない。
    return;
  }

  // Inline (現状の挙動): メインウィンドウ内 ViewerPanel に切り替えて表示。
  // 先にビュアーパネルへ切り替えてからロードする。これで ViewerPanel
  // 内部で表示する「読み込み中…」プレースホルダがユーザーから見える
  // 状態になる (順序を逆にすると、ロード中は依然としてファイルリストが
  // 見えており、ロードが終わってからスタックが切り替わるため、
  // 「ロード中の表示」が無いように見えてしまう)。
  m_stack->setCurrentWidget(m_viewerPanel);
  // ビュアー表示中はツールバーの操作対象が無い (= ファイラ用のボタン群が
  // 並んでいる) ので、表示領域を画面いっぱい使えるよう一時的に非表示にする。
  // ファイラに戻る showFileManager() で Settings::showToolbar() に従って復元。
  if (m_toolbar) m_toolbar->setVisible(false);
  m_viewerPanel->setFocus();
  updateStatusBar();

  if (!m_viewerPanel->openFile(filePath, kind, displayPath)) {
    // 失敗時はファイルマネージャパネルへ戻す
    showFileManager();
  }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
  if (event->type() == QEvent::KeyPress) {
    QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

    // ファイルマネージャーパネル表示中
    if (m_stack->currentWidget() == m_fileManagerPanel) {
      auto* lv = m_fileManagerPanel->leftPane()->view();
      auto* rv = m_fileManagerPanel->rightPane()->view();
      auto* lt = m_fileManagerPanel->leftPane()->thumbnailView();
      auto* rt = m_fileManagerPanel->rightPane()->thumbnailView();
      if (obj == lv || obj == rv || obj == lt || obj == rt) {
        // Try to route through KeyBindingManager first
        QKeySequence keySeq(keyEvent->key() | keyEvent->modifiers());
        QString commandId = KeyBindingManager::instance().commandFor(keySeq);

        if (!commandId.isEmpty()) {
          // Execute the command through the registry
          if (CommandRegistry::instance().execute(commandId)) {
            return true;
          }
        }

        // Fall back to FileManagerPanel's handleKeyEvent for unbound keys
        return m_fileManagerPanel->handleKeyEvent(keyEvent);
      }
    }
    // ビューアパネル表示中
    else if (m_stack->currentWidget() == m_viewerPanel) {
      if (obj == m_viewerPanel) {
        // Enter / Return / Esc でファイルマネージャーに戻る (External モード
        // 各 *ViewerWindow とキー対応を揃える)
        if (keyEvent->key() == Qt::Key_Return ||
            keyEvent->key() == Qt::Key_Enter  ||
            keyEvent->key() == Qt::Key_Escape) {
          showFileManager();
          return true;
        }
      }
    }
  }

  return QMainWindow::eventFilter(obj, event);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
  // ビューアパネル表示中のキーイベント
  if (m_stack->currentWidget() == m_viewerPanel) {
    // Enter / Return / Esc でファイラに戻る (External モード各 *ViewerWindow
    // とキー対応を揃える)
    if (event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter  ||
        event->key() == Qt::Key_Escape) {
      showFileManager();
      return;
    }
  }

  QMainWindow::keyPressEvent(event);
}

void MainWindow::onFileActivated(const QString& filePath,
                                 const QString& displayPath) {
  showViewer(filePath, displayPath);
}


void MainWindow::registerCommands() {
  auto& registry = CommandRegistry::instance();

  // Navigation commands
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "navigate.up",
    tr("Up"),
    [this]() {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "navigation"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "navigate.down",
    tr("Down"),
    [this]() {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "navigation"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "navigate.left",
    tr("Left"),
    [this]() {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "navigation"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "navigate.right",
    tr("Right"),
    [this]() {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "navigation"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "navigate.home",
    tr("Jump to Top"),
    [this]() {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "navigation"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "navigate.end",
    tr("Jump to Bottom"),
    [this]() {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_End, Qt::NoModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "navigation"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "navigate.pageup",
    tr("Page Up"),
    [this]() {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_PageUp, Qt::NoModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "navigation"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "navigate.pagedown",
    tr("Page Down"),
    [this]() {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "navigation"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "navigate.enter",
    tr("Enter"),
    [this]() {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "navigation"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "navigate.parent",
    tr("Parent Directory"),
    [this]() {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "navigation"
  ));

  // Selection commands
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "select.toggle",
    tr("Toggle Selection"),
    [this]() {
      // カーソル据え置きで選択トグル。
      m_fileManagerPanel->toggleSelection();
    },
    "selection"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "select.toggle_and_down",
    tr("Toggle Selection and Move Down"),
    [this]() {
      // 選択トグル後にカーソルを下へ。handleSpaceKey の挙動。
      QKeyEvent event(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "selection"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "select.invert",
    tr("Invert Selection"),
    [this]() {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_Asterisk, Qt::NoModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "selection"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "select.all",
    tr("Select All"),
    [this]() {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "selection"
  ));

  // Pane commands
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "pane.switch",
    tr("Switch Pane"),
    [this]() {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "pane"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "pane.toggle_single",
    tr("Toggle Single Pane Mode"),
    [this]() {
      m_fileManagerPanel->togglePaneMode();
    },
    "pane"
  ));

  // Preview レイアウトのトグル: Preview ⇔ Dual。
  // 「左ペイン: ファイル一覧 / 右ペイン: ビュアー」の Quick View モード。
  // カーソル移動でビュアーが順次切り替わる (Phase 1 以降で実装)。
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "pane.toggle_preview",
    tr("Toggle Preview Layout"),
    [this]() {
      if (m_fileManagerPanel->isPreviewMode()) {
        m_fileManagerPanel->setLayoutMode(LayoutMode::Dual);
      } else {
        m_fileManagerPanel->setLayoutMode(LayoutMode::Preview);
      }
    },
    "pane"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "pane.sort_filter",
    tr("Sort & Filter..."),
    [this]() {
      m_fileManagerPanel->openSortFilterDialog();
    },
    "pane"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "pane.sync_other_to_active",
    tr("Sync Other Pane to Active"),
    [this]() {
      m_fileManagerPanel->syncOtherToActive();
    },
    "pane"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "pane.sync_active_to_other",
    tr("Sync Active Pane to Other"),
    [this]() {
      m_fileManagerPanel->syncActiveToOther();
    },
    "pane"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "pane.sync_browse_toggle",
    tr("Toggle Sync Browse"),
    [this]() {
      m_fileManagerPanel->toggleSyncBrowse();
    },
    "pane"
  ));

  // File operation commands
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "file.copy",
    tr("Copy"),
    [this]() {
      m_fileManagerPanel->copySelectedFiles();
    },
    "file"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "file.move",
    tr("Move"),
    [this]() {
      m_fileManagerPanel->moveSelectedFiles();
    },
    "file"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "file.delete",
    tr("Delete"),
    [this]() {
      m_fileManagerPanel->deleteSelectedFiles();
    },
    "file"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "file.mkdir",
    tr("Make Directory"),
    [this]() {
      m_fileManagerPanel->createDirectory();
    },
    "file"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "file.newfile",
    tr("New File"),
    [this]() {
      m_fileManagerPanel->createFile();
    },
    "file"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "file.attributes",
    tr("Change Attributes"),
    [this]() {
      m_fileManagerPanel->changeAttributes();
    },
    "file"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "file.search",
    tr("Search Files..."),
    [this]() {
      const QString start = m_fileManagerPanel->activePane()->currentPath();
      SearchDialog dlg(start, this);
      if (dlg.exec() == QDialog::Accepted) {
        const QString picked = dlg.selectedPath();
        if (!picked.isEmpty()) {
          m_fileManagerPanel->navigateActivePaneTo(picked);
        }
      }
      m_fileManagerPanel->activePane()->view()->setFocus();
    },
    "file"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "file.pack",
    tr("Create Archive"),
    [this]() { m_fileManagerPanel->createArchive(); },
    "file"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "file.unpack",
    tr("Extract Archive"),
    [this]() { m_fileManagerPanel->extractArchive(); },
    "file"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "file.rename",
    tr("Rename"),
    [this]() {
      m_fileManagerPanel->renameItem();
    },
    "file"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "file.bulk_rename",
    tr("Bulk Rename..."),
    [this]() {
      m_fileManagerPanel->bulkRenameItems();
    },
    "file"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "file.copy_path",
    tr("Copy Path"),
    [this]() {
      auto* pane  = m_fileManagerPanel->activePane();
      auto* model = pane->model();
      const QModelIndex idx = pane->view()->currentIndex();
      if (!idx.isValid()) return;
      const FileItem* item = model->itemAt(idx.row());
      if (!item) return;
      const QString path = item->absolutePath();
      QGuiApplication::clipboard()->setText(path);
      Logger::instance().info(QStringLiteral("Path copied: %1").arg(path));
    },
    "file"
  ));

  // Application commands
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "app.quit",
    tr("Quit"),
    [this]() {
      QApplication::quit();
    },
    "application"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "app.settings",
    tr("Settings"),
    [this]() {
      showSettingsDialog();
    },
    "application"
  ));

  // 手動アップデートチェック (Help メニュー → "Check for Updates...")。
  // UpdateChecker で GitHub Releases API を叩いて、結果を simple ダイアログで表示。
  // Phase B 以降で本格的な通知ダイアログ (Update Now / Remind / Skip) に置き換える。
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "app.check_for_updates",
    tr("Check for Updates..."),
    [this]() {
      checkForUpdatesManually();
    },
    "application",
    tr("Manually check GitHub for a newer farman release.")
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "view.file",
    tr("View File"),
    [this]() {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
      m_fileManagerPanel->handleKeyEvent(&event);
    },
    "view"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "view.choose",
    tr("Open With Viewer..."),
    [this]() {
      auto* pane = m_fileManagerPanel->activePane();
      auto* model = pane->model();
      const QModelIndex idx = pane->view()->currentIndex();
      if (!idx.isValid()) return;
      const FileItem* item = model->itemAt(idx.row());
      if (!item || item->isDir()) return;
      const QString path = item->absolutePath();

      QMenu menu(this);
      menu.addAction(tr("Text Viewer"), this, [this, path]() {
        showViewerWith(path, ViewerPanel::ViewerKind::Text);
      });
      menu.addAction(tr("Image Viewer"), this, [this, path]() {
        showViewerWith(path, ViewerPanel::ViewerKind::Image);
      });
      menu.addAction(tr("Binary Viewer"), this, [this, path]() {
        showViewerWith(path, ViewerPanel::ViewerKind::Binary);
      });
      // カーソル行の左端付近に出す
      const QRect rect = pane->view()->visualRect(idx);
      const QPoint pos = pane->view()->viewport()->mapToGlobal(rect.bottomLeft());
      menu.exec(pos);
    },
    "view"
  ));

  // ファイル / ディレクトリのプロパティ表示 (Alt+Enter)
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "file.properties",
    tr("Properties..."),
    [this]() {
      auto* pane = m_fileManagerPanel->activePane();
      auto* model = pane->model();
      const QModelIndex idx = pane->view()->currentIndex();
      if (!idx.isValid()) return;
      const FileItem* item = model->itemAt(idx.row());
      if (!item || item->isDotDot()) return;
      PropertiesDialog dlg(item->absolutePath(), this);
      dlg.exec();
    },
    "file",
    tr("Show file or directory details. For directories, recursively "
       "calculates the total size in the background.")
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "file.execute",
    tr("Execute / Open Externally"),
    [this]() {
      auto* pane = m_fileManagerPanel->activePane();
      auto* model = pane->model();
      const QModelIndex idx = pane->view()->currentIndex();
      if (!idx.isValid()) return;
      const FileItem* item = model->itemAt(idx.row());
      if (!item) return;
      const QString path = item->absolutePath();
      const bool ok = QDesktopServices::openUrl(QUrl::fromLocalFile(path));
      if (ok) {
        Logger::instance().info(QStringLiteral("Execute: %1").arg(path));
      } else {
        Logger::instance().warn(QStringLiteral("Execute failed: %1").arg(path));
      }
    },
    "file"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "view.toggle_log",
    tr("Toggle Log Pane"),
    [this]() {
      const bool nowVisible = !m_fileManagerPanel->isLogPaneVisible();
      m_fileManagerPanel->setLogPaneVisible(nowVisible);
      Settings::instance().setLogVisible(nowVisible);
      Settings::instance().save();
    },
    "view"
  ));

  // ツールバーの表示トグル。Settings の値を反転して即座にウィンドウへ反映。
  // メニュー / Settings / コマンド経由のいずれからでも同じ経路で同期する。
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "view.toggle_toolbar",
    tr("Toolbar"),
    [this]() {
      auto& s = Settings::instance();
      s.setShowToolbar(!s.showToolbar());
      s.save();
      applyToolbarVisibility();
    },
    "view",
    tr("Show or hide the main toolbar.")
  ));

  // 即時フィルタ (Quick Filter Bar) のトグル。アクティブペインの
  // FileListPane が QLineEdit を出して live filter を model に反映する。
  // QLineEdit にフォーカスがあるとき (= バー入力中) に "/" を押された場合は、
  // トグルに横取りせずスラッシュ文字をそのまま入力させる (filename に "/" が
  // 含まれることは稀だが、ユーザーの直感に合わせる)。
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "view.quick_filter",
    tr("Quick Filter"),
    [this]() {
      if (auto* pane = m_fileManagerPanel ? m_fileManagerPanel->activePane() : nullptr) {
        pane->toggleQuickFilter();
      }
    },
    "view",
    tr("Toggle the quick filter bar to filter the current list by name (substring).")
  ));

  // ビュアー表示モードのトグル (Inline ⇔ External)。Settings に書き込むので
  // 設定ダイアログを開かなくても切り替え可能。現状ビュアーパネル表示中なら
  // 一旦ファイルマネージャに戻す (Inline 経由で使われていた m_viewerPanel が
  // 不整合のまま残らないように)。
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "view.toggle_viewer_mode",
    tr("Use External Viewer Window"),
    [this]() {
      auto& s = Settings::instance();
      const ViewerMode next = (s.viewerMode() == ViewerMode::External)
                                ? ViewerMode::Inline
                                : ViewerMode::External;
      s.setViewerMode(next);
      s.save();
      // External に切り替えた瞬間、ビュアーパネル表示中だったら戻す。
      if (m_stack && m_stack->currentWidget() == m_viewerPanel) {
        showFileManager();
      }
      Logger::instance().info(
        next == ViewerMode::External
          ? tr("Viewer mode: External (separate windows)")
          : tr("Viewer mode: Inline (panel)"));
    },
    "view",
    tr("Toggle between in-window viewer panel and separate viewer windows.")
  ));

  // 表示モード切替コマンド群。Cmd+1/2/3/4 で List / Thumbnail S/M/L を
  // それぞれ直接選択する (Finder 風)。アクティブペインに対して動作する。
  // 巡回ショートカット (`view.toggle_thumbnails`) も互換のため残すが、
  // デフォルトキーは外す (ユーザーが任意に再割当可能)。
  auto applyPaneViewMode = [this](ListViewMode mode) {
    if (!m_fileManagerPanel) return;
    auto* pane = m_fileManagerPanel->activePane();
    if (!pane) return;
    if (pane->viewMode() == mode) return;
    pane->setViewMode(mode);
    const PaneType pt =
      (pane == m_fileManagerPanel->leftPane()) ? PaneType::Left : PaneType::Right;
    auto& s = Settings::instance();
    s.setPaneViewMode(pt, mode);
    s.save();
  };
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "view.list",            tr("View as List"),
    [applyPaneViewMode]() { applyPaneViewMode(ListViewMode::List); },
    "view", tr("Show the active pane as a detailed list.")
  ));
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "view.thumbnail_small", tr("View as Thumbnails (Small)"),
    [applyPaneViewMode]() { applyPaneViewMode(ListViewMode::ThumbnailSmall); },
    "view", tr("Show the active pane as small thumbnails (96 px).")
  ));
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "view.thumbnail_medium", tr("View as Thumbnails (Medium)"),
    [applyPaneViewMode]() { applyPaneViewMode(ListViewMode::ThumbnailMedium); },
    "view", tr("Show the active pane as medium thumbnails (160 px).")
  ));
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "view.thumbnail_large", tr("View as Thumbnails (Large)"),
    [applyPaneViewMode]() { applyPaneViewMode(ListViewMode::ThumbnailLarge); },
    "view", tr("Show the active pane as large thumbnails (256 px).")
  ));
  // 巡回コマンド (互換): デフォルトキー無し。
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "view.toggle_thumbnails",
    tr("Cycle View Mode"),
    [this]() {
      if (!m_fileManagerPanel) return;
      auto* pane = m_fileManagerPanel->activePane();
      if (!pane) return;
      const ListViewMode next = nextListViewMode(pane->viewMode());
      pane->setViewMode(next);
      const PaneType pt =
        (pane == m_fileManagerPanel->leftPane()) ? PaneType::Left : PaneType::Right;
      auto& s = Settings::instance();
      s.setPaneViewMode(pt, next);
      s.save();
    },
    "view",
    tr("Cycle the active pane through List / Thumbnail (S/M/L).")
  ));

  // ディレクトリ比較: 左右ペインの内容差分を着色表示するモードに入る。
  // モード ON 中はもう一度同じコマンドを実行すると OFF (トグル動作)。
  // ペイン遷移時は自動 OFF (FileManagerPanel::navigatePane 側で stop を呼ぶ)。
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "view.compare_directories",
    tr("Compare Directories..."),
    [this]() {
      if (!m_fileManagerPanel) return;
      if (m_fileManagerPanel->isDirectoryCompareActive()) {
        m_fileManagerPanel->stopDirectoryCompare();
      } else {
        m_fileManagerPanel->startDirectoryCompare();
      }
    },
    "view",
    tr("Compare the contents of the two panes' current directories and "
       "highlight the differences. Press again to clear.")
  ));

  // 比較中、差分グループ別にアクティブペインの行をまとめて選択する補助。
  // 選択後は既存の c / d / m などで操作する想定 (一括同期ではなく、ユーザーが
  // Space で個別に除外できる選り好み可能なフロー)。
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "compare.select_differ",
    tr("Select Differ rows"),
    [this]() {
      if (m_fileManagerPanel) m_fileManagerPanel->selectCompareDiffer();
    },
    "view",
    tr("In compare mode, add rows that differ between the two panes to the "
       "active pane's selection.")
  ));
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "compare.select_only_here",
    tr("Select Only-Here rows"),
    [this]() {
      if (m_fileManagerPanel) m_fileManagerPanel->selectCompareOnlyHere();
    },
    "view",
    tr("In compare mode, add rows that exist only in the active pane to "
       "its selection.")
  ));
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "compare.select_newer",
    tr("Select Newer-Than-Other rows"),
    [this]() {
      if (m_fileManagerPanel) m_fileManagerPanel->selectCompareNewer();
    },
    "view",
    tr("In compare mode, add Differ rows whose mtime is newer than the "
       "matching file in the other pane.")
  ));

  // ショートカット一覧の表示トグル (`?` キー)
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "help.shortcuts",
    tr("Keyboard Shortcuts"),
    [this]() { toggleShortcutList(); },
    "help",
    tr("Show or hide the keyboard shortcuts reference window")
  ));

  // Bookmark commands
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "bookmark.toggle",
    tr("Toggle Bookmark"),
    [this]() {
      m_fileManagerPanel->activePane()->toggleBookmarkForCurrentPath();
    },
    "bookmark"
  ));

  registry.registerCommand(std::make_shared<LambdaCommand>(
    "bookmark.list",
    tr("Bookmarks..."),
    [this]() {
      BookmarkListDialog dlg(this);
      if (dlg.exec() == QDialog::Accepted) {
        const QString path = dlg.selectedPath();
        if (!path.isEmpty()) {
          m_fileManagerPanel->navigateActivePaneTo(path);
        }
      }
      // ダイアログ閉じた後もアクティブペインにフォーカスを戻す
      m_fileManagerPanel->activePane()->view()->setFocus();
    },
    "bookmark"
  ));

  // History commands
  registry.registerCommand(std::make_shared<LambdaCommand>(
    "history.show",
    tr("History..."),
    [this]() {
      const DirectoryHistory& hist = m_fileManagerPanel->history(
        m_fileManagerPanel->activePane() == m_fileManagerPanel->leftPane()
          ? PaneType::Left : PaneType::Right);
      HistoryDialog dlg(hist.entries(), this);
      if (dlg.exec() == QDialog::Accepted) {
        const QString path = dlg.selectedPath();
        if (!path.isEmpty()) {
          m_fileManagerPanel->navigateActivePaneTo(path);
        }
      }
      m_fileManagerPanel->activePane()->view()->setFocus();
    },
    "history"
  ));
}

void MainWindow::createMenus() {
  QMenuBar* bar = menuBar();

  // コマンドIDとラベルから QAction を作る。現在のキーバインドをショートカットとして
  // 設定し、メニュー右側に表示する。triggered は CommandRegistry 経由で実行するので
  // キー操作とメニュークリックで同じパスを通る。
  //
  // global=false のショートカットは FileManagerPanel スコープに限定する。
  // c / m / d のような 1 文字キーがビュアー表示中に意図せず発火するのを防ぐため。
  // macOS のネイティブメニューでは Insert キーが特殊な Unicode 字形（⎀, U+2380）
  // に変換されるが、Mac の標準フォントでは描画できず文字化けしたように見える。
  // 加えて Mac キーボードには Insert キーが無いので、メニュー上に表示する意味も薄い。
  // そういうキーが含まれる QKeySequence はメニューには出さない（eventFilter 経由の
  // キー操作は引き続き効くので機能は失われない）。
  auto isMenuDisplayable = [](const QKeySequence& seq) {
#ifdef Q_OS_MACOS
    for (int i = 0; i < seq.count(); ++i) {
      const Qt::Key k = seq[i].key();
      if (k == Qt::Key_Insert) return false;
    }
#else
    Q_UNUSED(seq);
#endif
    return true;
  };

  auto addCmd = [this, isMenuDisplayable](QMenu* menu, const QString& id, const QString& label,
                                          bool global = false) -> QAction* {
    QAction* action = new QAction(label, this);
    const QList<QKeySequence> keys = KeyBindingManager::instance().keysForCommand(id);
    if (!keys.isEmpty() && isMenuDisplayable(keys.first())) {
      action->setShortcut(keys.first());
      if (!global) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        m_fileManagerPanel->addAction(action);
      }
    }
    connect(action, &QAction::triggered, this, [id]() {
      CommandRegistry::instance().execute(id);
    });
    menu->addAction(action);
    return action;
  };

  // File
  QMenu* fileMenu = bar->addMenu(tr("&File"));
  addCmd(fileMenu, "file.newfile",    tr("New File"));
  addCmd(fileMenu, "file.mkdir",      tr("New Directory"));
  addCmd(fileMenu, "file.rename",     tr("Rename"));
  addCmd(fileMenu, "file.bulk_rename", tr("Bulk Rename..."));
  addCmd(fileMenu, "file.copy_path",  tr("Copy Path"));
  fileMenu->addSeparator();
  addCmd(fileMenu, "file.copy",       tr("Copy"));
  addCmd(fileMenu, "file.move",       tr("Move"));
  addCmd(fileMenu, "file.delete",     tr("Delete"));
  fileMenu->addSeparator();
  addCmd(fileMenu, "file.attributes", tr("Change Attributes..."));
  addCmd(fileMenu, "file.properties", tr("Properties..."));
  fileMenu->addSeparator();
  addCmd(fileMenu, "file.execute",    tr("Execute / Open Externally"));
  fileMenu->addSeparator();
  addCmd(fileMenu, "file.pack",       tr("Create Archive..."));
  addCmd(fileMenu, "file.unpack",     tr("Extract Archive..."));
  fileMenu->addSeparator();
  // Ctrl+Q はウィンドウ全体（ビュアー表示中でも）効くべきなので global=true
  QAction* quitAction = addCmd(fileMenu, "app.quit", tr("Quit"), /*global=*/true);
  // macOS ではアプリケーションメニューに自動で移動する
  quitAction->setMenuRole(QAction::QuitRole);

  // Edit（macOS では "Start Dictation" / "Auto Fill" 等のシステム項目が自動で
  // 追加されるが、慣例的な名前のほうがわかりやすいので Edit のまま）
  QMenu* editMenu = bar->addMenu(tr("&Edit"));
  addCmd(editMenu, "select.all",             tr("Select All"));
  addCmd(editMenu, "select.invert",          tr("Invert Selection"));
  addCmd(editMenu, "select.toggle",          tr("Toggle Selection"));
  addCmd(editMenu, "select.toggle_and_down", tr("Toggle and Move Down"));
  editMenu->addSeparator();
  // Settings は Edit メニュー末尾に置く (Windows/Linux の慣習)。
  // macOS では setMenuRole(PreferencesRole) によりアプリケーションメニューの
  // Preferences へ自動的に移動するため、ここの位置はあくまで非 Mac の置き場所。
  // ビュアー表示中でも開けるように global=true。
  QAction* settingsAction = addCmd(editMenu, "app.settings", tr("Settings..."), /*global=*/true);
  settingsAction->setMenuRole(QAction::PreferencesRole);

  // View
  QMenu* viewMenu = bar->addMenu(tr("&View"));
  addCmd(viewMenu, "pane.switch", tr("Switch Pane"));
  // Layout (Dual / Single / Preview) は 3 排他チェック。QActionGroup で 1 つ
  // だけチェック状態にする。キー / メニュー / 設定など複数経路で変わるため
  // aboutToShow で最新化する。
  QAction* singlePaneAction  = addCmd(viewMenu, "pane.toggle_single",  tr("Single Pane"));
  QAction* previewPaneAction = addCmd(viewMenu, "pane.toggle_preview", tr("Preview Pane"));
  singlePaneAction->setCheckable(true);
  previewPaneAction->setCheckable(true);
  singlePaneAction->setChecked(m_fileManagerPanel->isSinglePaneMode());
  previewPaneAction->setChecked(m_fileManagerPanel->isPreviewMode());
  connect(viewMenu, &QMenu::aboutToShow, this,
          [this, singlePaneAction, previewPaneAction]() {
    singlePaneAction->setChecked(m_fileManagerPanel->isSinglePaneMode());
    previewPaneAction->setChecked(m_fileManagerPanel->isPreviewMode());
    if (m_syncBrowseAction) {
      // Single / Preview 中は Sync Browse の意味がないので項目を disable する。
      // チェックマーク自体は FileManagerPanel::syncBrowseChanged で同期済み。
      m_syncBrowseAction->setEnabled(m_fileManagerPanel->isDualPaneMode());
    }
  });
  addCmd(viewMenu, "pane.sort_filter", tr("Sort && Filter..."));
  viewMenu->addSeparator();
  // 同期ブラウズ (常時連動モード) のトグル。チェックマークで現状を示し、
  // FileManagerPanel::syncBrowseChanged シグナルで双方向に状態を同期する。
  // シングルペイン時はメニュー側で disable する。
  m_syncBrowseAction = addCmd(viewMenu, "pane.sync_browse_toggle", tr("Sync Browse"));
  m_syncBrowseAction->setCheckable(true);
  connect(m_fileManagerPanel, &FileManagerPanel::syncBrowseChanged,
          this, [this](bool on) {
    if (m_syncBrowseAction) {
      QSignalBlocker blocker(m_syncBrowseAction);
      m_syncBrowseAction->setChecked(on);
    }
    if (m_statusSyncBrowseLabel) {
      m_statusSyncBrowseLabel->setText(on ? tr("Sync Browse: ON") : QString());
    }
    updateWindowTitle();
  });
  // 単発同期 (1 回だけアクティブ → 反対側 / 反対側 → アクティブ)
  addCmd(viewMenu, "pane.sync_other_to_active", tr("Sync Other Pane to Active"));
  addCmd(viewMenu, "pane.sync_active_to_other", tr("Sync Active Pane to Other"));
  // ディレクトリ比較。モード ON 中はチェックマーク + ステータスバーに状態表示。
  m_compareAction = addCmd(viewMenu, "view.compare_directories", tr("Compare Directories..."));
  m_compareAction->setCheckable(true);
  // 比較選択補助は比較モード中のみ意味があるので、モード OFF 時は disabled。
  QAction* selDifferAct   = addCmd(viewMenu, "compare.select_differ",    tr("Select Differ rows"));
  QAction* selOnlyHereAct = addCmd(viewMenu, "compare.select_only_here", tr("Select Only-Here rows"));
  QAction* selNewerAct    = addCmd(viewMenu, "compare.select_newer",     tr("Select Newer-Than-Other rows"));
  selDifferAct->setEnabled(false);
  selOnlyHereAct->setEnabled(false);
  selNewerAct->setEnabled(false);
  connect(m_fileManagerPanel, &FileManagerPanel::directoryCompareChanged,
          this, [this, selDifferAct, selOnlyHereAct, selNewerAct](bool active) {
    if (m_compareAction) {
      QSignalBlocker blocker(m_compareAction);
      m_compareAction->setChecked(active);
    }
    if (m_statusCompareLabel) {
      m_statusCompareLabel->setText(active ? tr("Compare: ON") : QString());
    }
    selDifferAct->setEnabled(active);
    selOnlyHereAct->setEnabled(active);
    selNewerAct->setEnabled(active);
    updateWindowTitle();
  });
  viewMenu->addSeparator();
  addCmd(viewMenu, "view.file", tr("View File"));
  addCmd(viewMenu, "view.choose", tr("Open With Viewer..."));
  // 表示モード切替: List / Thumbnail S/M/L を 4 個のメニュー項目で直接選択。
  // Cmd+1〜4 のショートカットがメニュー横に表示される。aboutToShow で現在
  // モードに ✓ を付ける。
  QMenu* viewModeMenu = viewMenu->addMenu(tr("View Mode"));
  QAction* vmListAct  = addCmd(viewModeMenu, "view.list",
                                tr("List"));
  QAction* vmSAct     = addCmd(viewModeMenu, "view.thumbnail_small",
                                tr("Thumbnails (Small)"));
  QAction* vmMAct     = addCmd(viewModeMenu, "view.thumbnail_medium",
                                tr("Thumbnails (Medium)"));
  QAction* vmLAct     = addCmd(viewModeMenu, "view.thumbnail_large",
                                tr("Thumbnails (Large)"));
  vmListAct->setIcon(QIcon(QStringLiteral(":/icons/toolbar/view-list-rows.svg")));
  vmSAct->setIcon   (QIcon(QStringLiteral(":/icons/toolbar/view-grid-3.svg")));
  vmMAct->setIcon   (QIcon(QStringLiteral(":/icons/toolbar/view-grid-2.svg")));
  vmLAct->setIcon   (QIcon(QStringLiteral(":/icons/toolbar/view-grid-1.svg")));
  for (QAction* a : { vmListAct, vmSAct, vmMAct, vmLAct }) a->setCheckable(true);
  QActionGroup* vmGroup = new QActionGroup(this);
  vmGroup->setExclusive(true);
  vmGroup->addAction(vmListAct);
  vmGroup->addAction(vmSAct);
  vmGroup->addAction(vmMAct);
  vmGroup->addAction(vmLAct);
  connect(viewMenu, &QMenu::aboutToShow, this, [this, vmListAct, vmSAct, vmMAct, vmLAct]() {
    const auto* pane = m_fileManagerPanel ? m_fileManagerPanel->activePane() : nullptr;
    const ListViewMode cur = pane ? pane->viewMode() : ListViewMode::List;
    QSignalBlocker bL(vmListAct), bS(vmSAct), bM(vmMAct), bLg(vmLAct);
    vmListAct->setChecked(cur == ListViewMode::List);
    vmSAct->setChecked   (cur == ListViewMode::ThumbnailSmall);
    vmMAct->setChecked   (cur == ListViewMode::ThumbnailMedium);
    vmLAct->setChecked   (cur == ListViewMode::ThumbnailLarge);
  });

  addCmd(viewMenu, "view.toggle_log", tr("Toggle Log Pane"));
  addCmd(viewMenu, "view.quick_filter", tr("Quick Filter"));
  // ツールバー表示のトグル。aboutToShow で Settings の現状を反映させる。
  m_toolbarMenuAction = addCmd(viewMenu, "view.toggle_toolbar", tr("Toolbar"));
  m_toolbarMenuAction->setCheckable(true);
  m_toolbarMenuAction->setChecked(Settings::instance().showToolbar());
  connect(viewMenu, &QMenu::aboutToShow, this, [this]() {
    if (m_toolbarMenuAction) {
      QSignalBlocker blocker(m_toolbarMenuAction);
      m_toolbarMenuAction->setChecked(Settings::instance().showToolbar());
    }
  });
  // ビュアー表示モード (Inline / External) のトグル。チェック付きでメニューに
  // 表示し、aboutToShow で Settings の現状を反映する。
  QAction* viewerModeAction = addCmd(viewMenu, "view.toggle_viewer_mode",
                                     tr("Use External Viewer Window"));
  viewerModeAction->setCheckable(true);
  viewerModeAction->setChecked(Settings::instance().viewerMode() == ViewerMode::External);
  connect(viewMenu, &QMenu::aboutToShow, this, [viewerModeAction]() {
    QSignalBlocker blocker(viewerModeAction);
    viewerModeAction->setChecked(Settings::instance().viewerMode() == ViewerMode::External);
  });

  // Tools (外部アプリ連携)
  // 慣例として Edit と View の間に置きたいが、createMenus は単方向に並べていく
  // 都合で View の直後に置く。Total Commander / Double Commander でも実質
  // 「右端の Help より左」に Tools が並んでいれば違和感は少ない。
  m_toolsMenu = bar->addMenu(tr("&Tools"));
  rebuildToolsMenu();

  // Go
  QMenu* goMenu = bar->addMenu(tr("&Go"));
  addCmd(goMenu, "navigate.parent", tr("Parent Directory"));
  addCmd(goMenu, "navigate.home",   tr("Jump to Top"));
  addCmd(goMenu, "navigate.end",    tr("Jump to Bottom"));
  goMenu->addSeparator();
  addCmd(goMenu, "file.search",     tr("Search Files..."));
  addCmd(goMenu, "history.show",    tr("History..."));

  // Bookmarks
  QMenu* bookmarksMenu = bar->addMenu(tr("&Bookmarks"));
  addCmd(bookmarksMenu, "bookmark.toggle", tr("Toggle Bookmark"));
  addCmd(bookmarksMenu, "bookmark.list",   tr("Bookmarks..."));

  // Help
  QMenu* helpMenu = bar->addMenu(tr("&Help"));
  // ショートカット一覧 (`?` キー)
  addCmd(helpMenu, "help.shortcuts", tr("Keyboard Shortcuts"), /*global=*/true);
  helpMenu->addSeparator();
  // 手動アップデートチェック (macOS の慣習で menuRole = ApplicationSpecific
  // を当てると標準で Help メニューに残る。Help → "Check for Updates..." は
  // Sparkle 系アプリの慣習で違和感ない位置)。
  addCmd(helpMenu, "app.check_for_updates", tr("Check for Updates..."), /*global=*/true);
  QAction* aboutAction = new QAction(tr("About farman..."), this);
  aboutAction->setMenuRole(QAction::AboutRole);
  connect(aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);
  helpMenu->addAction(aboutAction);
}

void MainWindow::rebuildToolsMenu() {
  if (!m_toolsMenu) return;

  // 前回 rebuild で FileManagerPanel に追加したショートカット用 QAction を
  // 取り除いてから破棄する。これをやらないと、同一ショートカット (例: T) が
  // 複数の QAction に紐付いて "Ambiguous shortcut overload" になる。
  // (m_toolsMenu->clear() は menu からは外すが、FileManagerPanel の addAction()
  // 経由で生きている action は破棄されない。)
  for (QAction* a : m_userCmdActions) {
    if (m_fileManagerPanel) m_fileManagerPanel->removeAction(a);
    a->deleteLater();
  }
  m_userCmdActions.clear();

  // メニュー側の placeholder 等は menu が parent なので clear() で破棄される。
  m_toolsMenu->clear();

  // Tools メニュー専用に「ショートカットを最初の 1 件だけ採用」する
  // ローカル addCmd 相当 (createMenus の lambda と同じ流儀)。
  // ビュアー表示中は外部アプリを呼ぶ意味が薄いので global=false 相当
  // (= ファイルマネージャパネル限定で Action 経由のキーが効く) にしておく。
  const QList<UserCommand>& cmds = UserCommandManager::instance().commands();
  bool addedAny = false;
  for (const UserCommand& cmd : cmds) {
    if (!cmd.showInToolsMenu) continue;

    QAction* action = new QAction(cmd.name, m_toolsMenu);
    const QList<QKeySequence> keys =
      KeyBindingManager::instance().keysForCommand(cmd.id);
    if (!keys.isEmpty()) {
      action->setShortcut(keys.first());
      action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
      m_fileManagerPanel->addAction(action);
      m_userCmdActions.append(action);
    }
    const QString id = cmd.id;
    connect(action, &QAction::triggered, this, [id, this]() {
      QString err;
      if (!UserCommandManager::instance().run(id, &err)) {
        warn(this, tr("External command failed"), err);
      }
    });
    m_toolsMenu->addAction(action);
    addedAny = true;
  }

  if (!addedAny) {
    // Empty menu はメニューバーにも出ない実装が多いので、placeholder を入れる。
    QAction* placeholder = new QAction(tr("(no external commands configured)"), m_toolsMenu);
    placeholder->setEnabled(false);
    m_toolsMenu->addAction(placeholder);
  }
}

void MainWindow::createMainToolBar() {
  // 既に作成済みなら何もしない (構築フローで一度だけ呼ぶ前提)。
  if (m_toolbar) return;

  m_toolbar = new QToolBar(tr("Main Toolbar"), this);
  m_toolbar->setObjectName(QStringLiteral("mainToolBar"));
  m_toolbar->setMovable(false);
  // 表示スタイルは Settings::toolbarStyle() に従う。Icon / Text / IconBesideText
  // の 3 値で、Settings → General のコンボから切替。設定変更時は
  // applyToolbarVisibility() 経由で再適用する。アイコン素材は
  // :/icons/toolbar/<name>.svg (Lucide スタイル / monochrome 24x24)。
  m_toolbar->setToolButtonStyle(
    toQtToolButtonStyle(Settings::instance().toolbarStyle()));
  m_toolbar->setIconSize(QSize(20, 20));
  // フォーカス枠 + checkable トグルの押下状態 + ホバーをまとめてスタイリング。
  // (utils/EnterClickFilter.h の toolbarStyleSheet() に共通定義あり)
  m_toolbar->setStyleSheet(toolbarStyleSheet());

  // 1 ボタン分の生成ヘルパ。CommandRegistry::execute(id) を呼ぶだけの
  // QAction を作って toolbar に追加。tooltip にはラベル + キーバインドを
  // 表示する。menu の addCmd と異なり、ショートカットは setShortcut せず
  // メニュー側 / eventFilter 側の登録を流用する (重複登録 = "Ambiguous
  // shortcut overload" を避けるため)。
  auto addBtn = [this](const QString& id, const QString& label,
                       const QString& iconName) -> QAction* {
    QAction* a = new QAction(label, m_toolbar);
    if (!iconName.isEmpty()) {
      a->setIcon(QIcon(QStringLiteral(":/icons/toolbar/") + iconName));
    }
    const QList<QKeySequence> keys =
      KeyBindingManager::instance().keysForCommand(id);
    if (!keys.isEmpty()) {
      a->setToolTip(QStringLiteral("%1 (%2)")
                      .arg(label, keys.first().toString(QKeySequence::NativeText)));
    } else {
      a->setToolTip(label);
    }
    connect(a, &QAction::triggered, this, [id]() {
      CommandRegistry::instance().execute(id);
    });
    m_toolbar->addAction(a);
    return a;
  };

  // 配置: 機能の近いものを 7 グループにセパレータで区切る。
  //   1. 作成系 (新規ファイル / 新規ディレクトリ)
  //   2. 操作系 (Copy / Move / Delete / Rename)
  //   3. 表示・絞り込み系 (View ファイル / Sort & Filter / Search)
  //   4. ナビゲーション (Bookmarks / History)
  //   5. 外部アプリ (Terminal / Editor — UserCommand 経由)
  //   6. 表示モードトグル (Single Pane / Sync Browse / Log) ← checkable
  //   7. ヘルプ・設定 (Shortcuts / Settings)
  addBtn("file.newfile",            tr("New File"),     QStringLiteral("new-file.svg"));
  addBtn("file.mkdir",              tr("New Dir"),      QStringLiteral("new-dir.svg"));
  m_toolbar->addSeparator();
  addBtn("file.copy",               tr("Copy"),         QStringLiteral("copy.svg"));
  addBtn("file.move",               tr("Move"),         QStringLiteral("move.svg"));
  addBtn("file.delete",             tr("Delete"),       QStringLiteral("delete.svg"));
  addBtn("file.rename",             tr("Rename"),       QStringLiteral("rename.svg"));
  m_toolbar->addSeparator();
  addBtn("view.file",               tr("Viewer"),       QStringLiteral("view-file.svg"));
  // ビュアー外部表示モード切替 — 「ビュアー」ボタンの隣に置く方が自然なので
  // 従来のトグル群 (Single Pane / Sync Browse / Log) ではなくここに配置する。
  // checkable: ON = ビュアーを別ウィンドウで開く設定。Settings::settingsChanged
  // 経由で他経路 (Settings ダイアログ / メニュー / キーバインド) からの変更も
  // 表示状態に反映される。
  QAction* externalViewerAct = addBtn("view.toggle_viewer_mode",
                                      tr("Use External Viewer Window"),
                                      QStringLiteral("external-viewer.svg"));
  externalViewerAct->setCheckable(true);
  externalViewerAct->setChecked(Settings::instance().viewerMode() == ViewerMode::External);
  connect(&Settings::instance(), &Settings::settingsChanged,
          this, [externalViewerAct]() {
    QSignalBlocker b(externalViewerAct);
    externalViewerAct->setChecked(Settings::instance().viewerMode() == ViewerMode::External);
  });
  addBtn("pane.sort_filter",        tr("Sort && Filter"), QStringLiteral("sort-filter.svg"));
  addBtn("file.search",             tr("Search"),       QStringLiteral("search.svg"));
  m_toolbar->addSeparator();
  addBtn("bookmark.list",           tr("Bookmarks"),    QStringLiteral("bookmarks.svg"));
  addBtn("history.show",            tr("History"),      QStringLiteral("history.svg"));
  m_toolbar->addSeparator();
  // 外部アプリは UserCommand 経由 (terminal / editor の組み込みエントリ)。
  // ユーザーが Settings → External Apps で program を変更すれば、ここから
  // 起動するアプリも自動で切り替わる (ユーザー定義コマンドはまだツールバー
  // からは出さない方針)。
  addBtn("user.cmd.terminal",       tr("Terminal"),     QStringLiteral("terminal.svg"));
  addBtn("user.cmd.editor",         tr("Editor"),       QStringLiteral("editor.svg"));
  m_toolbar->addSeparator();
  // ── トグル系 (押下状態を保持) ────────────────────────────
  // Single Pane / Sync Browse / Log は機能の ON/OFF を反映するため checkable に
  // する。triggered で命令が走るのは他と同じだが、状態は FileManagerPanel の
  // シグナル経由で反映する (toolbar から押した場合 / メニューから押した場合 /
  // キーバインドから押した場合 のいずれでも同じ経路で更新される)。
  // QAction::setChecked は changed シグナルを発火しないように QSignalBlocker で
  // 包み、triggered ループを断ち切る。
  QAction* singlePaneAct = addBtn("pane.toggle_single", tr("Single Pane"),
                                  QStringLiteral("single-pane.svg"));
  singlePaneAct->setCheckable(true);
  singlePaneAct->setChecked(m_fileManagerPanel->isSinglePaneMode());

  // Preview レイアウト切替ボタン (Single Pane と排他)。
  QAction* previewPaneAct = addBtn("pane.toggle_preview", tr("Preview Pane"),
                                   QStringLiteral("preview-pane.svg"));
  previewPaneAct->setCheckable(true);
  previewPaneAct->setChecked(m_fileManagerPanel->isPreviewMode());

  // 3 値 LayoutMode 変化を 2 つの checkable ボタンに排他で反映する。
  connect(m_fileManagerPanel, &FileManagerPanel::layoutModeChanged,
          this, [singlePaneAct, previewPaneAct](LayoutMode mode) {
    QSignalBlocker bs(singlePaneAct);
    QSignalBlocker bp(previewPaneAct);
    singlePaneAct->setChecked(mode == LayoutMode::Single);
    previewPaneAct->setChecked(mode == LayoutMode::Preview);
  });

  QAction* syncBrowseAct = addBtn("pane.sync_browse_toggle", tr("Sync Browse"),
                                  QStringLiteral("sync-browse.svg"));
  syncBrowseAct->setCheckable(true);
  syncBrowseAct->setChecked(m_fileManagerPanel->isSyncBrowseEnabled());
  connect(m_fileManagerPanel, &FileManagerPanel::syncBrowseChanged,
          this, [syncBrowseAct](bool on) {
    QSignalBlocker b(syncBrowseAct);
    syncBrowseAct->setChecked(on);
  });

  // ディレクトリ比較トグル。コマンドは「ON ↔ OFF」のトグルなので checkable に
  // して、現在の比較モード状態を反映させる。
  QAction* compareAct = addBtn("view.compare_directories", tr("Compare Directories"),
                               QStringLiteral("compare-directories.svg"));
  compareAct->setCheckable(true);
  compareAct->setChecked(m_fileManagerPanel->isDirectoryCompareActive());
  connect(m_fileManagerPanel, &FileManagerPanel::directoryCompareChanged,
          this, [compareAct](bool on) {
    QSignalBlocker b(compareAct);
    compareAct->setChecked(on);
  });
  // クリック時、Qt は triggered の前に checkable QAction を自動でトグルする。
  // startDirectoryCompare はモーダルダイアログを出すので、ユーザーが Esc 等で
  // キャンセルすると directoryCompareChanged が発火せず、チェック状態だけ
  // ずれてしまう (ボタンが押下中の見た目のまま残る)。コマンド実行 *後* に
  // 「実際のモード状態」と照合して同期し直す。
  connect(compareAct, &QAction::triggered, this, [this, compareAct]() {
    QSignalBlocker b(compareAct);
    compareAct->setChecked(m_fileManagerPanel->isDirectoryCompareActive());
  });

  QAction* logAct = addBtn("view.toggle_log", tr("Log"),
                           QStringLiteral("log.svg"));
  logAct->setCheckable(true);
  logAct->setChecked(m_fileManagerPanel->isLogPaneVisible());
  connect(m_fileManagerPanel, &FileManagerPanel::logPaneVisibleChanged,
          this, [logAct](bool visible) {
    QSignalBlocker b(logAct);
    logAct->setChecked(visible);
  });

  // サムネイル表示モードはペイン固有の状態 (左右で独立にトグル) なので、
  // ツールバーには出さず、各 FileListPane のフッタにモード切替ボタン + サイズ
  // 切替ボタンを並べる構成にしている (ペインごとの「sort / filter / view mode /
  // size」を 1 か所で一望できる)。ツールバーは両ペイン / アプリ全体に関わる
  // 操作 (Single Pane / Sync Browse / Compare / Log etc.) に限定する。

  m_toolbar->addSeparator();
  addBtn("help.shortcuts",          tr("Shortcuts"),    QStringLiteral("shortcuts.svg"));
  addBtn("app.settings",            tr("Settings"),     QStringLiteral("settings.svg"));

  // 右端に「ツールバーを閉じる (×)」ボタン。残りスペースを expanding な
  // QWidget で埋めて、その後ろに追加することで右寄せを実現する。
  // クリックで Settings::showToolbar を false にして即時非表示。再表示は
  // View → Toolbar / Settings → Show toolbar / view.toggle_toolbar コマンド
  // のいずれからでも可能。
  {
    auto* spacer = new QWidget(m_toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);

    QAction* closeAct = new QAction(tr("Hide toolbar"), m_toolbar);
    closeAct->setIcon(QIcon(QStringLiteral(":/icons/toolbar/close.svg")));
    closeAct->setToolTip(tr(
      "Hide the toolbar. Re-show it from View → Toolbar or Settings → General."));
    connect(closeAct, &QAction::triggered, this, [this]() {
      auto& s = Settings::instance();
      s.setShowToolbar(false);
      s.save();
      applyToolbarVisibility();
    });
    m_toolbar->addAction(closeAct);
  }

  addToolBar(Qt::TopToolBarArea, m_toolbar);

  // Tab フォーカス中のボタンで Enter / Return を押されたら、そのボタンの
  // クリックとして扱う。QToolBar が QAction から内部生成した QToolButton も
  // findChildren で拾える。後で動的に widget が増えるケースは無いので
  // 1 回だけ install すれば十分。
  auto* clickFilter = new EnterClickFilter(this);
  clickFilter->installOnButtonsIn(m_toolbar);
}

void MainWindow::applyToolbarVisibility() {
  if (!m_toolbar) return;
  m_toolbar->setVisible(Settings::instance().showToolbar());
  if (m_toolbarMenuAction) {
    QSignalBlocker blocker(m_toolbarMenuAction);
    m_toolbarMenuAction->setChecked(Settings::instance().showToolbar());
  }
  // ボタン表示スタイル (Icon / Text / IconBesideText) も Settings から
  // 再適用する。Settings → General ダイアログから変更したケースの追従。
  m_toolbar->setToolButtonStyle(
    toQtToolButtonStyle(Settings::instance().toolbarStyle()));
}

void MainWindow::showAboutDialog() {
  const QString version = QStringLiteral(QT_STRINGIFY(FARMAN_VERSION));
  inform(this, tr("About farman"),
    tr("<b>farman</b> %1<br><br>"
       "Copyright &copy; Mashsoft Inc.<br>"
       "<a href=\"https://www.mashsoft.co.jp\">https://www.mashsoft.co.jp</a>")
      .arg(version));
}

void MainWindow::ensureUpdateChecker() {
  if (m_updateChecker) return;
  m_updateChecker = new UpdateChecker(this);
  connect(m_updateChecker, &UpdateChecker::finished, this,
          &MainWindow::onUpdateCheckFinished);
}

void MainWindow::checkForUpdatesManually() {
  ensureUpdateChecker();
  if (m_updateChecker->isChecking()) return;
  m_updateCheckIsManual = true;
  m_updateChecker->checkLatest();
}

void MainWindow::maybeCheckForUpdatesOnStartup() {
  auto& s = Settings::instance();
  if (!s.autoUpdateCheckOnStartup()) return;
  // 24h スロットル: 前回チェックから 24 時間経っていなければスキップ。
  const QDateTime last = s.autoUpdateLastCheckedAt();
  if (last.isValid() && last.secsTo(QDateTime::currentDateTime()) < 24 * 3600) {
    return;
  }
  // 起動直後にネットワーク I/O が走るのでメインループが安定してから少し遅延。
  QTimer::singleShot(1500, this, [this]() {
    ensureUpdateChecker();
    if (m_updateChecker->isChecking()) return;
    m_updateCheckIsManual = false;
    m_updateChecker->checkLatest();
  });
}

void MainWindow::onUpdateCheckFinished(bool ok, const ReleaseInfo& info,
                                        bool isNewer,
                                        const QString& errorReason) {
  // lastCheckedAt は成功時のみ更新 (失敗はリトライ可能なので前回値を残す)。
  if (ok) {
    auto& s = Settings::instance();
    s.setAutoUpdateLastCheckedAt(QDateTime::currentDateTime());
    s.save();
  }
  const bool manual = m_updateCheckIsManual;
  m_updateCheckIsManual = false;

  if (!ok) {
    Logger::instance().info(tr("Update check failed: %1").arg(errorReason));
    // 失敗時は manual のときだけ理由を見せる (自動チェックはサイレント、SPEC 通り)。
    if (!manual) return;
    QString userMessage;
    if (errorReason == QLatin1String("no_published_release")
        || errorReason.startsWith(QLatin1String("latest is draft/prerelease"))) {
      // GitHub 側に stable release が無い / draft / prerelease のみ → 利用者から
      // 見れば「これ以上新しい版は無い」と等価なので「最新を使用中」と表示。
      userMessage = tr("You're using the latest version of farman.");
    } else if (errorReason.startsWith(QLatin1String("skipped: dev build"))) {
      userMessage = tr("This is a development build.");
    } else {
      userMessage = tr("Could not check for updates: %1").arg(errorReason);
    }
    inform(this, tr("Check for Updates"), userMessage);
    return;
  }

  Logger::instance().info(
    tr("Update check: latest=%1, current=%2, newer=%3")
      .arg(info.version,
            QStringLiteral(QT_STRINGIFY(FARMAN_VERSION)),
            isNewer ? QStringLiteral("yes") : QStringLiteral("no")));

  if (!isNewer) {
    // 同等 or 古い (= dev build 等)。manual のときだけ「最新です」を出す。
    if (manual) {
      inform(this, tr("Check for Updates"),
             tr("You're using the latest version of farman."));
    }
    return;
  }

  // 新版あり。自動チェックの場合、ユーザーが Skip This Version で記録した
  // バージョンなら表示を抑止する。manual のときは Skip 状態でも常に出す。
  auto& s = Settings::instance();
  const bool isSkipped = s.autoUpdateSkippedVersions().contains(info.version);
  if (!manual && isSkipped) {
    Logger::instance().info(
      tr("Update %1 is in skip list, suppressing notification").arg(info.version));
    return;
  }

  // silent モード ON のとき (Settings から有効化されている) は、確認 dialog を
  // 出さずに即ダウンロードを開始する。SPEC.md "自動アップデート" 節の
  // "silent モード" 相当。manual チェック経由のときは silent でも一応 dialog を
  // 見せる (= 明示的に「今チェック」を押したユーザーには結果と選択肢を見せる)。
  if (!manual && s.autoUpdateSilent()) {
    Logger::instance().info(
      tr("Silent auto-update: downloading %1").arg(info.version));
    startUpdateDownload(info);
    return;
  }

  // 通知ダイアログをポップアップ。
  UpdateAvailableDialog dlg(QStringLiteral(QT_STRINGIFY(FARMAN_VERSION)), info, this);
  dlg.exec();
  switch (dlg.lastAction()) {
    case UpdateAvailableDialog::Action::UpdateNow: {
      startUpdateDownload(info);
      break;
    }
    case UpdateAvailableDialog::Action::Skip: {
      s.addAutoUpdateSkippedVersion(info.version);
      s.save();
      Logger::instance().info(
        tr("User chose to skip update %1").arg(info.version));
      break;
    }
    case UpdateAvailableDialog::Action::RemindLater:
    case UpdateAvailableDialog::Action::Closed:
    default:
      // 何もしない: 次回 24h 後のチェックで再表示される
      break;
  }
}

void MainWindow::startUpdateDownload(const ReleaseInfo& info) {
  // 進捗ダイアログを 1 個用意。UpdateDownloader の phaseChanged / progress を
  // 反映し、finished で閉じる。キャンセルボタンは現状未実装 (中断ロジックを
  // 入れるなら QNetworkReply::abort() を経由する必要)。
  auto* progressDlg = new QProgressDialog(this);
  progressDlg->setWindowTitle(tr("Updating farman..."));
  progressDlg->setLabelText(tr("Preparing download..."));
  progressDlg->setRange(0, 0);  // 不確定スピナー (downloadProgress で確定値に切替)
  progressDlg->setMinimumDuration(0);
  progressDlg->setAutoClose(false);
  progressDlg->setAutoReset(false);
  progressDlg->setCancelButton(nullptr);  // Phase C では中断不可
  progressDlg->setAttribute(Qt::WA_DeleteOnClose);

  auto* dl = new UpdateDownloader(this);
  connect(dl, &UpdateDownloader::phaseChanged, progressDlg,
          &QProgressDialog::setLabelText);
  connect(dl, &UpdateDownloader::progress, progressDlg,
          [progressDlg](qint64 received, qint64 total) {
    if (total > 0) {
      progressDlg->setRange(0, 100);
      progressDlg->setValue(static_cast<int>((received * 100) / total));
    } else {
      progressDlg->setRange(0, 0);  // 引き続き不確定
    }
  });
  connect(dl, &UpdateDownloader::finished, this,
          [this, dl, progressDlg](bool ok, const QString& errorReason) {
    progressDlg->close();
    dl->deleteLater();
    if (!ok) {
      Logger::instance().warn(tr("Update download failed: %1").arg(errorReason));
      warn(this, tr("Update Failed"),
           tr("Could not install the update: %1").arg(errorReason));
      return;
    }
    Logger::instance().info(tr("Update install launched, exiting"));
    // インストーラ / ヘルパースクリプトを startDetached したので、自プロセスを
    // 終了させて旧バイナリを開放する。
    QApplication::quit();
  });

  progressDlg->show();
  dl->start(info);
}

void MainWindow::toggleShortcutList() {
  // 開いていれば閉じる、閉じていれば開く。初回のみ生成する。
  if (m_shortcutListDialog && m_shortcutListDialog->isVisible()) {
    m_shortcutListDialog->close();
    return;
  }
  if (!m_shortcutListDialog) {
    m_shortcutListDialog = new ShortcutListDialog(this);
  } else {
    m_shortcutListDialog->rebuild();  // キーバインドが変わっている可能性
  }
  // 初期位置: メインウィンドウの右側に並べて配置。
  const QRect main = geometry();
  const QSize dlgSize = m_shortcutListDialog->size();
  int x = main.x() + main.width() + 12;
  int y = main.y();
  // 画面外に出ないよう、はみ出すなら main 右端からの距離を縮める。
  if (auto* scr = screen()) {
    const QRect avail = scr->availableGeometry();
    if (x + dlgSize.width() > avail.right()) {
      x = qMax(avail.right() - dlgSize.width(), avail.left());
    }
    if (y + dlgSize.height() > avail.bottom()) {
      y = qMax(avail.bottom() - dlgSize.height(), avail.top());
    }
  }
  m_shortcutListDialog->move(x, y);
  m_shortcutListDialog->show();
  m_shortcutListDialog->raise();
  m_shortcutListDialog->activateWindow();
}

void MainWindow::showSettingsDialog() {
  SettingsDialog* dialog = new SettingsDialog(
    m_fileManagerPanel->leftPath(),
    m_fileManagerPanel->rightPath(),
    size(), pos(),
    this
  );
  connect(dialog, &SettingsDialog::settingsChanged, this, &MainWindow::onSettingsChanged);
  dialog->exec();
  delete dialog;
}

void MainWindow::onSettingsChanged() {
  // Settings have been changed and saved
  // Apply the new settings to the file manager panel
  m_fileManagerPanel->applySettings();

  // ログ表示・ファイル出力の状態を Settings に追従
  auto& s = Settings::instance();
  m_fileManagerPanel->setLogPaneVisible(s.logVisible());
  m_fileManagerPanel->setLogPaneHeight(s.logPaneHeight());
  Logger::instance().setFileOutput(s.logToFile(), s.logDirectory(), s.logRetentionDays());

  // ツールバーの表示も Settings 側からのトグルに追従させる。
  applyToolbarVisibility();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  auto& settings = Settings::instance();

  // Show confirmation dialog if enabled
  if (settings.confirmOnExit()) {
    if (!confirm(this, tr("Confirm Exit"),
                 tr("Are you sure you want to exit farman?"))) {
      event->ignore();
      return;
    }
  }

  // Save last window size and position
  settings.setLastWindowSize(size());
  settings.setLastWindowPosition(pos());

  // Persist each pane's current path so InitialPathMode::LastSession works.
  auto storeLastPath = [&settings](PaneType type, FileListPane* pane) {
    PaneSettings s = settings.paneSettings(type);
    s.path = pane->currentPath();
    settings.setPaneSettings(type, s);
  };
  storeLastPath(PaneType::Left,  m_fileManagerPanel->leftPane());
  storeLastPath(PaneType::Right, m_fileManagerPanel->rightPane());

  // 履歴の永続化: ON のときは現在の履歴を保存、OFF のときは空で上書きして残留を消す
  auto storeHistory = [&settings, this](PaneType type) {
    if (settings.persistHistory()) {
      settings.setPaneHistory(type, m_fileManagerPanel->history(type).entries());
    } else {
      settings.setPaneHistory(type, {});
    }
  };
  storeHistory(PaneType::Left);
  storeHistory(PaneType::Right);

  // 終了時の LayoutMode を永続化 (次回起動時に同じレイアウトで開く)。
  settings.setLayoutMode(m_fileManagerPanel->layoutMode());

  settings.save();

  event->accept();
  QMainWindow::closeEvent(event);
}

} // namespace Farman
