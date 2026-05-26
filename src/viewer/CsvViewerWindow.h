#pragma once

#include <QMainWindow>

namespace Farman {

class CsvView;

class CsvViewerWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit CsvViewerWindow(const QString& filePath,
                            const QString& displayPath = {},
                            QWidget* parent = nullptr);
  ~CsvViewerWindow() override = default;

protected:
  void keyPressEvent(QKeyEvent* event) override;

private:
  void setupUi();
  void loadFile();

  QString  m_filePath;
  QString  m_displayPath;
  CsvView* m_csvView = nullptr;
};

} // namespace Farman
