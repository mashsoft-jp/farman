#pragma once

#include "viewer/IPluginSettingsPage.h"

#include <QFont>
#include <QString>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;

namespace Farman {

class ViewerShortcutSettingsWidget;

// CSV / TSV ビュアーの設定ページ (Settings → Plugins → 詳細)。
// 区切り文字の既定・先頭行ヘッダ扱いに加え、表示フォントを設定する。
// CSV は文字色設定を持たないため、フォントはテーマ (Light/Dark) で分けず
// 単一値として編集する。ただし保存時は将来的な拡張に備えて Light/Dark
// 双方のテーマスキームに同じ値を書き込む。
class CsvViewerSettingsPage : public IPluginSettingsPage {
  Q_OBJECT

public:
  explicit CsvViewerSettingsPage(QWidget* parent = nullptr);

  void save() override;
  void restoreDefaults() override;

private:
  void applyValuesToUi(const QStringList& extensions, const QString& delimiter,
                       bool firstRowAsHeader);
  QWidget* buildFontSection();

  QLineEdit* m_extensionsEdit  = nullptr;
  QComboBox* m_delimiterCombo = nullptr;
  QCheckBox* m_headerCheck     = nullptr;

  // 表示フォント (テーマ非依存の単一値。保存時のみ両テーマへ書き込む)
  QPushButton* m_fontButton = nullptr;
  QFont        m_uiFont;

  ViewerShortcutSettingsWidget* m_shortcuts = nullptr;
};

} // namespace Farman
