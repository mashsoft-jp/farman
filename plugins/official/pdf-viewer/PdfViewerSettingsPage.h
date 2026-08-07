#pragma once

#include "viewer/IPluginSettingsPage.h"
#include "types.h"

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace Farman { class ViewerShortcutSettingsWidget; }

namespace Farman {

// PDF ビュアーの設定ページ (Settings → Plugins → 詳細)。
// テーマに依存しない表示の既定 (連続スクロール / 既定のフィット方法) を扱う。
class PdfViewerSettingsPage : public IPluginSettingsPage {
  Q_OBJECT

public:
  explicit PdfViewerSettingsPage(QWidget* parent = nullptr);

  void save() override;
  void restoreDefaults() override;

private:
  void applyValuesToUi(const QStringList& extensions, bool continuous,
                       PdfViewerFitMode fitMode);

  QLineEdit* m_extensionsEdit  = nullptr;
  QCheckBox* m_continuousCheck = nullptr;
  QComboBox* m_fitCombo        = nullptr;
  ViewerShortcutSettingsWidget* m_shortcuts = nullptr;
};

} // namespace Farman
