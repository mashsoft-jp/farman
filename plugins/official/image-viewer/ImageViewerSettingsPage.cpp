#include "ImageViewerSettingsPage.h"

#include "settings/Settings.h"
#include "viewer/ExtensionsField.h"
#include "viewer/ViewerThemeFields.h"
#include "types.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace Farman {

namespace {
// 既定値 (Settings の初期値と一致させる)。
constexpr int  kDefZoomPercent = 100;
constexpr bool kDefFit          = true;
constexpr bool kDefAnimation    = false;
const QColor   kDefChecker1(0xC8, 0xC8, 0xC8);
const QColor   kDefChecker2(0xF0, 0xF0, 0xF0);
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

  outer->addWidget(buildTransparencySection());
  outer->addStretch();

  // 現在値を読み込む。プラグイン dylib 側の Settings インスタンスをファイルから
  // 読み直してから取る (本体での変更を取りこぼさないため)。
  Settings& s = Settings::instance();
  s.load();
  applyValuesToUi(s.imageViewerExtensions(), s.imageViewerZoomPercent(),
                  s.imageViewerFitToWindow(), s.imageViewerAnimation());

  // 透過 (テーマ非依存)
  (s.imageViewerTransparencyMode() == ImageTransparencyMode::SolidColor
     ? m_solidRadio : m_checkerRadio)->setChecked(true);
  m_uiChecker1 = s.imageViewerCheckerColor1();
  m_uiChecker2 = s.imageViewerCheckerColor2();
  styleThemeColorButton(m_checker1Button, m_uiChecker1);
  styleThemeColorButton(m_checker2Button, m_uiChecker2);

  // 透過 Solid 色 (テーマ依存)
  m_light = s.scheme(ThemeMode::Light);
  m_dark  = s.scheme(ThemeMode::Dark);
  m_editSide = s.effectiveTheme();
  (m_editSide == ThemeMode::Dark ? m_themeDarkRadio : m_themeLightRadio)
    ->setChecked(true);
  loadSideToUi(m_editSide);
}

QWidget* ImageViewerSettingsPage::buildTransparencySection() {
  auto* group = new QGroupBox(tr("Transparency"), this);
  auto* outer = new QVBoxLayout(group);

  // ── Checker (テーマ非依存の 2 色) ──
  auto* checkerRow = new QHBoxLayout();
  m_checkerRadio = new QRadioButton(tr("Checker"), group);
  checkerRow->addWidget(m_checkerRadio);
  m_checker1Button = new QPushButton(group);
  m_checker1Button->setFixedWidth(100);
  m_checker2Button = new QPushButton(group);
  m_checker2Button->setFixedWidth(100);
  wireColorButton(m_checker1Button, &m_uiChecker1, tr("Checker Color 1"));
  wireColorButton(m_checker2Button, &m_uiChecker2, tr("Checker Color 2"));
  checkerRow->addWidget(new QLabel(tr("Color 1:"), group));
  checkerRow->addWidget(m_checker1Button);
  checkerRow->addWidget(new QLabel(tr("Color 2:"), group));
  checkerRow->addWidget(m_checker2Button);
  checkerRow->addStretch();
  outer->addLayout(checkerRow);

  // ── Solid (テーマ依存: Light/Dark トグル + 1 色) ──
  auto* solidRow = new QHBoxLayout();
  m_solidRadio = new QRadioButton(tr("Solid Color"), group);
  solidRow->addWidget(m_solidRadio);
  solidRow->addSpacing(12);
  solidRow->addWidget(new QLabel(tr("Theme:"), group));
  m_themeLightRadio = new QRadioButton(tr("Light"), group);
  m_themeDarkRadio  = new QRadioButton(tr("Dark"), group);
  auto* themeGroup = new QButtonGroup(this);
  themeGroup->addButton(m_themeLightRadio);
  themeGroup->addButton(m_themeDarkRadio);
  solidRow->addWidget(m_themeLightRadio);
  solidRow->addWidget(m_themeDarkRadio);
  m_solidButton = new QPushButton(group);
  m_solidButton->setFixedWidth(100);
  wireColorButton(m_solidButton, &m_uiSolid, tr("Solid Color"));
  solidRow->addWidget(new QLabel(tr("Color:"), group));
  solidRow->addWidget(m_solidButton);
  solidRow->addStretch();
  outer->addLayout(solidRow);

  // Checker/Solid の選択は排他 (どちらの塗り方を使うか)。
  auto* modeGroup = new QButtonGroup(this);
  modeGroup->addButton(m_checkerRadio);
  modeGroup->addButton(m_solidRadio);

  auto onThemeToggled = [this]() {
    const ThemeMode newSide =
      m_themeDarkRadio->isChecked() ? ThemeMode::Dark : ThemeMode::Light;
    if (newSide == m_editSide) return;
    commitUiToSide(m_editSide);
    m_editSide = newSide;
    loadSideToUi(m_editSide);
  };
  connect(m_themeLightRadio, &QRadioButton::toggled, this, onThemeToggled);
  connect(m_themeDarkRadio,  &QRadioButton::toggled, this, onThemeToggled);

  return group;
}

