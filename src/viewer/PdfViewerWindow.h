#pragma once

#include <QMainWindow>

class QStackedWidget;

namespace Farman {

class CancellableLoadPage;
class PdfView;

class PdfViewerWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit PdfViewerWindow(const QString& filePath,
                            const QString& displayPath = {},
                            QWidget* parent = nullptr);
  ~PdfViewerWindow() override = default;

protected:
  void keyPressEvent(QKeyEvent* event) override;

private:
  void setupUi();
  void loadFile();

  QString              m_filePath;
  QString              m_displayPath;
  QStackedWidget*      m_stack    = nullptr;
  CancellableLoadPage* m_loadPage = nullptr;
  PdfView*             m_pdfView  = nullptr;
};

} // namespace Farman
