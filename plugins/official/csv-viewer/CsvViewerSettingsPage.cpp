#include "CsvViewerSettingsPage.h"

#include "settings/Settings.h"
#include "viewer/ExtensionsField.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
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
  outer->addStretch();

  Settings& s = Settings::instance();
  s.load();
  applyValuesToUi(s.csvViewerExtensions(), s.csvViewerDelimiter(),
                  s.csvViewerFirstRowAsHeader());
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
  s.save();
}

void CsvViewerSettingsPage::restoreDefaults() {
  applyValuesToUi(kDefExtensions, kDefDelimiter, kDefFirstRowHeader);
}

} // namespace Farman
