#include "ImageViewerSettingsPage.h"

#include "settings/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace Farman {

namespace {
// 既定値 (Settings の初期値と一致させる)。
constexpr int  kDefZoomPercent = 100;
constexpr bool kDefFit         = false;
constexpr bool kDefAnimation   = false;
} // namespace

ImageViewerSettingsPage::ImageViewerSettingsPage(QWidget* parent)
  : IPluginSettingsPage(parent) {
  auto* outer = new QVBoxLayout(this);

  auto* row = new QHBoxLayout();
  row->setSpacing(12);
  row->addWidget(new QLabel(tr("Zoom:"), this));
  m_zoomCombo = new QComboBox(this);
  m_zoomCombo->setEditable(true);
  for (int p : { 25, 50, 75, 100, 200 }) {
    m_zoomCombo->addItem(QString::number(p) + QLatin1Char('%'), p);
  }
  m_zoomCombo->setToolTip(
    tr("Default zoom factor (used when 'Fit to window' is off)"));
  row->addWidget(m_zoomCombo);

  m_fitCheck = new QCheckBox(tr("Fit image to window"), this);
  m_fitCheck->setToolTip(tr(
    "Scale the image to fit within the viewer; zoom factor is ignored "
    "while this is on."));
  row->addWidget(m_fitCheck);

  m_animCheck = new QCheckBox(tr("Play animation (GIF / WebP)"), this);
  row->addWidget(m_animCheck);
  row->addStretch();
  outer->addLayout(row);
  outer->addStretch();

  // 現在値を読み込む。プラグイン dylib 側の Settings インスタンスをファイルから
  // 読み直してから取る (本体での変更を取りこぼさないため)。
  Settings& s = Settings::instance();
  s.load();
  applyValuesToUi(s.imageViewerZoomPercent(), s.imageViewerFitToWindow(),
                  s.imageViewerAnimation());
}

void ImageViewerSettingsPage::applyValuesToUi(int zoomPercent, bool fit,
                                              bool animation) {
  m_zoomCombo->setCurrentText(QString::number(zoomPercent) + QLatin1Char('%'));
  m_fitCheck->setChecked(fit);
  m_animCheck->setChecked(animation);
}

void ImageViewerSettingsPage::save() {
  Settings& s = Settings::instance();

  bool ok = false;
  const int zoom =
    m_zoomCombo->currentText().remove(QLatin1Char('%')).trimmed().toInt(&ok);
  if (ok && zoom > 0) s.setImageViewerZoomPercent(zoom);

  s.setImageViewerFitToWindow(m_fitCheck->isChecked());
  s.setImageViewerAnimation(m_animCheck->isChecked());

  // ファイルへ永続化 (本体側はこの後 Settings::load() で拾う)。
  s.save();
}

void ImageViewerSettingsPage::restoreDefaults() {
  applyValuesToUi(kDefZoomPercent, kDefFit, kDefAnimation);
}

} // namespace Farman
