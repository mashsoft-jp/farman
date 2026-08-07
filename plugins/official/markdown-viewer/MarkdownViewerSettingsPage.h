#pragma once

#include "viewer/IPluginSettingsPage.h"
#include "settings/ColorScheme.h"

class QCheckBox;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace Farman { class ViewerShortcutSettingsWidget; }

namespace Farman {

// Markdown ビュアーの設定ページ (Settings → Plugins → 詳細)。
// 既定の表示モード (ソース / プレビュー) に加え、本文フォント / 文字色 /
// 背景色 (テーマ依存) を、ページ上部の Light/Dark トグルで per-theme に編集する。
// 文字色 / 背景色を "(none)" (無効色) のままにするとテーマ既定色で描画される。
class MarkdownViewerSettingsPage : public IPluginSettingsPage {
  Q_OBJECT

public:
  explicit MarkdownViewerSettingsPage(QWidget* parent = nullptr);

  void save() override;
  void restoreDefaults() override;

private:
  void applyValuesToUi(const QStringList& extensions, bool showSource);

  QWidget* buildThemeSection();
  void wireColorButton(QPushButton* btn, QColor* storedValue,
                       const QString& title);
  void loadSideToUi(ThemeMode side);
  void commitUiToSide(ThemeMode side);
  static void overlayFields(const ColorScheme& src, ColorScheme& dst);

  QLineEdit* m_extensionsEdit  = nullptr;
  QCheckBox* m_showSourceCheck = nullptr;

  // テーマ依存 (フォント / 文字色 / 背景色)
  QRadioButton* m_themeLightRadio = nullptr;
  QRadioButton* m_themeDarkRadio  = nullptr;
  QPushButton*  m_fontButton = nullptr;
  QPushButton*  m_fgButton   = nullptr;
  QPushButton*  m_bgButton   = nullptr;
  QPushButton*  m_linkButton = nullptr;
  QFont         m_uiFont;
  QColor        m_uiFg;
  QColor        m_uiBg;
  QColor        m_uiLink;

  ColorScheme   m_light;
  ColorScheme   m_dark;
  ThemeMode     m_editSide = ThemeMode::Light;

  ViewerShortcutSettingsWidget* m_shortcuts = nullptr;
};

} // namespace Farman
