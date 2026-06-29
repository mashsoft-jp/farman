#include "MediaViewerWindow.h"
#include "MediaView.h"
#include "utils/CancellableLoadPage.h"

#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QShowEvent>
#include <QStatusBar>

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

  resize(900, 600);
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
}

} // namespace Farman
