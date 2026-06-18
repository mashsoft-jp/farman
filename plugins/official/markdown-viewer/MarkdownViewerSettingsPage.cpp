#include "MarkdownViewerSettingsPage.h"

#include "settings/Settings.h"
#include "viewer/ExtensionsField.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>

namespace Farman {

namespace {
// 既定値 (Settings の初期値と一致させる)。
constexpr bool kDefShowSource = false;
const QStringList kDefExtensions = { "md", "markdown", "mdown", "mkd" };
} // namespace

MarkdownViewerSettingsPage::MarkdownViewerSettingsPage(QWidget* parent)
  : IPluginSettingsPage(parent) {
  auto* outer = new QVBoxLayout(this);

  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setContentsMargins(0, 0, 0, 0);
  m_extensionsEdit = new QLineEdit(this);
  m_extensionsEdit->setToolTip(
    tr("Comma, semicolon, or space separated extensions without leading dots."));
  form->addRow(tr("Extensions:"), m_extensionsEdit);
  outer->addLayout(form);

  m_showSourceCheck = new QCheckBox(tr("Show raw source instead of preview"), this);
  m_showSourceCheck->setToolTip(
    tr("Open documents as plain Markdown text rather than rendered HTML."));
  outer->addWidget(m_showSourceCheck);
  outer->addStretch();

  Settings& s = Settings::instance();
  s.load();
  applyValuesToUi(s.markdownViewerExtensions(), s.markdownViewerShowSource());
}

void MarkdownViewerSettingsPage::applyValuesToUi(const QStringList& extensions,
                                                 bool showSource) {
  m_extensionsEdit->setText(joinExtensionsText(extensions));
  m_showSourceCheck->setChecked(showSource);
}

void MarkdownViewerSettingsPage::save() {
  Settings& s = Settings::instance();
  const QStringList exts = parseExtensionsText(m_extensionsEdit->text());
  if (!exts.isEmpty()) s.setMarkdownViewerExtensions(exts);
  s.setMarkdownViewerShowSource(m_showSourceCheck->isChecked());
  s.save();
}

void MarkdownViewerSettingsPage::restoreDefaults() {
  applyValuesToUi(kDefExtensions, kDefShowSource);
}

} // namespace Farman
