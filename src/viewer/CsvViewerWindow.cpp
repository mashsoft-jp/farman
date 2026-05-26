#include "CsvViewerWindow.h"
#include "CsvView.h"
#include "utils/Dialogs.h"

#include <QFileInfo>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QWidget>

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

  QWidget* centralWidget = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(centralWidget);
  layout->setContentsMargins(0, 0, 0, 0);

  m_csvView = new CsvView(this);
  layout->addWidget(m_csvView);
  setCentralWidget(centralWidget);

  resize(900, 600);
}

void CsvViewerWindow::loadFile() {
  if (!m_csvView->loadFile(m_filePath)) {
    critical(this, QStringLiteral("Error"),
      QStringLiteral("Failed to open CSV: %1").arg(m_filePath));
    return;
  }
  m_csvView->setFocus();
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
