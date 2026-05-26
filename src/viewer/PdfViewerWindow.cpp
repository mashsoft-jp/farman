#include "PdfViewerWindow.h"
#include "PdfView.h"
#include "utils/Dialogs.h"

#include <QFileInfo>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QWidget>

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

  QWidget* centralWidget = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(centralWidget);
  layout->setContentsMargins(0, 0, 0, 0);

  m_pdfView = new PdfView(this);
  layout->addWidget(m_pdfView);
  setCentralWidget(centralWidget);

  resize(900, 700);
}

void PdfViewerWindow::loadFile() {
  if (!m_pdfView->loadFile(m_filePath)) {
    critical(this, QStringLiteral("Error"),
      QStringLiteral("Failed to open PDF: %1").arg(m_filePath));
    return;
  }
  m_pdfView->setFocus();
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
