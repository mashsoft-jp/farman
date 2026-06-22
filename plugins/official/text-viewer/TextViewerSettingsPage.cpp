#include "TextViewerSettingsPage.h"

#include "settings/Settings.h"
#include "viewer/ExtensionsField.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QVBoxLayout>

namespace Farman {

namespace {
// 既定の対応拡張子 (Settings::m_textViewerExtensions と一致させる)。
const QStringList kDefExtensions = {
  "txt", "log",
  "c*", "!class", "!cab", "!chm", "!com",
  "h", "hpp",
  "py", "js", "ts", "java", "rs", "go", "rb", "php", "pl", "pm",
  "htm*", "json", "xml",
  "*sh", "fish",
  "yml", "yaml", "toml", "ini"
};
} // namespace

TextViewerSettingsPage::TextViewerSettingsPage(QWidget* parent)
  : IPluginSettingsPage(parent) {
  auto* outer = new QVBoxLayout(this);

  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  m_extensionsEdit = new QLineEdit(this);
  m_extensionsEdit->setToolTip(
    tr("Comma, semicolon, or space separated extensions without leading dots."));
  form->addRow(tr("Extensions:"), m_extensionsEdit);

  m_encodingCombo = new QComboBox(this);
  m_encodingCombo->setEditable(true);
  for (const char* e : { "Auto", "UTF-8", "UTF-16LE", "UTF-16BE",
                         "Shift_JIS", "EUC-JP", "ISO-8859-1" }) {
    m_encodingCombo->addItem(QString::fromLatin1(e));
  }
  m_encodingCombo->setToolTip(tr(
    "Default encoding for opening text files. 'Auto' detects the encoding "
    "from the file content."));
  form->addRow(tr("Encoding:"), m_encodingCombo);
  outer->addLayout(form);

  auto* row = new QHBoxLayout();
  m_lineNumbersCheck = new QCheckBox(tr("Show line numbers"), this);
  row->addWidget(m_lineNumbersCheck);
  m_wordWrapCheck = new QCheckBox(tr("Word wrap"), this);
  row->addWidget(m_wordWrapCheck);
  row->addStretch();
  outer->addLayout(row);
  outer->addStretch();

  Settings& s = Settings::instance();
  s.load();
  applyValuesToUi(s.textViewerExtensions(), s.textViewerEncoding(),
                  s.textViewerShowLineNumbers(), s.textViewerWordWrap());
}

void TextViewerSettingsPage::applyValuesToUi(const QStringList& extensions,
                                             const QString& encoding,
                                             bool showLineNumbers,
                                             bool wordWrap) {
  m_extensionsEdit->setText(joinExtensionsText(extensions));
  m_encodingCombo->setCurrentText(encoding);
  m_lineNumbersCheck->setChecked(showLineNumbers);
  m_wordWrapCheck->setChecked(wordWrap);
}

void TextViewerSettingsPage::save() {
  Settings& s = Settings::instance();
  const QStringList exts = parseExtensionsText(m_extensionsEdit->text());
  if (!exts.isEmpty()) s.setTextViewerExtensions(exts);
  s.setTextViewerEncoding(m_encodingCombo->currentText().trimmed());
  s.setTextViewerShowLineNumbers(m_lineNumbersCheck->isChecked());
  s.setTextViewerWordWrap(m_wordWrapCheck->isChecked());
  s.save();
}

void TextViewerSettingsPage::restoreDefaults() {
  // Settings の初期値に合わせる。
  applyValuesToUi(kDefExtensions, QStringLiteral("Auto"),
                  /*showLineNumbers=*/true, /*wordWrap=*/false);
}

} // namespace Farman
