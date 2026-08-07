#include "CsvViewerSettingsPage.h"

#include "settings/Settings.h"
#include "viewer/ExtensionsField.h"
#include "viewer/ViewerShortcutSettingsWidget.h"
#include "viewer/ViewerThemeFields.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QFontDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace Farman {

namespace {
// 既定値 (Settings の初期値と一致させる)。
const QString kDefDelimiter      = QStringLiteral("auto");
constexpr bool kDefFirstRowHeader = false;
const QStringList kDefExtensions  = { "csv", "tsv" };
} // namespace

CsvViewerSettingsPage::CsvViewerSettingsPage(QWidget* parent)
  : IPluginSettingsPage(parent) {
  auto* outer = new QVBoxLayout(this);

  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setContentsMargins(0, 0, 0, 0);
  m_extensionsEdit = new QLineEdit(this);
  m_extensionsEdit->setToolTip(
    tr("Comma, semicolon, or space separated extensions without leading dots."));
  form->addRow(tr("Extensions:"), m_extensionsEdit);
  m_delimiterCombo = new QComboBox(this);
  m_delimiterCombo->addItem(tr("Auto detect"),     QStringLiteral("auto"));
  m_delimiterCombo->addItem(tr("Comma ( , )"),     QStringLiteral("comma"));
  m_delimiterCombo->addItem(tr("Tab ( \\t )"),     QStringLiteral("tab"));
  m_delimiterCombo->addItem(tr("Semicolon ( ; )"), QStringLiteral("semicolon"));
  m_delimiterCombo->setToolTip(tr("Column separator used when a file is opened"));
  form->addRow(tr("Delimiter:"), m_delimiterCombo);
  outer->addLayout(form);

  m_headerCheck = new QCheckBox(tr("Treat first row as header"), this);
  outer->addWidget(m_headerCheck);

  outer->addWidget(buildFontSection());

  auto* scGroup = new QGroupBox(tr("Shortcuts"), this);
  auto* scLayout = new QVBoxLayout(scGroup);
  m_shortcuts = new ViewerShortcutSettingsWidget(QStringLiteral("csv"), scGroup);
  scLayout->addWidget(m_shortcuts);
  outer->addWidget(scGroup);
  outer->addStretch();

  Settings& s = Settings::instance();
  s.load();
  applyValuesToUi(s.csvViewerExtensions(), s.csvViewerDelimiter(),
                  s.csvViewerFirstRowAsHeader());
  // フォントは有効テーマ側の値を初期表示 (Light/Dark 双方に同値保存される)。
  m_uiFont = s.csvViewerFont();
  styleThemeFontButton(m_fontButton, m_uiFont);
}

QWidget* CsvViewerSettingsPage::buildFontSection() {
  auto* row = new QWidget(this);
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(new QLabel(tr("Font:"), row));
  m_fontButton = new QPushButton(row);
  m_fontButton->setToolTip(tr("Choose the font for the CSV / TSV table"));
  connect(m_fontButton, &QPushButton::clicked, this, [this]() {
    bool ok = false;
    const QFont chosen = QFontDialog::getFont(&ok, m_uiFont, this,
                                              tr("CSV / TSV Viewer Font"));
    if (ok) {
      m_uiFont = chosen;
      styleThemeFontButton(m_fontButton, m_uiFont);
    }
  });
  layout->addWidget(m_fontButton);
  layout->addStretch();
  return row;
}

void CsvViewerSettingsPage::applyValuesToUi(const QStringList& extensions,
                                            const QString& delimiter,
                                            bool firstRowAsHeader) {
  m_extensionsEdit->setText(joinExtensionsText(extensions));
  const int idx = m_delimiterCombo->findData(delimiter);
  m_delimiterCombo->setCurrentIndex(idx >= 0 ? idx : 0);
  m_headerCheck->setChecked(firstRowAsHeader);
}

void CsvViewerSettingsPage::save() {
  Settings& s = Settings::instance();
  const QStringList exts = parseExtensionsText(m_extensionsEdit->text());
  if (!exts.isEmpty()) s.setCsvViewerExtensions(exts);
  s.setCsvViewerDelimiter(m_delimiterCombo->currentData().toString());
  s.setCsvViewerFirstRowAsHeader(m_headerCheck->isChecked());

  // フォントは Light / Dark 双方のスキームへ同じ値を書き込む (RMW overlay)。
  // 将来 CSV に文字色などテーマ依存項目が増えても分岐しやすいよう、
  // ストレージ上はテーマ別のままにしておく。
  ColorScheme fresh = s.scheme(ThemeMode::Light);
  fresh.csvViewerFont = m_uiFont;
  s.setScheme(ThemeMode::Light, fresh);
  fresh = s.scheme(ThemeMode::Dark);
  fresh.csvViewerFont = m_uiFont;
  s.setScheme(ThemeMode::Dark, fresh);

  s.save();
  if (m_shortcuts) m_shortcuts->save();
}

void CsvViewerSettingsPage::restoreDefaults() {
  applyValuesToUi(kDefExtensions, kDefDelimiter, kDefFirstRowHeader);
  m_uiFont = defaultLightScheme().csvViewerFont;
  styleThemeFontButton(m_fontButton, m_uiFont);
  if (m_shortcuts) m_shortcuts->restoreDefaults();
}

} // namespace Farman
