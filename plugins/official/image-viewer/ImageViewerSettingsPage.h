#pragma once

#include "viewer/IPluginSettingsPage.h"

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace Farman {

// 画像ビュアーの設定ページ (Settings → Plugins → 詳細 → 設定...)。
// 旧 外観タブ「Image Viewer」サブタブのうち、テーマに依存しない表示設定
// (既定ズーム / ウィンドウに合わせる / アニメ再生) を移設したもの。
// 色 (透過の表現) とフォントはテーマ (ColorScheme) と一体なので外観タブに残す。
class ImageViewerSettingsPage : public IPluginSettingsPage {
  Q_OBJECT

public:
  explicit ImageViewerSettingsPage(QWidget* parent = nullptr);

  void save() override;
  void restoreDefaults() override;

private:
  void applyValuesToUi(const QStringList& extensions, int zoomPercent,
                       bool fit, bool animation);

  QLineEdit* m_extensionsEdit = nullptr;
  QComboBox* m_zoomCombo = nullptr;
  QCheckBox* m_fitCheck  = nullptr;
  QCheckBox* m_animCheck = nullptr;
};

} // namespace Farman
