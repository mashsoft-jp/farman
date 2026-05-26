#include "MarkdownViewerWindow.h"
#include "MarkdownView.h"
#include "utils/Dialogs.h"

#include <QFileInfo>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QWidget>

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

  QWidget* centralWidget = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(centralWidget);
  layout->setContentsMargins(0, 0, 0, 0);

  m_markdownView = new MarkdownView(this);
  layout->addWidget(m_markdownView);
  setCentralWidget(centralWidget);

  resize(900, 700);
}

void MarkdownViewerWindow::loadFile() {
  if (!m_markdownView->loadFile(m_filePath)) {
    critical(this, QStringLiteral("Error"),
      QStringLiteral("Failed to open Markdown file: %1").arg(m_filePath));
    return;
  }
  m_markdownView->setFocus();
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
