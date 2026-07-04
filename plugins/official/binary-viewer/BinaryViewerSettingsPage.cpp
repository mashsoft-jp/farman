#include "BinaryViewerSettingsPage.h"

#include "settings/Settings.h"
#include "viewer/ViewerThemeFields.h"
#include "types.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QFontDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace Farman {

BinaryViewerSettingsPage::BinaryViewerSettingsPage(QWidget* parent)
  : IPluginSettingsPage(parent) {
  auto* outer = new QVBoxLayout(this);
  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  m_unitCombo = new QComboBox(this);
  m_unitCombo->addItem(tr("1 Byte"), 1);
  m_unitCombo->addItem(tr("2 Byte"), 2);
  m_unitCombo->addItem(tr("4 Byte"), 4);
  m_unitCombo->addItem(tr("8 Byte"), 8);
  m_unitCombo->setToolTip(tr("Number of bytes grouped per value column."));
  form->addRow(tr("Unit:"), m_unitCombo);

  m_endianCombo = new QComboBox(this);
  m_endianCombo->addItem(tr("Little Endian"),
                         static_cast<int>(BinaryViewerEndian::Little));
  m_endianCombo->addItem(tr("Big Endian"),
                         static_cast<int>(BinaryViewerEndian::Big));
  m_endianCombo->setToolTip(tr("Byte order used when grouping multiple bytes."));
  form->addRow(tr("Endian:"), m_endianCombo);

  m_encodingCombo = new QComboBox(this);
  m_encodingCombo->setEditable(true);
  for (const char* e : { "UTF-8", "UTF-16LE", "UTF-16BE", "Shift_JIS",
                         "EUC-JP", "ISO-8859-1" }) {
    m_encodingCombo->addItem(QString::fromLatin1(e));
  }
  m_encodingCombo->setToolTip(
    tr("Encoding used for the string (ASCII/text) column."));
  form->addRow(tr("String Encoding:"), m_encodingCombo);

  outer->addLayout(form);
  outer->addWidget(buildThemeColorSection());
  outer->addStretch();

  Settings& s = Settings::instance();
  s.load();
  applyValuesToUi(binaryViewerUnitToBytes(s.binaryViewerUnit()),
                  static_cast<int>(s.binaryViewerEndian()),
                  s.binaryViewerEncoding());

  m_light = s.scheme(ThemeMode::Light);
  m_dark  = s.scheme(ThemeMode::Dark);
  m_editSide = s.effectiveTheme();
  (m_editSide == ThemeMode::Dark ? m_themeDarkRadio : m_themeLightRadio)
    ->setChecked(true);
  loadSideToUi(m_editSide);
}

