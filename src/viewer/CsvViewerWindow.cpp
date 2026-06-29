#include "CsvViewerWindow.h"
#include "CsvView.h"
#include "utils/CancellableLoadPage.h"

#include <QApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QStackedWidget>
#include <QLabel>
#include <QStatusBar>
#include <QtConcurrent/QtConcurrentRun>

namespace Farman {

CsvViewerWindow::CsvViewerWindow(const QString& filePath,
                                  const QString& displayPath,
                                  QWidget* parent)
  : QMainWindow(parent)
  , m_filePath(filePath)
  , m_displayPath(displayPath.isEmpty() ? filePath : displayPath)
{
  setupUi();
  loadFile();
}

void CsvViewerWindow::setupUi() {
  QFileInfo fileInfo(m_displayPath);
  setWindowTitle(QString("CSV Viewer - %1").arg(fileInfo.fileName()));

  m_stack    = new QStackedWidget(this);
  m_loadPage = new CancellableLoadPage(this);
  m_csvView  = new CsvView(this);
  m_stack->addWidget(m_loadPage);
  m_stack->addWidget(m_csvView);
  setCentralWidget(m_stack);

  connect(m_loadPage, &CancellableLoadPage::cancelled,
          this,        &QMainWindow::close);

  resize(900, 600);
}

void CsvViewerWindow::loadFile() {
  m_loadPage->setForFile(m_filePath);
  m_stack->setCurrentWidget(m_loadPage);
  auto token = m_loadPage->resetToken();
  QApplication::setOverrideCursor(Qt::WaitCursor);

  auto future = QtConcurrent::run(&CsvView::prepareLoad,
                                   m_filePath,
                                   m_csvView->currentUserEncoding(),
                                   m_csvView->currentDelimiter(),
                                   token.get(),
                                   /*maxBytes=*/ qint64(-1));
  CsvView::PreparedLoad p = waitForFutureWithEventLoop(future);
  QApplication::restoreOverrideCursor();

  const bool cancelled = token && token->load(std::memory_order_acquire);
  if (!p.ok) {
    logViewerLoadResult(QStringLiteral("CSV, external"),
                         m_displayPath, false, cancelled);
    close();
    return;
  }
  m_csvView->applyPreparedLoad(p);
  m_stack->setCurrentWidget(m_csvView);
  m_csvView->setFocus();
  statusBar()->addPermanentWidget(new QLabel(m_csvView->statusInfo(), this));
  logViewerLoadResult(QStringLiteral("CSV, external"),
                       m_displayPath, true, false);
}

void CsvViewerWindow::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape ||
      event->key() == Qt::Key_Return ||
      event->key() == Qt::Key_Enter) {
    close();
    return;
  }
  QMainWindow::keyPressEvent(event);
}

} // namespace Farman
