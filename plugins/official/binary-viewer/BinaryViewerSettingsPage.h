#pragma once

#include "viewer/IPluginSettingsPage.h"

class QComboBox;

namespace Farman {

// バイナリビュアーの設定ページ (Settings → Plugins → 詳細 → 設定...)。
// 旧 外観タブ「Binary Viewer」サブタブのうち、テーマに依存しない表示設定
// (表示単位 / エンディアン / 文字列エンコーディング) を移設したもの。
// フォントと色はテーマ (ColorScheme) と一体なので外観タブに残す。
class BinaryViewerSettingsPage : public IPluginSettingsPage {
  Q_OBJECT

public:
  explicit BinaryViewerSettingsPage(QWidget* parent = nullptr);

  void save() override;
  void restoreDefaults() override;

private:
  void applyValuesToUi(int unitBytes, int endian, const QString& encoding);

  QComboBox* m_unitCombo     = nullptr;
  QComboBox* m_endianCombo   = nullptr;
  QComboBox* m_encodingCombo = nullptr;
};

} // namespace Farman
