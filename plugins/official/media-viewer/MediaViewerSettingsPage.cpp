#include "MediaViewerSettingsPage.h"

#include "settings/Settings.h"
#include "viewer/ExtensionsField.h"
#include "viewer/ViewerShortcutSettingsWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

namespace Farman {

namespace {
// 既定値 (Settings の初期値と一致させる)。
constexpr int  kDefVolume    = 80;
constexpr bool kDefLoop       = false;
constexpr bool kDefAutoplay   = true;
constexpr bool kDefFit        = true;
constexpr int  kDefZoom       = 100;
const QStringList kDefExtensions = {
  "mp4", "mov", "m4v", "webm", "avi", "mkv",
  "wmv", "mpg", "mpeg", "m2v", "m2ts", "mts", "ts",
  "wav", "mp3", "m4a", "flac", "ogg", "aac", "wma",
};
} // namespace

MediaViewerSettingsPage::MediaViewerSettingsPage(QWidget* parent)
  : IPluginSettingsPage(parent) {
  auto* outer = new QVBoxLayout(this);

  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setContentsMargins(0, 0, 0, 0);
  m_extensionsEdit = new QLineEdit(this);
  m_extensionsEdit->setToolTip(
    tr("Comma, semicolon, or space separated extensions without leading dots."));
  form->addRow(tr("Extensions:"), m_extensionsEdit);
  m_volumeSpin = new QSpinBox(this);
  m_volumeSpin->setRange(0, 100);
  m_volumeSpin->setSuffix(QStringLiteral(" %"));
  m_volumeSpin->setToolTip(tr("Initial playback volume"));
  form->addRow(tr("Volume:"), m_volumeSpin);
  m_zoomCombo = new QComboBox(this);
  m_zoomCombo->setEditable(true);
  for (int p : { 25, 50, 75, 100, 150, 200 }) {
    m_zoomCombo->addItem(QString::number(p) + QLatin1Char('%'), p);
  }
  m_zoomCombo->setToolTip(
    tr("Default zoom factor (used when 'Fit video to window' is off)"));
  form->addRow(tr("Zoom:"), m_zoomCombo);
  outer->addLayout(form);

  m_fitCheck = new QCheckBox(tr("Fit video to window"), this);
  m_fitCheck->setToolTip(tr(
    "Scale the video to fit within the viewer; zoom factor is ignored "
    "while this is on."));
  outer->addWidget(m_fitCheck);

  m_autoplayCheck = new QCheckBox(tr("Start playback automatically"), this);
  outer->addWidget(m_autoplayCheck);

  m_loopCheck = new QCheckBox(tr("Loop playback"), this);
  outer->addWidget(m_loopCheck);

  auto* scGroup = new QGroupBox(tr("Shortcuts"), this);
  auto* scLayout = new QVBoxLayout(scGroup);
  m_shortcuts = new ViewerShortcutSettingsWidget(QStringLiteral("media"), scGroup);
  scLayout->addWidget(m_shortcuts);
  outer->addWidget(scGroup);
  outer->addStretch();

  Settings& s = Settings::instance();
  s.load();
  applyValuesToUi(s.mediaViewerExtensions(), s.mediaViewerVolume(),
                  s.mediaViewerLoop(), s.mediaViewerAutoplay(),
                  s.mediaViewerFitToWindow(), s.mediaViewerZoomPercent());
}

void MediaViewerSettingsPage::applyValuesToUi(const QStringList& extensions,
                                              int volume, bool loop,
                                              bool autoplay, bool fitToWindow,
                                              int zoomPercent) {
  m_extensionsEdit->setText(joinExtensionsText(extensions));
  m_volumeSpin->setValue(volume);
  m_zoomCombo->setCurrentText(QString::number(zoomPercent) + QLatin1Char('%'));
  m_fitCheck->setChecked(fitToWindow);
  m_loopCheck->setChecked(loop);
  m_autoplayCheck->setChecked(autoplay);
}

void MediaViewerSettingsPage::save() {
  Settings& s = Settings::instance();
  const QStringList exts = parseExtensionsText(m_extensionsEdit->text());
  if (!exts.isEmpty()) s.setMediaViewerExtensions(exts);
  s.setMediaViewerVolume(m_volumeSpin->value());
  bool ok = false;
  const int zoom =
    m_zoomCombo->currentText().remove(QLatin1Char('%')).trimmed().toInt(&ok);
  if (ok && zoom > 0) s.setMediaViewerZoomPercent(zoom);
  s.setMediaViewerFitToWindow(m_fitCheck->isChecked());
  s.setMediaViewerLoop(m_loopCheck->isChecked());
  s.setMediaViewerAutoplay(m_autoplayCheck->isChecked());
  s.save();
  if (m_shortcuts) m_shortcuts->save();
}

void MediaViewerSettingsPage::restoreDefaults() {
  applyValuesToUi(kDefExtensions, kDefVolume, kDefLoop, kDefAutoplay,
                  kDefFit, kDefZoom);
  if (m_shortcuts) m_shortcuts->restoreDefaults();
}

} // namespace Farman
