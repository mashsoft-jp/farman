#pragma once

#include <QMainWindow>

class QStackedWidget;

namespace Farman {

class CancellableLoadPage;
class MarkdownView;

class MarkdownViewerWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MarkdownViewerWindow(const QString& filePath,
                                 const QString& displayPath = {},
                                 QWidget* parent = nullptr);
  ~MarkdownViewerWindow() override = default;

protected:
  void keyPressEvent(QKeyEvent* event) override;

private:
  void setupUi();
  void loadFile();

  QString              m_filePath;
  QString              m_displayPath;
  QStackedWidget*      m_stack         = nullptr;
  CancellableLoadPage* m_loadPage      = nullptr;
  MarkdownView*        m_markdownView  = nullptr;
};

} // namespace Farman
