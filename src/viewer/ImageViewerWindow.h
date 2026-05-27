#pragma once

#include <QMainWindow>

class QStackedWidget;

namespace Farman {

class CancellableLoadPage;
class ImageView;

class ImageViewerWindow : public QMainWindow {
  Q_OBJECT

public:
  // filePath は実際にディスクから読むパス。displayPath はタイトルに使うパス。
  // 空のときは filePath をそのまま使う (アーカイブ内エントリ展開時のみ別物)。
  explicit ImageViewerWindow(const QString& filePath,
                             const QString& displayPath = {},
                             QWidget* parent = nullptr);
  ~ImageViewerWindow() override = default;

protected:
  // Esc でウィンドウを閉じる。
  void keyPressEvent(QKeyEvent* event) override;

private:
  void setupUi();
  void loadImage();
  // 画像の自然サイズに合わせてウィンドウを resize する。ツールバー / 余白
  // ぶんは画像表示領域 (m_imageView->sizeHint() ベース) との差で補正する。
  void fitWindowToImage();

  QString              m_filePath;
  QString              m_displayPath;
  QStackedWidget*      m_stack     = nullptr;
  CancellableLoadPage* m_loadPage  = nullptr;
  ImageView*           m_imageView = nullptr;
};

} // namespace Farman
