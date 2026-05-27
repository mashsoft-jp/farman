#pragma once

#include <QMainWindow>

class QStackedWidget;

namespace Farman {

class CancellableLoadPage;
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

  QString              m_filePath;
  QString              m_displayPath;
  QStackedWidget*      m_stack    = nullptr;
  CancellableLoadPage* m_loadPage = nullptr;
  CsvView*             m_csvView  = nullptr;
};

} // namespace Farman
