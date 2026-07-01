#include "ImageViewerSettingsPage.h"

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
constexpr int  kDefZoomPercent = 100;
constexpr bool kDefFit          = true;
constexpr bool kDefAnimation    = false;
// 既定の対応拡張子 (Settings::m_imageViewerExtensions と一致させる)。
const QStringList kDefExtensions = {
  "png", "jp*g", "gif", "bmp", "svg", "webp", "ico", "tif*", "psd"
};
} // namespace

ImageViewerSettingsPage::ImageViewerSettingsPage(QWidget* parent)
  : IPluginSettingsPage(parent) {
  auto* outer = new QVBoxLayout(this);

  // ズームはラベル付きフォーム行。チェックボックスはフォームの「フィールド列」
  // に入れるとラベル分インデントされ、ズームに関連した項目のように見えてしまう
  // ため、フォームの外 (左端) に独立して並べる。
  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setContentsMargins(0, 0, 0, 0);
  m_extensionsEdit = new QLineEdit(this);
  m_extensionsEdit->setToolTip(
    tr("Comma, semicolon, or space separated extensions without leading dots."));
  form->addRow(tr("Extensions:"), m_extensionsEdit);
  m_zoomCombo = new QComboBox(this);
  m_zoomCombo->setEditable(true);
  for (int p : { 25, 50, 75, 100, 200 }) {
    m_zoomCombo->addItem(QString::number(p) + QLatin1Char('%'), p);
  }
  m_zoomCombo->setToolTip(
    tr("Default zoom factor (used when 'Fit to window' is off)"));
  form->addRow(tr("Zoom:"), m_zoomCombo);
  outer->addLayout(form);

  m_fitCheck = new QCheckBox(tr("Fit image to window"), this);
  m_fitCheck->setToolTip(tr(
    "Scale the image to fit within the viewer; zoom factor is ignored "
    "while this is on."));
  outer->addWidget(m_fitCheck);

  m_animCheck = new QCheckBox(tr("Play animation (GIF / WebP)"), this);
  outer->addWidget(m_animCheck);
  outer->addStretch();

  // 現在値を読み込む。プラグイン dylib 側の Settings インスタンスをファイルから
  // 読み直してから取る (本体での変更を取りこぼさないため)。
  Settings& s = Settings::instance();
  s.load();
  applyValuesToUi(s.imageViewerExtensions(), s.imageViewerZoomPercent(),
                  s.imageViewerFitToWindow(), s.imageViewerAnimation());
}

void ImageViewerSettingsPage::applyValuesToUi(const QStringList& extensions,
                                              int zoomPercent, bool fit,
                                              bool animation) {
  m_extensionsEdit->setText(joinExtensionsText(extensions));
  m_zoomCombo->setCurrentText(QString::number(zoomPercent) + QLatin1Char('%'));
  m_fitCheck->setChecked(fit);
  m_animCheck->setChecked(animation);
}

void ImageViewerSettingsPage::save() {
  Settings& s = Settings::instance();

  const QStringList exts = parseExtensionsText(m_extensionsEdit->text());
  if (!exts.isEmpty()) s.setImageViewerExtensions(exts);

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
  applyValuesToUi(kDefExtensions, kDefZoomPercent, kDefFit, kDefAnimation);
}

} // namespace Farman
