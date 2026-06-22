#pragma once

#include <QMainWindow>

namespace Farman {

class MediaView;

class MediaViewerWindow : public QMainWindow {
  Q_OBJECT

public:
  // filePath は実際にディスクから読むパス。displayPath はタイトルに使うパス。
  // 空のときは filePath をそのまま使う (アーカイブ内エントリ展開時のみ別物)。
  explicit MediaViewerWindow(const QString& filePath,
                             const QString& displayPath = {},
                             QWidget* parent = nullptr);
  ~MediaViewerWindow() override = default;

protected:
  // Esc / Enter でウィンドウを閉じる (トップレベル時のみ)。
  void keyPressEvent(QKeyEvent* event) override;

private:
  void setupUi();

  QString    m_filePath;
  QString    m_displayPath;
  MediaView* m_mediaView = nullptr;
};

} // namespace Farman
