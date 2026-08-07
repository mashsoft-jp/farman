#include "PdfViewerSettingsPage.h"

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
constexpr bool             kDefContinuous = true;
constexpr PdfViewerFitMode kDefFitMode    = PdfViewerFitMode::ActualSize;
const QStringList          kDefExtensions = { "pdf" };
} // namespace

PdfViewerSettingsPage::PdfViewerSettingsPage(QWidget* parent)
  : IPluginSettingsPage(parent) {
  auto* outer = new QVBoxLayout(this);

  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setContentsMargins(0, 0, 0, 0);
  m_extensionsEdit = new QLineEdit(this);
  m_extensionsEdit->setToolTip(
    tr("Comma, semicolon, or space separated extensions without leading dots."));
  form->addRow(tr("Extensions:"), m_extensionsEdit);
  m_fitCombo = new QComboBox(this);
  m_fitCombo->addItem(tr("Actual size"), static_cast<int>(PdfViewerFitMode::ActualSize));
  m_fitCombo->addItem(tr("Fit width"),   static_cast<int>(PdfViewerFitMode::FitWidth));
  m_fitCombo->addItem(tr("Fit page"),    static_cast<int>(PdfViewerFitMode::FitPage));
  m_fitCombo->setToolTip(tr("How the page is scaled when a document is opened"));
  form->addRow(tr("Default fit:"), m_fitCombo);
  outer->addLayout(form);

  m_continuousCheck = new QCheckBox(tr("Continuous scrolling"), this);
  m_continuousCheck->setToolTip(
    tr("Scroll through all pages continuously instead of one page at a time."));
  outer->addWidget(m_continuousCheck);

  outer->addStretch();

  // 現在値を読み込む (プラグイン側 Settings をファイルから読み直す)。
  Settings& s = Settings::instance();
  s.load();
  applyValuesToUi(s.pdfViewerExtensions(), s.pdfViewerContinuous(),
                  s.pdfViewerFitMode());
}

void PdfViewerSettingsPage::applyValuesToUi(const QStringList& extensions,
                                            bool continuous,
                                            PdfViewerFitMode fitMode) {
  m_extensionsEdit->setText(joinExtensionsText(extensions));
  m_continuousCheck->setChecked(continuous);
  const int idx = m_fitCombo->findData(static_cast<int>(fitMode));
  if (idx >= 0) m_fitCombo->setCurrentIndex(idx);
}

void PdfViewerSettingsPage::save() {
  Settings& s = Settings::instance();
  const QStringList exts = parseExtensionsText(m_extensionsEdit->text());
  if (!exts.isEmpty()) s.setPdfViewerExtensions(exts);
  s.setPdfViewerContinuous(m_continuousCheck->isChecked());
  s.setPdfViewerFitMode(
    static_cast<PdfViewerFitMode>(m_fitCombo->currentData().toInt()));
  s.save();
}

void PdfViewerSettingsPage::restoreDefaults() {
  applyValuesToUi(kDefExtensions, kDefContinuous, kDefFitMode);
}

} // namespace Farman
