#pragma once

#include "viewer/IPluginSettingsPage.h"

class QCheckBox;
class QComboBox;

namespace Farman {

// テキストビュアーの設定ページ (Settings → Plugins → 詳細 → 設定...)。
// 旧 外観タブ「Text Viewer」サブタブのうち、テーマに依存しない表示設定
// (既定エンコーディング / 行番号表示 / 折り返し) を移設したもの。
// フォントと色はテーマ (ColorScheme) と一体なので外観タブに残す。
class TextViewerSettingsPage : public IPluginSettingsPage {
  Q_OBJECT

public:
  explicit TextViewerSettingsPage(QWidget* parent = nullptr);

  void save() override;
  void restoreDefaults() override;

private:
  void applyValuesToUi(const QString& encoding, bool showLineNumbers,
                       bool wordWrap);

  QComboBox* m_encodingCombo   = nullptr;
  QCheckBox* m_lineNumbersCheck = nullptr;
  QCheckBox* m_wordWrapCheck    = nullptr;
};

} // namespace Farman
