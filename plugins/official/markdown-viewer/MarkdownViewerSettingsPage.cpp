#include "MarkdownViewerSettingsPage.h"

#include "settings/Settings.h"
#include "viewer/ExtensionsField.h"
#include "viewer/ViewerThemeFields.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QFontDialog>
#include <QGuiApplication>
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
// 既定値 (Settings の初期値と一致させる)。
constexpr bool kDefShowSource = false;
const QStringList kDefExtensions = { "*.md", "*.markdown", "*.mdown", "*.mkd" };
} // namespace

MarkdownViewerSettingsPage::MarkdownViewerSettingsPage(QWidget* parent)
  : IPluginSettingsPage(parent) {
  auto* outer = new QVBoxLayout(this);

  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setContentsMargins(0, 0, 0, 0);
  m_extensionsEdit = new QLineEdit(this);
  m_extensionsEdit->setToolTip(
    tr("Comma, semicolon, or space separated patterns matched against the "
    "file name (*.mp4, *.tar.gz, Makefile). Prefix with ! to exclude "
    "(!*.min.js). A bare extension (mp4) also works."));
  form->addRow(tr("File patterns:"), m_extensionsEdit);
  outer->addLayout(form);

  m_showSourceCheck = new QCheckBox(tr("Show raw source instead of preview"), this);
  m_showSourceCheck->setToolTip(
    tr("Open documents as plain Markdown text rather than rendered HTML."));
  outer->addWidget(m_showSourceCheck);

  outer->addWidget(buildThemeSection());
  outer->addStretch();

  Settings& s = Settings::instance();
  s.load();
  applyValuesToUi(s.markdownViewerExtensions(), s.markdownViewerShowSource());

  m_light = s.scheme(ThemeMode::Light);
  m_dark  = s.scheme(ThemeMode::Dark);
  m_editSide = s.effectiveTheme();
  (m_editSide == ThemeMode::Dark ? m_themeDarkRadio : m_themeLightRadio)
    ->setChecked(true);
  // フォントはテーマ非依存 (両スキーム同値)。
  m_uiFont = m_light.markdownViewerFont;
  styleThemeFontButton(m_fontButton, m_uiFont);
  loadSideToUi(m_editSide);
}

QWidget* MarkdownViewerSettingsPage::buildThemeSection() {
  auto* group = new QGroupBox(tr("Font && Colors (per theme)"), this);
  auto* outer = new QVBoxLayout(group);

  // ── テーマ切替 + フォント ──
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
  m_fontButton->setToolTip(tr("Choose the body font for the Markdown viewer"));
  connect(m_fontButton, &QPushButton::clicked, this, [this]() {
    bool ok = false;
    const QFont chosen = QFontDialog::getFont(&ok, m_uiFont, this,
                                              tr("Markdown Viewer Font"));
    if (ok) {
      m_uiFont = chosen;
      styleThemeFontButton(m_fontButton, m_uiFont);
    }
  });
  topRow->addWidget(m_fontButton);
  topRow->addStretch();
  outer->addLayout(topRow);

  // ── 文字色 / 背景色 ──
  auto* colors = new QHBoxLayout();
  colors->addWidget(new QLabel(tr("Text:"), group));
  m_fgButton = new QPushButton(group);
  m_fgButton->setFixedWidth(100);
  wireColorButton(m_fgButton, &m_uiFg, tr("Text Color"));
  colors->addWidget(m_fgButton);
  colors->addSpacing(16);
  colors->addWidget(new QLabel(tr("Background:"), group));
  m_bgButton = new QPushButton(group);
  m_bgButton->setFixedWidth(100);
  wireColorButton(m_bgButton, &m_uiBg, tr("Background Color"));
  colors->addWidget(m_bgButton);
  colors->addSpacing(16);
  colors->addWidget(new QLabel(tr("Link:"), group));
  m_linkButton = new QPushButton(group);
  m_linkButton->setFixedWidth(100);
  wireColorButton(m_linkButton, &m_uiLink, tr("Link Color"));
  colors->addWidget(m_linkButton);
  colors->addStretch();
  outer->addLayout(colors);

  auto* hint = new QLabel(
    tr("Leave a color as \"(none)\" to follow the theme's default."), group);
  hint->setEnabled(false);
  outer->addWidget(hint);

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
  return group;
}

void MarkdownViewerSettingsPage::wireColorButton(QPushButton* btn,
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

void MarkdownViewerSettingsPage::loadSideToUi(ThemeMode side) {
  // フォントはテーマ非依存 (Light/Dark 共通)。色のみ side から読む。
  const ColorScheme& s = (side == ThemeMode::Dark) ? m_dark : m_light;
  m_uiFg   = s.markdownViewerFg;
  m_uiBg   = s.markdownViewerBg;
  m_uiLink = s.markdownViewerLink;
  // 未設定 (無効色) のときに実際に使われるテーマ既定色をボタン背景に出す。
  const QPalette pal = Settings::paletteForScheme(
    (side == ThemeMode::Dark) ? m_dark : m_light);
  styleThemeColorButton(m_fgButton,   m_uiFg,   pal.color(QPalette::Text));
  styleThemeColorButton(m_bgButton,   m_uiBg,   pal.color(QPalette::Base));
  styleThemeColorButton(m_linkButton, m_uiLink, pal.color(QPalette::Link));
}

void MarkdownViewerSettingsPage::commitUiToSide(ThemeMode side) {
  // フォントはテーマ非依存なので side には書かない (色のみ)。
  ColorScheme& s = (side == ThemeMode::Dark) ? m_dark : m_light;
  s.markdownViewerFg   = m_uiFg;
  s.markdownViewerBg   = m_uiBg;
  s.markdownViewerLink = m_uiLink;
}

void MarkdownViewerSettingsPage::overlayFields(const ColorScheme& src,
                                               ColorScheme& dst) {
  // フォントは save() で m_uiFont を両スキームへ直接書くため、ここでは色のみ。
  dst.markdownViewerFg   = src.markdownViewerFg;
  dst.markdownViewerBg   = src.markdownViewerBg;
  dst.markdownViewerLink = src.markdownViewerLink;
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

  commitUiToSide(m_editSide);
  // 色は per-theme、フォント (m_uiFont) は共通で両側へ書く。
  ColorScheme fresh = s.scheme(ThemeMode::Light);
  overlayFields(m_light, fresh);
  fresh.markdownViewerFont = m_uiFont;
  s.setScheme(ThemeMode::Light, fresh);
  fresh = s.scheme(ThemeMode::Dark);
  overlayFields(m_dark, fresh);
  fresh.markdownViewerFont = m_uiFont;
  s.setScheme(ThemeMode::Dark, fresh);

  s.save();
}

void MarkdownViewerSettingsPage::restoreDefaults() {
  applyValuesToUi(kDefExtensions, kDefShowSource);

  const ColorScheme dl = defaultLightScheme();
  const ColorScheme dd = defaultDarkScheme();
  overlayFields(dl, m_light);
  overlayFields(dd, m_dark);
  m_uiFont = dl.markdownViewerFont;
  styleThemeFontButton(m_fontButton, m_uiFont);
  loadSideToUi(m_editSide);
}

} // namespace Farman
