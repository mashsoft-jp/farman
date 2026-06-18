#pragma once

#include "viewer/IPluginSettingsPage.h"

#include <QString>

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace Farman {

// CSV / TSV ビュアーの設定ページ (Settings → Plugins → 詳細)。
// 区切り文字の既定と、先頭行をヘッダとして扱うかを設定する。
class CsvViewerSettingsPage : public IPluginSettingsPage {
  Q_OBJECT

public:
  explicit CsvViewerSettingsPage(QWidget* parent = nullptr);

  void save() override;
  void restoreDefaults() override;

private:
  void applyValuesToUi(const QStringList& extensions, const QString& delimiter,
                       bool firstRowAsHeader);

  QLineEdit* m_extensionsEdit  = nullptr;
  QComboBox* m_delimiterCombo = nullptr;
  QCheckBox* m_headerCheck     = nullptr;
};

} // namespace Farman
