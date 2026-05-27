#pragma once

#include <QMainWindow>

class QStackedWidget;

namespace Farman {

class CancellableLoadPage;
class TextView;

class TextViewerWindow : public QMainWindow {
  Q_OBJECT

public:
  // filePath は実際にディスクから読むパス。displayPath は表示 (タイトル) に
  // 使うパス。アーカイブ内エントリ展開時のみ displayPath が "<archive>!/<inner>"。
  // displayPath が空なら filePath をそのまま使う。
  explicit TextViewerWindow(const QString& filePath,
                            const QString& displayPath = {},
                            QWidget* parent = nullptr);
  ~TextViewerWindow() override = default;

protected:
  // External モード時に Esc で閉じてメインに戻れるよう、QMainWindow の
  // keyPressEvent を上書きする。
  void keyPressEvent(QKeyEvent* event) override;

private:
  void setupUi();
  void loadFile();

  QString              m_filePath;     // ディスク上のパス (load 用)
  QString              m_displayPath;  // タイトルに出すパス (表示用)
  QStackedWidget*      m_stack    = nullptr;
  CancellableLoadPage* m_loadPage = nullptr;
  TextView*            m_textView = nullptr;
};

} // namespace Farman
