#include "MediaViewerSettingsPage.h"

#include "settings/Settings.h"
#include "viewer/ExtensionsField.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

namespace Farman {

namespace {
// 既定値 (Settings の初期値と一致させる)。
constexpr int  kDefVolume   = 80;
constexpr bool kDefLoop     = false;
constexpr bool kDefAutoplay = true;
const QStringList kDefExtensions = {
  "mp4", "mov", "m4v", "webm", "avi", "mkv",
  "wav", "mp3", "m4a", "flac", "ogg", "aac",
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
  outer->addLayout(form);

  m_autoplayCheck = new QCheckBox(tr("Start playback automatically"), this);
  outer->addWidget(m_autoplayCheck);

  m_loopCheck = new QCheckBox(tr("Loop playback"), this);
  outer->addWidget(m_loopCheck);
  outer->addStretch();

  Settings& s = Settings::instance();
  s.load();
  applyValuesToUi(s.mediaViewerExtensions(), s.mediaViewerVolume(),
                  s.mediaViewerLoop(), s.mediaViewerAutoplay());
}

void MediaViewerSettingsPage::applyValuesToUi(const QStringList& extensions,
                                              int volume, bool loop,
                                              bool autoplay) {
  m_extensionsEdit->setText(joinExtensionsText(extensions));
  m_volumeSpin->setValue(volume);
  m_loopCheck->setChecked(loop);
  m_autoplayCheck->setChecked(autoplay);
}

void MediaViewerSettingsPage::save() {
  Settings& s = Settings::instance();
  const QStringList exts = parseExtensionsText(m_extensionsEdit->text());
  if (!exts.isEmpty()) s.setMediaViewerExtensions(exts);
  s.setMediaViewerVolume(m_volumeSpin->value());
  s.setMediaViewerLoop(m_loopCheck->isChecked());
  s.setMediaViewerAutoplay(m_autoplayCheck->isChecked());
  s.save();
}

void MediaViewerSettingsPage::restoreDefaults() {
  applyValuesToUi(kDefExtensions, kDefVolume, kDefLoop, kDefAutoplay);
}

} // namespace Farman
