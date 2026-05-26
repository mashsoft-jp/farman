#pragma once

#include <QMainWindow>

namespace Farman {

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

  QString  m_filePath;
  QString  m_displayPath;
  PdfView* m_pdfView = nullptr;
};

} // namespace Farman