QWidget* BinaryViewerSettingsPage::buildThemeColorSection() {
  auto* group = new QGroupBox(tr("Font && Colors (per theme)"), this);
  auto* outer = new QVBoxLayout(group);

  // ── テーマ切替 (Light / Dark) + フォント (同じ行) ──
  auto* topRow = new QHBoxLayout();
  topRow->addWidget(new QLabel(tr("Theme:"), group));
  m_themeLightRadio = new QRadioButton(tr("Light"), group);
  m_themeDarkRadio  = new QRadioButton(tr("Dark"), group);
  auto* themeGroup = new QButtonGroup(this);
  themeGroup->addButton(m_themeLightRadio);
  themeGroup->addButton(m_themeDarkRadio);
  topRow->addWidget(m_themeLightRadio);
  topRow->addWidget(m_themeDarkRadio);
  topRow->addSpacing(16);
  topRow->addWidget(new QLabel(tr("Font:"), group));
  m_fontButton = new QPushButton(group);
  m_fontButton->setToolTip(tr("Choose the font for the binary viewer hex dump"));
  connect(m_fontButton, &QPushButton::clicked, this, [this]() {
    bool ok = false;
    const QFont chosen = QFontDialog::getFont(&ok, m_uiFont, this,
                                              tr("Binary Viewer Font"));
    if (ok) {
      m_uiFont = chosen;
      styleThemeFontButton(m_fontButton, m_uiFont);
    }
  });
  topRow->addWidget(m_fontButton);
  topRow->addStretch();
  outer->addLayout(topRow);

  auto onToggled = [this]() {
    const ThemeMode newSide =
      m_themeDarkRadio->isChecked() ? ThemeMode::Dark : ThemeMode::Light;
    if (newSide == m_editSide) return;
    commitUiToSide(m_editSide);
    m_editSide = newSide;
    loadSideToUi(m_editSide);
  };
  connect(m_themeLightRadio, &QRadioButton::toggled, this, onToggled);
  connect(m_themeDarkRadio,  &QRadioButton::toggled, this, onToggled);

  auto* colors = new QGridLayout();
  colors->setColumnStretch(3, 1);
  int r = 0;
  colors->addWidget(new QLabel(tr("Foreground"), group), r, 1, Qt::AlignCenter);
  colors->addWidget(new QLabel(tr("Background"), group), r, 2, Qt::AlignCenter);
  ++r;
  auto addColorRow = [&](const QString& label, QPushButton*& fg, QColor* fgVal,
                         QPushButton*& bg, QColor* bgVal,
                         const QString& fgTitle, const QString& bgTitle) {
    fg = new QPushButton(group);
    fg->setFixedWidth(100);
    bg = new QPushButton(group);
    bg->setFixedWidth(100);
    wireColorButton(fg, fgVal, fgTitle);
    wireColorButton(bg, bgVal, bgTitle);
    colors->addWidget(new QLabel(label, group), r, 0);
    colors->addWidget(fg, r, 1);
    colors->addWidget(bg, r, 2);
    ++r;
  };
  addColorRow(tr("Normal:"), m_normalFgButton, &m_uiNormalFg,
              m_normalBgButton, &m_uiNormalBg,
              tr("Normal Foreground"), tr("Normal Background"));
  addColorRow(tr("Selected:"), m_selectedFgButton, &m_uiSelectedFg,
              m_selectedBgButton, &m_uiSelectedBg,
              tr("Selected Foreground"), tr("Selected Background"));
  addColorRow(tr("Address:"), m_addressFgButton, &m_uiAddressFg,
              m_addressBgButton, &m_uiAddressBg,
              tr("Address Foreground"), tr("Address Background"));
  outer->addLayout(colors);
  return group;
}

void BinaryViewerSettingsPage::wireColorButton(QPushButton* btn,
                                               QColor* storedValue,
                                               const QString& title) {
  connect(btn, &QPushButton::clicked, this, [this, btn, storedValue, title]() {
    const QColor picked = QColorDialog::getColor(
      storedValue->isValid() ? *storedValue : QColor(Qt::black),
      this, title, QColorDialog::ShowAlphaChannel);
    if (picked.isValid()) {
      *storedValue = picked;
      styleThemeColorButton(btn, picked);
    }
  });
}

void BinaryViewerSettingsPage::loadSideToUi(ThemeMode side) {
  const ColorScheme& s = (side == ThemeMode::Dark) ? m_dark : m_light;
  m_uiFont       = s.binaryViewerFont;
  m_uiNormalFg   = s.binaryViewerNormalFg;
  m_uiNormalBg   = s.binaryViewerNormalBg;
  m_uiSelectedFg = s.binaryViewerSelectedFg;
  m_uiSelectedBg = s.binaryViewerSelectedBg;
  m_uiAddressFg  = s.binaryViewerAddressFg;
  m_uiAddressBg  = s.binaryViewerAddressBg;
  styleThemeFontButton(m_fontButton, m_uiFont);
  // 未設定 (無効色) のときに実際に使われるテーマ既定色をボタン背景に出す。
  const QPalette pal = Settings::paletteForScheme(
    (side == ThemeMode::Dark) ? m_dark : m_light);
  styleThemeColorButton(m_normalFgButton,   m_uiNormalFg,   pal.color(QPalette::Text));
  styleThemeColorButton(m_normalBgButton,   m_uiNormalBg,   pal.color(QPalette::Base));
  styleThemeColorButton(m_selectedFgButton, m_uiSelectedFg, pal.color(QPalette::HighlightedText));
  styleThemeColorButton(m_selectedBgButton, m_uiSelectedBg, pal.color(QPalette::Highlight));
  styleThemeColorButton(m_addressFgButton,  m_uiAddressFg,  pal.color(QPalette::Text));
  styleThemeColorButton(m_addressBgButton,  m_uiAddressBg,  pal.color(QPalette::Base));
}

