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

  // ステータスバー用の 1 行要約 (フォーマット / コーデック / 解像度 / 長さ)。
  // Inline 埋め込み時は本体ステータスバーがこれを使う。ViewerPanel が型に依存
  // せずメタオブジェクト経由 (invokeMethod) で呼ぶため Q_INVOKABLE が必須。
  Q_INVOKABLE QString statusInfo() const;

signals:
  // 要約が更新されたとき (メタデータ / 長さの確定時)。MediaView から転送する。
  void statusInfoChanged(const QString& info);

protected:
  // Esc / Enter でウィンドウを閉じる (トップレベル時のみ)。
  void keyPressEvent(QKeyEvent* event) override;
  // Inline (非トップレベル) では自前 statusBar を隠し、本体ステータスバーに任せる。
  void showEvent(QShowEvent* event) override;

private:
  void setupUi();

  QString    m_filePath;
  QString    m_displayPath;
  MediaView* m_mediaView = nullptr;
};

} // namespace Farman
