#include "MediaViewerWindow.h"
#include "MediaView.h"
#include "utils/CancellableLoadPage.h"

#include <QFileInfo>
#include <QKeyEvent>

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
  // Inline 埋め込み時は ViewerPanel::openPluginFile が既にログを出すので、
  // トップレベル (External) のときだけこちらでログする。
  connect(m_mediaView, &MediaView::loadFinished, this, [this](bool ok) {
    if (isWindow()) {
      logViewerLoadResult(QStringLiteral("Media, external"),
                          m_displayPath, ok, false);
    }
  });
  m_mediaView->openFile(filePath);
}

void MediaViewerWindow::setupUi() {
  QFileInfo fileInfo(m_displayPath);
  setWindowTitle(QString("Media Viewer - %1").arg(fileInfo.fileName()));

  m_mediaView = new MediaView(this);
  setCentralWidget(m_mediaView);
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

} // namespace Farman