void BinaryViewerSettingsPage::commitUiToSide(ThemeMode side) {
  ColorScheme& s = (side == ThemeMode::Dark) ? m_dark : m_light;
  s.binaryViewerFont       = m_uiFont;
  s.binaryViewerNormalFg   = m_uiNormalFg;
  s.binaryViewerNormalBg   = m_uiNormalBg;
  s.binaryViewerSelectedFg = m_uiSelectedFg;
  s.binaryViewerSelectedBg = m_uiSelectedBg;
  s.binaryViewerAddressFg  = m_uiAddressFg;
  s.binaryViewerAddressBg  = m_uiAddressBg;
}

void BinaryViewerSettingsPage::overlayFields(const ColorScheme& src,
                                             ColorScheme& dst) {
  dst.binaryViewerFont       = src.binaryViewerFont;
  dst.binaryViewerNormalFg   = src.binaryViewerNormalFg;
  dst.binaryViewerNormalBg   = src.binaryViewerNormalBg;
  dst.binaryViewerSelectedFg = src.binaryViewerSelectedFg;
  dst.binaryViewerSelectedBg = src.binaryViewerSelectedBg;
  dst.binaryViewerAddressFg  = src.binaryViewerAddressFg;
  dst.binaryViewerAddressBg  = src.binaryViewerAddressBg;
}

void BinaryViewerSettingsPage::applyValuesToUi(int unitBytes, int endian,
                                               const QString& encoding) {
  for (int i = 0; i < m_unitCombo->count(); ++i) {
    if (m_unitCombo->itemData(i).toInt() == unitBytes) {
      m_unitCombo->setCurrentIndex(i);
      break;
    }
  }
  for (int i = 0; i < m_endianCombo->count(); ++i) {
    if (m_endianCombo->itemData(i).toInt() == endian) {
      m_endianCombo->setCurrentIndex(i);
      break;
    }
  }
  m_encodingCombo->setCurrentText(encoding);
}

void BinaryViewerSettingsPage::save() {
  Settings& s = Settings::instance();
  s.setBinaryViewerUnit(bytesToBinaryViewerUnit(m_unitCombo->currentData().toInt()));
  s.setBinaryViewerEndian(
    static_cast<BinaryViewerEndian>(m_endianCombo->currentData().toInt()));
  s.setBinaryViewerEncoding(m_encodingCombo->currentText().trimmed());

  commitUiToSide(m_editSide);
  ColorScheme fresh = s.scheme(ThemeMode::Light);
  overlayFields(m_light, fresh);
  s.setScheme(ThemeMode::Light, fresh);
  fresh = s.scheme(ThemeMode::Dark);
  overlayFields(m_dark, fresh);
  s.setScheme(ThemeMode::Dark, fresh);

  s.save();
}

void BinaryViewerSettingsPage::restoreDefaults() {
  applyValuesToUi(/*unitBytes=*/1, static_cast<int>(BinaryViewerEndian::Little),
                  QStringLiteral("UTF-8"));

  const ColorScheme dl = defaultLightScheme();
  const ColorScheme dd = defaultDarkScheme();
  overlayFields(dl, m_light);
  overlayFields(dd, m_dark);
  loadSideToUi(m_editSide);
}

} // namespace Farman
