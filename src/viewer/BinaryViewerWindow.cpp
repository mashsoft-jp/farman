#include "BinaryViewerWindow.h"
#include "BinaryView.h"
#include "utils/CancellableLoadPage.h"

#include <QApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QStackedWidget>
#include <QLabel>
#include <QStatusBar>
#include <QtConcurrent/QtConcurrentRun>

namespace Farman {

BinaryViewerWindow::BinaryViewerWindow(const QString& filePath,
                                       const QString& displayPath,
                                       QWidget* parent)
  : QMainWindow(parent)
  , m_filePath(filePath)
  , m_displayPath(displayPath.isEmpty() ? filePath : displayPath)
{
  setupUi();
  loadFile();
}

void BinaryViewerWindow::setupUi() {
  QFileInfo fileInfo(m_displayPath);
  setWindowTitle(QString("Binary Viewer - %1").arg(fileInfo.fileName()));

  m_stack    = new QStackedWidget(this);
  m_loadPage = new CancellableLoadPage(this);
  m_view     = new BinaryView(this);
  m_stack->addWidget(m_loadPage);
  m_stack->addWidget(m_view);
  setCentralWidget(m_stack);

  connect(m_loadPage, &CancellableLoadPage::cancelled,
          this,        &QMainWindow::close);

  resize(800, 600);
}

void BinaryViewerWindow::loadFile() {
  m_loadPage->setForFile(m_filePath);
  m_stack->setCurrentWidget(m_loadPage);
  auto token = m_loadPage->resetToken();
  QApplication::setOverrideCursor(Qt::WaitCursor);

  auto future = QtConcurrent::run(&BinaryView::prepareLoad,
                                   m_filePath,
                                   m_view->currentUnit(),
                                   m_view->currentEndian(),
                                   m_view->currentEncoding(),
                                   token.get(),
                                   /*maxBytes=*/ qint64(-1));
  BinaryView::PreparedLoad p = waitForFutureWithEventLoop(future);
  QApplication::restoreOverrideCursor();

  const bool cancelled = token && token->load(std::memory_order_acquire);
  if (!p.ok) {
    logViewerLoadResult(QStringLiteral("Binary, external"),
                         m_displayPath, false, cancelled);
    close();
    return;
  }
  m_view->applyPreparedLoad(p);
  m_stack->setCurrentWidget(m_view);
  // BinaryView の setFocusProxy(m_textArea) を効かせるため明示的に setFocus。
  m_view->setFocus();
  statusBar()->addPermanentWidget(new QLabel(m_view->statusInfo(), this));
  logViewerLoadResult(QStringLiteral("Binary, external"),
                       m_displayPath, true, false);
}

void BinaryViewerWindow::keyPressEvent(QKeyEvent* event) {
  // Esc / Enter / Return でウィンドウを閉じる (Inline モード時の戻り操作と
  // 挙動を揃える)。ロード中の Esc は CancellableLoadPage 側で先に処理される。
  if (event->key() == Qt::Key_Escape ||
      event->key() == Qt::Key_Return ||
      event->key() == Qt::Key_Enter) {
    close();
    return;
  }
  QMainWindow::keyPressEvent(event);
}

} // namespace Farman
