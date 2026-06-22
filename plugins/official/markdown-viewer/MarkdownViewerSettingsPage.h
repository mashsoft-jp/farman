#pragma once

#include "viewer/IPluginSettingsPage.h"

class QCheckBox;
class QLineEdit;

namespace Farman {

// Markdown ビュアーの設定ページ (Settings → Plugins → 詳細)。
// 既定でソース (生の Markdown) を表示するか、整形済みプレビューを表示するか。
class MarkdownViewerSettingsPage : public IPluginSettingsPage {
  Q_OBJECT

public:
  explicit MarkdownViewerSettingsPage(QWidget* parent = nullptr);

  void save() override;
  void restoreDefaults() override;

private:
  void applyValuesToUi(const QStringList& extensions, bool showSource);

  QLineEdit* m_extensionsEdit  = nullptr;
  QCheckBox* m_showSourceCheck = nullptr;
};

} // namespace Farman
