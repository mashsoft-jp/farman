#include "MediaViewerWindow.h"
#include "MediaView.h"
#include "utils/CancellableLoadPage.h"

#include <QAction>
#include <QCloseEvent>
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QScreen>
#include <QShowEvent>
#include <QStatusBar>
#include <QToolButton>

namespace Farman {

MediaViewerWindow::MediaViewerWindow(const QString& filePath,
                                     const QString& displayPath,
                                     QWidget* parent)
  : QMainWindow(parent)
  , m_filePath(filePath)
  , m_displayPath(displayPath.isEmpty() ? filePath : displayPath)
{
  setupUi();

  // QMediaPlayer のロードは非同期なので、他ビュアーのような
  // CancellableLoadPage は挟まずに即ビューを出してロード結末だけログする。
  // Inline 埋め込み時は ViewerPanel::openPluginFile が createViewer 成功時点で
  // 成功ログを出すので、こちらでは失敗 (InvalidMedia / エラー) だけ追記する。
  // トップレベル (External) のときは成功・失敗の両方をログする。
  connect(m_mediaView, &MediaView::loadFinished, this, [this](bool ok) {
    if (isWindow()) {
      logViewerLoadResult(QStringLiteral("Media, external"),
                          m_displayPath, ok, false);
    } else if (!ok) {
      logViewerLoadResult(QStringLiteral("Media"),
                          m_displayPath, false, false);
    }
  });
  m_mediaView->openFile(filePath);
  // External モードで開いた直後からキー操作 (Space / 矢印 / M / L / F) が
  // 効くよう明示的にフォーカスを当てる (ImageViewerWindow と同じ作法)。
  // Inline 埋め込み時は ViewerPanel::openPluginFile が改めてフォーカスを移す。
  m_mediaView->setFocus(Qt::OtherFocusReason);
}

void MediaViewerWindow::setupUi() {
  QFileInfo fileInfo(m_displayPath);
  setWindowTitle(QString("Media Viewer - %1").arg(fileInfo.fileName()));

  m_mediaView = new MediaView(this);
  setCentralWidget(m_mediaView);

  // External (トップレベル) のときだけ、自前 statusBar にメディア情報を右寄せで
  // 表示する。Inline 埋め込み時は showEvent で隠し、本体ステータスバーに任せる。
  auto* statusLabel = new QLabel(this);
  statusBar()->addPermanentWidget(statusLabel);
  connect(m_mediaView, &MediaView::statusInfoChanged, statusLabel, &QLabel::setText);
  // Inline 埋め込み時に本体ステータスバーへ中継できるよう、MediaView の更新を転送。
  connect(m_mediaView, &MediaView::statusInfoChanged,
          this,        &MediaViewerWindow::statusInfoChanged);
  // External モードの activateWindow / Inline モードの
  // ViewerPanel::setFocusProxy(view) のどちらでも、ウィンドウ宛のフォーカスが
  // キー処理を持つ MediaView 本体へ届くようにする。
  setFocusProxy(m_mediaView);

  // 「ウィンドウサイズを動画にあわせる」ボタン (ImageViewerWindow と同じ作法)。
  // External のみ意味があるので、表示は showEvent で isWindow() に応じて切替える。
  auto* fitWinBtn = new QToolButton(this);
  fitWinBtn->setIcon(QIcon(QStringLiteral(":/icons/toolbar/fit-window-to-image.svg")));
  fitWinBtn->setIconSize(QSize(20, 20));
  // ショートカットは W キー (keyPressEvent で処理)。Cmd/Ctrl 系はメニュー
  // ショートカットと衝突して発火しないため、修飾キー無しのアルファベットで受ける。
  fitWinBtn->setFocusPolicy(Qt::StrongFocus);
  fitWinBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
  fitWinBtn->setToolTip(tr(
    "Fit window to video (W) — resize this window so the video is shown "
    "at its natural size. If the video is larger than the screen, the window "
    "is clamped to the available screen area."));
  connect(fitWinBtn, &QToolButton::clicked, this,
          &MediaViewerWindow::fitWindowToVideo);
  m_fitWindowAction = m_mediaView->addToolbarWidget(fitWinBtn);

  resize(900, 600);
}

void MediaViewerWindow::closeEvent(QCloseEvent* event) {
  // フルスクリーン中に閉じると、破棄途中の QVideoWidget 再ペアレントで
  // createWinId 無限再帰クラッシュが起きる。まだ生存しているこの時点で
  // フルスクリーンを解除しておく。
  if (m_mediaView) m_mediaView->exitFullscreen();
  QMainWindow::closeEvent(event);
}

void MediaViewerWindow::keyPressEvent(QKeyEvent* event) {
  // Esc / Enter / Return でウィンドウを閉じる (External モード)。
  // Inline モードで ViewerPanel に埋め込まれている間 (= 非トップレベル) は
  // 閉じずに親へ伝播させ、MainWindow 側の「ファイラに戻る」処理に任せる。
  if (event->key() == Qt::Key_Escape ||
      event->key() == Qt::Key_Return ||
      event->key() == Qt::Key_Enter) {
    if (isWindow()) {
      close();
      return;
    }
  }
  // W: ウィンドウサイズを動画に合わせる (External のみ。fitWindowToVideo が
  // isWindow() を確認する)。Cmd/Ctrl 系はメニューショートカットと衝突するため
  // アルファベットキーで受ける。
  if (event->key() == Qt::Key_W && isWindow()) {
    fitWindowToVideo();
    return;
  }
  QMainWindow::keyPressEvent(event);
}

QString MediaViewerWindow::statusInfo() const {
  return m_mediaView ? m_mediaView->statusInfo() : QString();
}

void MediaViewerWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  // Inline 埋め込み (非トップレベル) では自前 statusBar を隠し、本体ステータス
  // バーに任せる。External (トップレベル) のときだけ表示する。
  statusBar()->setVisible(isWindow());
  // 「ウィンドウサイズを動画にあわせる」も External のときだけ意味があるので、
  // Inline では非表示にする。
  if (m_fitWindowAction) m_fitWindowAction->setVisible(isWindow());
}

void MediaViewerWindow::fitWindowToVideo() {
  if (!m_mediaView || !isWindow()) return;
  const QSize vidSize = m_mediaView->naturalVideoSize();
  if (!vidSize.isValid() || vidSize.isEmpty()) return;

  // fit 中は 100% (実寸)、手動ズーム時は設定倍率を基準にする。
  const int zoom = qMax(1, m_mediaView->windowFitZoomPercent());
  const QSize scaled(qMax(1, vidSize.width()  * zoom / 100),
                     qMax(1, vidSize.height() * zoom / 100));

  // 「ウィンドウ全体 - 動画表示エリア (viewport)」が chrome (枠 + ツールバー +
  // statusBar + 余白)。これはズームの影響を受けないので固定値として扱う。
  const QSize videoArea = m_mediaView->videoAreaSize();
  const QSize chrome = (videoArea.isValid() && !videoArea.isEmpty())
                         ? size() - videoArea
                         : size() - m_mediaView->size();

  QSize target = scaled + chrome;
  if (auto* scr = screen()) {
    target = target.boundedTo(scr->availableGeometry().size());
  }
  target = target.expandedTo(QSize(320, 240));
  resize(target);
}

} // namespace Farman