void ImageViewerSettingsPage::wireColorButton(QPushButton* btn,
                                              QColor* storedValue,
                                              const QString& title) {
  connect(btn, &QPushButton::clicked, this, [this, btn, storedValue, title]() {
    const QColor picked = QColorDialog::getColor(
      storedValue->isValid() ? *storedValue : QColor(Qt::white),
      this, title, QColorDialog::ShowAlphaChannel);
    if (picked.isValid()) {
      *storedValue = picked;
      styleThemeColorButton(btn, picked);
    }
  });
}

void ImageViewerSettingsPage::loadSideToUi(ThemeMode side) {
  const ColorScheme& s = (side == ThemeMode::Dark) ? m_dark : m_light;
  m_uiSolid = s.imageViewerSolidColor;
  styleThemeColorButton(m_solidButton, m_uiSolid);
}

void ImageViewerSettingsPage::commitUiToSide(ThemeMode side) {
  ColorScheme& s = (side == ThemeMode::Dark) ? m_dark : m_light;
  s.imageViewerSolidColor = m_uiSolid;
}

void ImageViewerSettingsPage::overlayFields(const ColorScheme& src,
                                            ColorScheme& dst) {
  dst.imageViewerSolidColor = src.imageViewerSolidColor;
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

  // 透過 (テーマ非依存)
  s.setImageViewerTransparencyMode(m_solidRadio->isChecked()
    ? ImageTransparencyMode::SolidColor
    : ImageTransparencyMode::Checker);
  s.setImageViewerCheckerColor1(m_uiChecker1);
  s.setImageViewerCheckerColor2(m_uiChecker2);

  // 透過 Solid 色 (テーマ依存): 両スキームを RMW overlay で書き戻す。
  commitUiToSide(m_editSide);
  ColorScheme fresh = s.scheme(ThemeMode::Light);
  overlayFields(m_light, fresh);
  s.setScheme(ThemeMode::Light, fresh);
  fresh = s.scheme(ThemeMode::Dark);
  overlayFields(m_dark, fresh);
  s.setScheme(ThemeMode::Dark, fresh);

  // ファイルへ永続化 (本体側はこの後 Settings::load() で拾う)。
  s.save();
}

void ImageViewerSettingsPage::restoreDefaults() {
  applyValuesToUi(kDefExtensions, kDefZoomPercent, kDefFit, kDefAnimation);

  m_checkerRadio->setChecked(true);
  m_uiChecker1 = kDefChecker1;
  m_uiChecker2 = kDefChecker2;
  styleThemeColorButton(m_checker1Button, m_uiChecker1);
  styleThemeColorButton(m_checker2Button, m_uiChecker2);

  const ColorScheme dl = defaultLightScheme();
  const ColorScheme dd = defaultDarkScheme();
  overlayFields(dl, m_light);
  overlayFields(dd, m_dark);
  loadSideToUi(m_editSide);
}

} // namespace Farman
