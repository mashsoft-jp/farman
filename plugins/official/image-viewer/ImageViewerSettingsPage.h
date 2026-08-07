#pragma once

#include "viewer/IPluginSettingsPage.h"
#include "settings/ColorScheme.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace Farman { class ViewerShortcutSettingsWidget; }

namespace Farman {

// 画像ビュアーの設定ページ (Settings → Plugins → 詳細 → 設定...)。
// 旧 外観タブ「Image Viewer」サブタブの内容を全て集約する:
//  - テーマ非依存: 既定ズーム / ウィンドウに合わせる / アニメ再生 /
//                  透過モード (Checker/Solid) / Checker 2 色
//  - テーマ依存 (Light/Dark 別): 透過 Solid 色
// Solid 色のみ ColorScheme に格納されるため、ページ上部の Light/Dark トグルで
// 編集対象テーマを切り替えて編集し、save() で Settings::setScheme() へ書き戻す。
// Checker 2 色 / 透過モードは「透明部分のインジケータ」としてテーマ非依存の
// フラット設定に保存する。
class ImageViewerSettingsPage : public IPluginSettingsPage {
  Q_OBJECT

public:
  explicit ImageViewerSettingsPage(QWidget* parent = nullptr);

  void save() override;
  void restoreDefaults() override;

private:
  void applyValuesToUi(const QStringList& extensions, int zoomPercent,
                       bool fit, bool animation);

  QWidget* buildTransparencySection();
  void wireColorButton(QPushButton* btn, QColor* storedValue,
                       const QString& title);
  void loadSideToUi(ThemeMode side);   // Solid 色のみ
  void commitUiToSide(ThemeMode side); // Solid 色のみ
  static void overlayFields(const ColorScheme& src, ColorScheme& dst);

  QLineEdit* m_extensionsEdit = nullptr;
  QComboBox* m_zoomCombo = nullptr;
  QCheckBox* m_fitCheck  = nullptr;
  QCheckBox* m_animCheck = nullptr;

  // 透過 (テーマ非依存)
  QRadioButton* m_checkerRadio  = nullptr;
  QRadioButton* m_solidRadio    = nullptr;
  QPushButton*  m_checker1Button = nullptr;
  QPushButton*  m_checker2Button = nullptr;
  QColor m_uiChecker1, m_uiChecker2;

  // 透過 Solid 色 (テーマ依存)
  QRadioButton* m_themeLightRadio = nullptr;
  QRadioButton* m_themeDarkRadio  = nullptr;
  QPushButton*  m_solidButton     = nullptr;
  QColor m_uiSolid;

  ColorScheme m_light;
  ColorScheme m_dark;
  ThemeMode   m_editSide = ThemeMode::Light;

  ViewerShortcutSettingsWidget* m_shortcuts = nullptr;
};

} // namespace Farman
