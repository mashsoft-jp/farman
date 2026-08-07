#pragma once

#include "viewer/IPluginSettingsPage.h"
#include "settings/ColorScheme.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace Farman {

// テキストビュアーの設定ページ (Settings → Plugins → 詳細 → 設定...)。
// 旧 外観タブ「Text Viewer」サブタブの内容を全て集約する:
//  - テーマ非依存: 既定エンコーディング / 行番号表示 / 折り返し
//  - テーマ依存 (Light/Dark 別): フォント / 配色 (Normal / Selected / 行番号)
// テーマ依存フィールドは ColorScheme に格納されるため、ページ上部の Light/Dark
// トグルで編集対象テーマを切り替えながら両スキームを編集し、save() で
// Settings::setScheme() へ書き戻す。
class TextViewerSettingsPage : public IPluginSettingsPage {
  Q_OBJECT

public:
  explicit TextViewerSettingsPage(QWidget* parent = nullptr);

  void save() override;
  void restoreDefaults() override;

private:
  void applyValuesToUi(const QStringList& extensions, const QString& encoding,
                       bool showLineNumbers, bool wordWrap);

  // テーマ依存セクション (フォント / 配色) を構築する。
  QWidget* buildThemeColorSection();
  // 指定色ボタンをクリックで QColorDialog に繋ぎ、選択色を storedValue へ。
  void wireColorButton(QPushButton* btn, QColor* storedValue,
                       const QString& title);
  // シャドースキーム (side 側) の値を UI へ反映する。
  void loadSideToUi(ThemeMode side);
  // 現在 UI に乗っている値をシャドースキーム (side 側) へ書き戻す。
  void commitUiToSide(ThemeMode side);
  // src のテキストビュアー欄のみを dst へ上書き (RMW overlay)。
  static void overlayFields(const ColorScheme& src, ColorScheme& dst);

  QLineEdit* m_extensionsEdit   = nullptr;
  QComboBox* m_encodingCombo   = nullptr;
  QCheckBox* m_lineNumbersCheck = nullptr;
  QCheckBox* m_wordWrapCheck    = nullptr;

  // テーマ依存 (フォント / 配色)
  QRadioButton* m_themeLightRadio = nullptr;
  QRadioButton* m_themeDarkRadio  = nullptr;
  QPushButton*  m_fontButton         = nullptr;
  QPushButton*  m_normalFgButton     = nullptr;
  QPushButton*  m_normalBgButton     = nullptr;
  QPushButton*  m_selectedFgButton   = nullptr;
  QPushButton*  m_selectedBgButton   = nullptr;
  QPushButton*  m_lineNumberFgButton = nullptr;
  QPushButton*  m_lineNumberBgButton = nullptr;

  // 現在 UI に乗っている値 (編集中テーマ側)
  QFont  m_uiFont;
  QColor m_uiNormalFg, m_uiNormalBg;
  QColor m_uiSelectedFg, m_uiSelectedBg;
  QColor m_uiLineNumberFg, m_uiLineNumberBg;

  // Light / Dark のシャドースキーム (フル ColorScheme を保持し viewer 欄のみ編集)
  ColorScheme m_light;
  ColorScheme m_dark;
  ThemeMode   m_editSide = ThemeMode::Light;
};

} // namespace Farman
