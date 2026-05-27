#pragma once

#include <QMainWindow>

class QStackedWidget;

namespace Farman {

class BinaryView;
class CancellableLoadPage;

class BinaryViewerWindow : public QMainWindow {
  Q_OBJECT

public:
  // filePath は実際にディスクから読むパス。displayPath はタイトルに使うパス。
  // 空のときは filePath をそのまま使う (アーカイブ内エントリ展開時のみ別物)。
  explicit BinaryViewerWindow(const QString& filePath,
                              const QString& displayPath = {},
                              QWidget* parent = nullptr);
  ~BinaryViewerWindow() override = default;

protected:
  // Esc でウィンドウを閉じる。
  void keyPressEvent(QKeyEvent* event) override;

private:
  void setupUi();
  void loadFile();

  QString              m_filePath;
  QString              m_displayPath;
  QStackedWidget*      m_stack    = nullptr;
  CancellableLoadPage* m_loadPage = nullptr;
  BinaryView*          m_view     = nullptr;
};

} // namespace Farman
