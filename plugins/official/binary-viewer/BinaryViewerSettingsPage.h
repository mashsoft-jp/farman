#pragma once

#include "viewer/IPluginSettingsPage.h"
#include "settings/ColorScheme.h"

class QComboBox;
class QPushButton;
class QRadioButton;

namespace Farman { class ViewerShortcutSettingsWidget; }

namespace Farman {

// バイナリビュアーの設定ページ (Settings → Plugins → 詳細 → 設定...)。
// 旧 外観タブ「Binary Viewer」サブタブの内容を全て集約する:
//  - テーマ非依存: 表示単位 / エンディアン / 文字列エンコーディング
//  - テーマ依存 (Light/Dark 別): フォント / 配色 (Normal / Selected / Address)
// テーマ依存フィールドはページ上部の Light/Dark トグルで編集対象を切り替え、
// save() で Settings::setScheme() へ書き戻す。
class BinaryViewerSettingsPage : public IPluginSettingsPage {
  Q_OBJECT

public:
  explicit BinaryViewerSettingsPage(QWidget* parent = nullptr);

  void save() override;
  void restoreDefaults() override;

private:
  void applyValuesToUi(int unitBytes, int endian, const QString& encoding);

  QWidget* buildThemeColorSection();
  void wireColorButton(QPushButton* btn, QColor* storedValue,
                       const QString& title);
  void loadSideToUi(ThemeMode side);
  void commitUiToSide(ThemeMode side);
  static void overlayFields(const ColorScheme& src, ColorScheme& dst);

  QComboBox* m_unitCombo     = nullptr;
  QComboBox* m_endianCombo   = nullptr;
  QComboBox* m_encodingCombo = nullptr;

  // テーマ依存 (フォント / 配色)
  QRadioButton* m_themeLightRadio = nullptr;
  QRadioButton* m_themeDarkRadio  = nullptr;
  QPushButton*  m_fontButton       = nullptr;
  QPushButton*  m_normalFgButton   = nullptr;
  QPushButton*  m_normalBgButton   = nullptr;
  QPushButton*  m_selectedFgButton = nullptr;
  QPushButton*  m_selectedBgButton = nullptr;
  QPushButton*  m_addressFgButton  = nullptr;
  QPushButton*  m_addressBgButton  = nullptr;

  QFont  m_uiFont;
  QColor m_uiNormalFg, m_uiNormalBg;
  QColor m_uiSelectedFg, m_uiSelectedBg;
  QColor m_uiAddressFg, m_uiAddressBg;

  ColorScheme m_light;
  ColorScheme m_dark;
  ThemeMode   m_editSide = ThemeMode::Light;

  ViewerShortcutSettingsWidget* m_shortcuts = nullptr;
};

} // namespace Farman
