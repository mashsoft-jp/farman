#pragma once

#include <QMainWindow>

namespace Farman {

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

  QString       m_filePath;
  QString       m_displayPath;
  MarkdownView* m_markdownView = nullptr;
};

} // namespace Farman
