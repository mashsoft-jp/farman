#include "PdfViewerWindow.h"
#include "PdfView.h"
#include "utils/CancellableLoadPage.h"

#include <QApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QStackedWidget>
#include <QtConcurrent/QtConcurrentRun>

namespace Farman {

PdfViewerWindow::PdfViewerWindow(const QString& filePath,
                                  const QString& displayPath,
                                  QWidget* parent)
  : QMainWindow(parent)
  , m_filePath(filePath)
  , m_displayPath(displayPath.isEmpty() ? filePath : displayPath)
{
  setupUi();
  loadFile();
}

void PdfViewerWindow::setupUi() {
  QFileInfo fileInfo(m_displayPath);
  setWindowTitle(QString("PDF Viewer - %1").arg(fileInfo.fileName()));

  m_stack    = new QStackedWidget(this);
  m_loadPage = new CancellableLoadPage(this);
  m_pdfView  = new PdfView(this);
  m_stack->addWidget(m_loadPage);
  m_stack->addWidget(m_pdfView);
  setCentralWidget(m_stack);

  connect(m_loadPage, &CancellableLoadPage::cancelled,
          this,        &QMainWindow::close);

  resize(900, 700);
}

void PdfViewerWindow::loadFile() {
  m_loadPage->setForFile(m_filePath);
  m_stack->setCurrentWidget(m_loadPage);
  auto token = m_loadPage->resetToken();
  QApplication::setOverrideCursor(Qt::WaitCursor);

  // PdfView::prepareLoad は存在チェックだけの軽量関数なので、ほぼ即時。
  // 実際の document load (heavier) は applyPreparedLoad で UI 側に走る。
  auto future = QtConcurrent::run(&PdfView::prepareLoad,
                                   m_filePath,
                                   token.get());
  PdfView::PreparedLoad p = waitForFutureWithEventLoop(future);
  QApplication::restoreOverrideCursor();

  const bool cancelled = token && token->load(std::memory_order_acquire);
  if (!p.ok) {
    logViewerLoadResult(QStringLiteral("PDF, external"),
                         m_displayPath, false, cancelled);
    close();
    return;
  }
  m_pdfView->applyPreparedLoad(p);
  m_stack->setCurrentWidget(m_pdfView);
  m_pdfView->setFocus();
  logViewerLoadResult(QStringLiteral("PDF, external"),
                       m_displayPath, true, false);
}

void PdfViewerWindow::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape ||
      event->key() == Qt::Key_Return ||
      event->key() == Qt::Key_Enter) {
    close();
    return;
  }
  QMainWindow::keyPressEvent(event);
}

} // namespace Farman
