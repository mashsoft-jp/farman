#include "MarkdownViewerWindow.h"
#include "MarkdownView.h"
#include "utils/CancellableLoadPage.h"

#include <QApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QStackedWidget>
#include <QtConcurrent/QtConcurrentRun>

namespace Farman {

MarkdownViewerWindow::MarkdownViewerWindow(const QString& filePath,
                                            const QString& displayPath,
                                            QWidget* parent)
  : QMainWindow(parent)
  , m_filePath(filePath)
  , m_displayPath(displayPath.isEmpty() ? filePath : displayPath)
{
  setupUi();
  loadFile();
}

void MarkdownViewerWindow::setupUi() {
  QFileInfo fileInfo(m_displayPath);
  setWindowTitle(QString("Markdown Viewer - %1").arg(fileInfo.fileName()));

  m_stack        = new QStackedWidget(this);
  m_loadPage     = new CancellableLoadPage(this);
  m_markdownView = new MarkdownView(this);
  m_stack->addWidget(m_loadPage);
  m_stack->addWidget(m_markdownView);
  setCentralWidget(m_stack);

  connect(m_loadPage, &CancellableLoadPage::cancelled,
          this,        &QMainWindow::close);

  resize(900, 700);
}

void MarkdownViewerWindow::loadFile() {
  m_loadPage->setForFile(m_filePath);
  m_stack->setCurrentWidget(m_loadPage);
  auto token = m_loadPage->resetToken();
  QApplication::setOverrideCursor(Qt::WaitCursor);

  auto future = QtConcurrent::run(&MarkdownView::prepareLoad,
                                   m_filePath,
                                   m_markdownView->currentUserEncoding(),
                                   token.get(),
                                   /*maxBytes=*/ qint64(-1));
  MarkdownView::PreparedLoad p = waitForFutureWithEventLoop(future);
  QApplication::restoreOverrideCursor();

  const bool cancelled = token && token->load(std::memory_order_acquire);
  if (!p.ok) {
    logViewerLoadResult(QStringLiteral("Markdown, external"),
                         m_displayPath, false, cancelled);
    close();
    return;
  }
  m_markdownView->applyPreparedLoad(p);
  m_stack->setCurrentWidget(m_markdownView);
  m_markdownView->setFocus();
  logViewerLoadResult(QStringLiteral("Markdown, external"),
                       m_displayPath, true, false);
}

void MarkdownViewerWindow::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape ||
      event->key() == Qt::Key_Return ||
      event->key() == Qt::Key_Enter) {
    close();
    return;
  }
  QMainWindow::keyPressEvent(event);
}

} // namespace Farman
