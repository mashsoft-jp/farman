#include "TextViewerSettingsPage.h"

#include "settings/Settings.h"
#include "viewer/ExtensionsField.h"
#include "viewer/ViewerThemeFields.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFontDatabase>
#include <QFontDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QRadioButton>
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

  outer->addWidget(buildThemeColorSection());
  outer->addStretch();

  Settings& s = Settings::instance();
  s.load();
  applyValuesToUi(s.textViewerExtensions(), s.textViewerEncoding(),
                  s.textViewerShowLineNumbers(), s.textViewerWordWrap());

  // テーマ依存フィールド: Light/Dark 双方のスキームをシャドーに取り込み、
  // 現在有効なテーマ側を初期表示する。
  m_light = s.scheme(ThemeMode::Light);
  m_dark  = s.scheme(ThemeMode::Dark);
  m_editSide = s.effectiveTheme();
  (m_editSide == ThemeMode::Dark ? m_themeDarkRadio : m_themeLightRadio)
    ->setChecked(true);
  // フォントはテーマ非依存。両スキーム同値なのでどちらから読んでも良い。
  m_uiFont = m_light.textViewerFont;
  styleThemeFontButton(m_fontButton, m_uiFont);
  loadSideToUi(m_editSide);
}

QWidget* TextViewerSettingsPage::buildThemeColorSection() {
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
  m_fontButton->setToolTip(tr("Choose the font for the text viewer"));
  connect(m_fontButton, &QPushButton::clicked, this, [this]() {
    bool ok = false;
    const QFont chosen = QFontDialog::getFont(&ok, m_uiFont, this,
                                              tr("Text Viewer Font"));
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
    commitUiToSide(m_editSide);   // 旧側へ書き戻し
    m_editSide = newSide;
    loadSideToUi(m_editSide);     // 新側を表示
  };
  connect(m_themeLightRadio, &QRadioButton::toggled, this, onToggled);
  connect(m_themeDarkRadio,  &QRadioButton::toggled, this, onToggled);

  // ── カラー (Normal / Selected / 行番号) ──
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
  addColorRow(tr("Line Number:"), m_lineNumberFgButton, &m_uiLineNumberFg,
              m_lineNumberBgButton, &m_uiLineNumberBg,
              tr("Line Number Foreground"), tr("Line Number Background"));
  outer->addLayout(colors);
  return group;
}

void TextViewerSettingsPage::wireColorButton(QPushButton* btn,
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

void TextViewerSettingsPage::loadSideToUi(ThemeMode side) {
  // フォントはテーマ非依存 (Light/Dark 共通) なので、テーマ切替では読み直さない。
  // 色のみを side スキームから読み込む。
  const ColorScheme& s = (side == ThemeMode::Dark) ? m_dark : m_light;
  m_uiNormalFg     = s.textViewerNormalFg;
  m_uiNormalBg     = s.textViewerNormalBg;
  m_uiSelectedFg   = s.textViewerSelectedFg;
  m_uiSelectedBg   = s.textViewerSelectedBg;
  m_uiLineNumberFg = s.textViewerLineNumberFg;
  m_uiLineNumberBg = s.textViewerLineNumberBg;
  // 未設定 (無効色) のときに実際に使われるテーマ既定色をボタン背景に出す。
  const QPalette pal = Settings::paletteForScheme(
    (side == ThemeMode::Dark) ? m_dark : m_light);
  styleThemeColorButton(m_normalFgButton,     m_uiNormalFg,     pal.color(QPalette::Text));
  styleThemeColorButton(m_normalBgButton,     m_uiNormalBg,     pal.color(QPalette::Base));
  styleThemeColorButton(m_selectedFgButton,   m_uiSelectedFg,   pal.color(QPalette::HighlightedText));
  styleThemeColorButton(m_selectedBgButton,   m_uiSelectedBg,   pal.color(QPalette::Highlight));
  styleThemeColorButton(m_lineNumberFgButton, m_uiLineNumberFg, pal.color(QPalette::Text));
  styleThemeColorButton(m_lineNumberBgButton, m_uiLineNumberBg, pal.color(QPalette::Base));
}

void TextViewerSettingsPage::commitUiToSide(ThemeMode side) {
  // フォントはテーマ非依存なので side には書かない (色のみ)。
  ColorScheme& s = (side == ThemeMode::Dark) ? m_dark : m_light;
  s.textViewerNormalFg     = m_uiNormalFg;
  s.textViewerNormalBg     = m_uiNormalBg;
  s.textViewerSelectedFg   = m_uiSelectedFg;
  s.textViewerSelectedBg   = m_uiSelectedBg;
  s.textViewerLineNumberFg = m_uiLineNumberFg;
  s.textViewerLineNumberBg = m_uiLineNumberBg;
}

void TextViewerSettingsPage::overlayFields(const ColorScheme& src,
                                           ColorScheme& dst) {
  // フォントは save() で m_uiFont を両スキームへ直接書くため、ここでは色のみ。
  dst.textViewerNormalFg     = src.textViewerNormalFg;
  dst.textViewerNormalBg     = src.textViewerNormalBg;
  dst.textViewerSelectedFg   = src.textViewerSelectedFg;
  dst.textViewerSelectedBg   = src.textViewerSelectedBg;
  dst.textViewerLineNumberFg = src.textViewerLineNumberFg;
  dst.textViewerLineNumberBg = src.textViewerLineNumberBg;
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

  // テーマ依存フィールド: 現在編集中側を確定してから、両スキームを
  // fresh に対する overlay (RMW) で書き戻す。
  commitUiToSide(m_editSide);
  // 色は per-theme (m_light/m_dark)、フォントは共通 (m_uiFont) を両側へ書く。
  ColorScheme fresh = s.scheme(ThemeMode::Light);
  overlayFields(m_light, fresh);
  fresh.textViewerFont = m_uiFont;
  s.setScheme(ThemeMode::Light, fresh);
  fresh = s.scheme(ThemeMode::Dark);
  overlayFields(m_dark, fresh);
  fresh.textViewerFont = m_uiFont;
  s.setScheme(ThemeMode::Dark, fresh);

  s.save();
}

void TextViewerSettingsPage::restoreDefaults() {
  applyValuesToUi(kDefExtensions, QStringLiteral("Auto"),
                  /*showLineNumbers=*/true, /*wordWrap=*/false);

  // 色 (per-theme) は出荷時テーマの値へ戻す。フォント (共通) も既定へ。
  const ColorScheme dl = defaultLightScheme();
  const ColorScheme dd = defaultDarkScheme();
  overlayFields(dl, m_light);
  overlayFields(dd, m_dark);
  m_uiFont = dl.textViewerFont;
  styleThemeFontButton(m_fontButton, m_uiFont);
  loadSideToUi(m_editSide);
}

} // namespace Farman
