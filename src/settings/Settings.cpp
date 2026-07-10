#include "Settings.h"
#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <QSize>
#include <QPoint>

namespace Farman {

// この Settings.cpp をコンパイルしたバイナリが「本体 (farman 実行ファイル)」か
// どうか。本体は main() で setHostApplication() を呼んで true にする。各プラグイン
// dylib は自前の Settings.cpp を持ち、この静的変数は false のまま → プラグインは
// applyThemeFields で qApp を触らない (本体だけが qApp パレットを管理する)。
namespace { bool g_settingsIsHostApp = false; }

void Settings::setHostApplication() { g_settingsIsHostApp = true; }

Settings& Settings::instance() {
  static Settings instance;
  return instance;
}

Settings::Settings(QObject* parent) : QObject(parent) {
  // OS のカラースキームを「私たち自身が setColorScheme で上書きする前」に
  // 一度捕まえる。これが detectOsTheme() の真の OS 状態の起点となる。
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  if (auto* hints = QGuiApplication::styleHints()) {
    const Qt::ColorScheme cs = hints->colorScheme();
    m_osColorScheme = (cs == Qt::ColorScheme::Dark) ? ThemeMode::Dark
                                                    : ThemeMode::Light;
  }
#endif

  applyDefaults();

  // OS のカラースキーム変更 (Qt 6.5+) を監視。
  // 注意: 私たちが setColorScheme(Light/Dark) を呼ぶときも colorSchemeChanged
  // が発火するので、信号の値だけで「OS が変わった」とは判断できない。
  // Auto モードのときだけ受け付け、かつ検出した OS 状態が前回と異なれば
  // 切替えるロジックは下のとおり。
  // m_osColorScheme は signal 経由で更新するが、この更新も「OS が変わった」
  // 場合と「我々が setColorScheme した」場合の両方で起きうる。Auto モード時
  // の整合だけ守れればよいので、ここでは単純に最新値で上書きする。
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  if (auto* hints = QGuiApplication::styleHints()) {
    connect(hints, &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme cs) {
      // Auto モード以外では、信号は私たちの setColorScheme による副次的な
      // ものなので、OS 状態キャッシュを更新しない (我々のロックが OS 状態と
      // 誤認されるのを防ぐ)。
      if (m_themeMode != ThemeMode::Auto) return;

      // Auto モード時: setColorScheme(Unknown) で OS 追従にしているので、
      // この信号は OS が真に変わったときのみ来る (はず)。
      m_osColorScheme = (cs == Qt::ColorScheme::Dark) ? ThemeMode::Dark
                                                      : ThemeMode::Light;
      const ThemeMode now = m_osColorScheme;
      if (now == m_lastEffective) return;
      ColorScheme snapshot = collectThemeFields();
      if (m_lastEffective == ThemeMode::Light) m_lightScheme = snapshot;
      else                                     m_darkScheme  = snapshot;
      m_lastEffective = now;
      applyThemeFields(now == ThemeMode::Light ? m_lightScheme : m_darkScheme);
      emit settingsChanged();
    });
  }
#endif
}

void Settings::resetToDefaults() {
  applyDefaults();
  // 先にファイルへ書き出してから settingsChanged を発火する。
  // settingsChanged は各プラグインの appearanceChanged → syncPluginFromHostSettings
  // → Settings::load() を呼ぶが、この load はファイルを読み直して配色を再適用
  // するため、保存前に発火すると「リセット前の古い (ダーク) ファイル」を読んで
  // せっかくリセットした palette を巻き戻してしまう (再起動でしか直らない)。
  save();
  emit settingsChanged();
}

void Settings::applyDefaults() {
  // ── フォント ──
  // OS 標準の UI フォント (.AppleSystemUIFont 等) は QFontDialog に出ない隠し
  // フォントなので使わず、一覧に出る一般的な具体フォント (defaultUiFont /
  // defaultMonospaceFont) を既定にする。ユーザーが後で選び直せる。
  m_uiFont           = defaultUiGeneralFont();  // UI 全般 (QApplication::setFont)
  m_font             = defaultUiFont();
  m_addressFont      = defaultUiFont();
  m_textViewerFont   = defaultViewerMonospaceFont();
  m_binaryViewerFont = defaultViewerMonospaceFont();
  m_csvViewerFont    = defaultMonospaceFont();
  m_markdownViewerFont = defaultUiFont();

  // ── 表示設定 ─────────────────────
  m_fileSizeFormatDual         = FileSizeFormat::Auto;
  m_fileSizeFormatSingle       = FileSizeFormat::Auto;
  m_fileSizeThousandsSeparatorDual   = true;
  m_fileSizeThousandsSeparatorSingle = true;
  m_fileListRowHeight          = 0;
  m_dateTimeFormatDual         = QStringLiteral("yyyy/MM/dd HH:mm:ss");
  m_dateTimeFormatSingle       = QStringLiteral("yyyy/MM/dd HH:mm:ss");

  // 列表示の既定値: 2 画面は従来通り 4 列、1 画面はもう少し情報量を増やす。
  m_listColumnsDual = ListColumnVisibility{};   // type/size/modified ON 既定
  m_listColumnsSingle = ListColumnVisibility{};
  m_listColumnsSingle.created     = true;
  m_listColumnsSingle.permissions = true;
  m_listColumnsSingle.owner       = true;
  m_listColumnsSingle.group       = true;
  m_colorRules.clear();
  m_useInactivePaneColors = false;

  // Address bar
  m_addressForeground = QColor(Qt::black);
  m_addressBackground = QColor(0xE0, 0xE0, 0xE0);
  // アーカイブ内ブラウジング中のアドレスバー (Light テーマの既定値)
  m_archiveAddressForeground = QColor(Qt::black);
  m_archiveAddressBackground = QColor(0xFF, 0xE9, 0xA8);
  // ディレクトリ比較の着色 (Light テーマの既定値)
  m_compareDifferForeground   = QColor(Qt::black);
  m_compareDifferBackground   = QColor(0xFF, 0xD8, 0xA8);
  m_compareOnlyHereForeground = QColor(Qt::black);
  m_compareOnlyHereBackground = QColor(0xC8, 0xE6, 0xB4);

  // Cursor
  m_cursorActiveColor   = QColor(Qt::black);
  m_cursorInactiveColor = QColor(Qt::lightGray);
  m_cursorShape         = CursorShape::Underline;
  m_cursorThickness     = 2;

  // ── 起動 ─────────────────────────
  m_initialPathMode[static_cast<int>(PaneType::Left)]  = InitialPathMode::LastSession;
  m_initialPathMode[static_cast<int>(PaneType::Right)] = InitialPathMode::LastSession;
  m_customInitialPath[static_cast<int>(PaneType::Left)].clear();
  m_customInitialPath[static_cast<int>(PaneType::Right)].clear();
  m_confirmOnExit = false;
  m_singleInstance = true;
  m_pluginsDirectory.clear();
  m_disabledViewerPlugins.clear();
  m_viewerAssociations.clear();
  m_viewerMode    = ViewerMode::Inline;
  m_showToolbar   = true;
  m_language      = LanguageMode::Auto;

  // ── ログ ─────────────────────────
  m_logVisible       = true;
  m_logPaneHeight    = 120;
  m_logToFile        = true;
  m_logDirectory     = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  m_logRetentionDays = 7;

  // ── ナビゲーション ───────────────
  m_cursorLoop                = false;
  m_typeAheadIncludeDotfiles  = true;

  // ── Markdown ビュアー ─────────────
  m_markdownViewerExtensions = { "md", "markdown", "mdown", "mkd" };

  m_pdfViewerExtensions = { "pdf" };

  m_csvViewerExtensions = { "csv", "tsv" };

  // ── テキストビュアー ─────────────
  m_textViewerExtensions = {
    "txt", "log",
    "c*", "!class", "!cab", "!chm", "!com",
    "h", "hpp",
    "py", "js", "ts", "java", "rs", "go", "rb", "php", "pl", "pm",
    "htm*", "json", "xml",
    "*sh", "fish",
    "yml", "yaml", "toml", "ini"
  };
  m_textViewerMimePatterns    = { "text/*", "text/plain" };
  m_textViewerEncoding        = QStringLiteral("Auto");
  m_textViewerShowLineNumbers = true;
  m_textViewerWordWrap        = false;
  m_textViewerNormalFg        = QColor(Qt::black);
  m_textViewerNormalBg        = QColor();
  m_textViewerSelectedFg      = QColor(Qt::white);
  m_textViewerSelectedBg      = QColor(0x31, 0x6A, 0xC5);
  m_textViewerLineNumberFg    = QColor(Qt::darkGray);
  m_textViewerLineNumberBg    = QColor(0xF0, 0xF0, 0xF0);

  // ── 画像ビュアー ─────────────────
  m_imageViewerExtensions       = { "png", "jp*g", "gif", "bmp", "svg", "webp", "ico", "tif*" };
  m_imageViewerMimePatterns     = { "image/*" };
  m_imageViewerZoomPercent      = 100;
  m_imageViewerFitToWindow      = true;
  m_imageViewerAnimation        = false;
  m_imageViewerTransparencyMode = ImageTransparencyMode::Checker;
  m_imageViewerSolidColor       = QColor(Qt::white);
  m_imageViewerCheckerColor1    = QColor(0xC8, 0xC8, 0xC8);
  m_imageViewerCheckerColor2    = QColor(0xF0, 0xF0, 0xF0);

  // ── バイナリビュアー ─────────────
  m_binaryViewerUnit       = BinaryViewerUnit::Byte1;
  m_binaryViewerEndian     = BinaryViewerEndian::Little;
  m_binaryViewerEncoding   = QStringLiteral("UTF-8");
  m_binaryViewerNormalFg   = QColor(Qt::black);
  m_binaryViewerNormalBg   = QColor();
  m_binaryViewerSelectedFg = QColor(Qt::white);
  m_binaryViewerSelectedBg = QColor(0x31, 0x6A, 0xC5);
  m_binaryViewerAddressFg  = QColor(Qt::darkGray);
  m_binaryViewerAddressBg  = QColor(0xF0, 0xF0, 0xF0);

  // ── PDF / CSV / Markdown / メディア ビュアー既定 ──
  m_pdfViewerContinuous       = true;
  m_pdfViewerFitMode          = PdfViewerFitMode::ActualSize;
  m_csvViewerDelimiter        = QStringLiteral("auto");
  m_csvViewerFirstRowAsHeader = false;
  m_markdownViewerShowSource  = false;
  m_mediaViewerVolume         = 80;
  m_mediaViewerLoop           = false;
  m_mediaViewerAutoplay       = true;
  m_mediaViewerFitToWindow    = true;
  m_mediaViewerZoomPercent    = 100;

  // ── 履歴・ブックマーク・ファイル操作・検索 ──
  m_persistHistory = false;
  m_paneHistory[static_cast<int>(PaneType::Left)].clear();
  m_paneHistory[static_cast<int>(PaneType::Right)].clear();
  m_autoRenameTemplate        = QStringLiteral(" ({n})");
  m_defaultDeleteToTrash      = true;
  m_progressAutoClose         = false;
  m_searchExcludeDirs         = { QStringLiteral(".*") };
  m_bookmarks.clear();
  m_defaultBookmarksInstalled = false;
  m_pathOverrides.clear();

  // 外部アプリ: 組み込み terminal / editor をプラットフォーム別デフォルトで投入。
  m_userCommands = defaultBuiltinUserCommands();

  // ── ペイン (sort/filter) ───────────
  for (int i = 0; i < static_cast<int>(PaneType::Count); ++i) {
    m_paneSettings[i] = PaneSettings{};
    m_paneSettings[i].path         = QDir::homePath();
    m_paneSettings[i].sortKey      = SortKey::Name;
    m_paneSettings[i].sortOrder    = Qt::AscendingOrder;
    m_paneSettings[i].sortKey2nd   = SortKey::None;
    m_paneSettings[i].sortDirsType = SortDirsType::First;
    m_paneSettings[i].sortDotFirst = true;
    m_paneSettings[i].sortCS       = Qt::CaseInsensitive;
    m_paneSettings[i].attrFilter   = AttrFilter::None;
  }

  // ── カテゴリカラー (active normal) ──
  m_categoryColors[static_cast<int>(FileCategory::Normal)]    = {QColor(Qt::black),       QColor(),                   false};
  m_categoryColors[static_cast<int>(FileCategory::Hidden)]    = {QColor(140, 140, 140),   QColor(),                   false};
  m_categoryColors[static_cast<int>(FileCategory::Directory)] = {QColor(30, 90, 200),     QColor(),                   true};

  // active selected
  const QColor kSelBg(0, 120, 215);
  for (int i = 0; i < static_cast<int>(FileCategory::Count); ++i) {
    m_selectedCategoryColors[i].foreground = QColor(Qt::white);
    m_selectedCategoryColors[i].background = kSelBg;
    m_selectedCategoryColors[i].bold       = m_categoryColors[i].bold;
  }

  // 非アクティブペインの初期色 (薄いグレー寄り)
  auto dim = [](const QColor& c) {
    if (!c.isValid()) return QColor();
    const int avg = (c.red() + c.green() + c.blue()) / 3;
    const int dimmed = (avg + 170) / 2;
    return QColor(dimmed, dimmed, dimmed);
  };
  for (int i = 0; i < static_cast<int>(FileCategory::Count); ++i) {
    m_inactiveCategoryColors[i].foreground = dim(m_categoryColors[i].foreground);
    m_inactiveCategoryColors[i].background = m_categoryColors[i].background;
    m_inactiveCategoryColors[i].bold       = m_categoryColors[i].bold;

    m_inactiveSelectedCategoryColors[i].foreground = QColor(Qt::white);
    m_inactiveSelectedCategoryColors[i].background = QColor(140, 140, 160);
    m_inactiveSelectedCategoryColors[i].bold       = m_selectedCategoryColors[i].bold;
  }

  // ── ウィンドウ ─────────────────
  m_windowSizeMode       = WindowSizeMode::Default;
  m_customWindowSize     = QSize(1200, 600);
  m_lastWindowSize       = QSize(1200, 600);
  m_windowPositionMode   = WindowPositionMode::Default;
  m_customWindowPosition = QPoint();
  m_lastWindowPosition   = QPoint();

  // ── テーマスキーム (Light/Dark) ─────────
  // 出荷時 Light/Dark の標準値で両スキームを初期化。実行環境の OS フォントは
  // 既に m_font に入っているのでそれをスキームへ転写する (デフォルト Light の
  // listFont は QFont() で空のため、OS 既定で上書き)。
  m_lightScheme = defaultLightScheme();
  m_darkScheme  = defaultDarkScheme();
  m_lightScheme.listFont    = m_font;
  m_lightScheme.addressFont = m_addressFont;
  m_lightScheme.textViewerFont   = m_textViewerFont;
  m_lightScheme.binaryViewerFont = m_binaryViewerFont;
  m_lightScheme.csvViewerFont      = m_csvViewerFont;
  m_lightScheme.markdownViewerFont = m_markdownViewerFont;
  // Dark 側も実行環境のフォントで揃えておく (色だけを差し替えるため)。
  m_darkScheme.listFont    = m_font;
  m_darkScheme.addressFont = m_addressFont;
  m_darkScheme.textViewerFont   = m_textViewerFont;
  m_darkScheme.binaryViewerFont = m_binaryViewerFont;
  m_darkScheme.csvViewerFont      = m_csvViewerFont;
  m_darkScheme.markdownViewerFont = m_markdownViewerFont;

  m_themeMode     = ThemeMode::Auto;
  m_lastEffective = detectOsTheme();
  // 有効テーマ側のスキームを m_ 作業コピーへ必ず反映する。以前は Dark の
  // ときだけ流し込み、Light では「applyDefaults が設定済みの Light 値がそのまま
  // 入っている」前提だったが、m_baseBackground / m_baseForeground はここで
  // 設定していないため、リセット前が Dark だとダークのベース色が m_ に残る。
  // 有効テーマが Light のとき scheme(Light) は m_lightScheme ではなく
  // collectThemeFields() (= m_) を返すため、その残存ダーク色がライトスキームと
  // して保存され、ライトモードなのにダーク配色になる不具合につながっていた。
  // 両テーマとも applyThemeFields して m_ をスキームと一致させることで解消する
  // (m_lightScheme/m_darkScheme は上で出荷時の正しい色に初期化済み)。
  applyThemeFields(m_lastEffective == ThemeMode::Dark ? m_darkScheme : m_lightScheme);
}

// ── テーマヘルパ ─────────────────────────────────
// 現在 m_ に載っているテーマ依存フィールドを ColorScheme へスナップショット
// する。save() の直前と setThemeMode() の冒頭で呼び、active スキーム側を
// 最新化する。
ColorScheme Settings::collectThemeFields() const {
  ColorScheme s;
  s.baseBackground     = m_baseBackground;
  s.baseForeground     = m_baseForeground;
  s.uiFont             = m_uiFont;
  s.listFont           = m_font;
  s.addressFont        = m_addressFont;
  s.fileListRowHeight  = m_fileListRowHeight;
  s.colorRules         = m_colorRules;
  for (size_t i = 0; i < s.categoryColors.size(); ++i) {
    s.categoryColors[i]                  = m_categoryColors[i];
    s.selectedCategoryColors[i]          = m_selectedCategoryColors[i];
    s.inactiveCategoryColors[i]          = m_inactiveCategoryColors[i];
    s.inactiveSelectedCategoryColors[i]  = m_inactiveSelectedCategoryColors[i];
  }
  s.addressForeground  = m_addressForeground;
  s.addressBackground  = m_addressBackground;
  s.archiveAddressForeground = m_archiveAddressForeground;
  s.archiveAddressBackground = m_archiveAddressBackground;
  s.compareDifferForeground   = m_compareDifferForeground;
  s.compareDifferBackground   = m_compareDifferBackground;
  s.compareOnlyHereForeground = m_compareOnlyHereForeground;
  s.compareOnlyHereBackground = m_compareOnlyHereBackground;
  s.cursorActiveColor  = m_cursorActiveColor;
  s.cursorInactiveColor= m_cursorInactiveColor;

  s.textViewerFont          = m_textViewerFont;
  s.textViewerNormalFg      = m_textViewerNormalFg;
  s.textViewerNormalBg      = m_textViewerNormalBg;
  s.textViewerSelectedFg    = m_textViewerSelectedFg;
  s.textViewerSelectedBg    = m_textViewerSelectedBg;
  s.textViewerLineNumberFg  = m_textViewerLineNumberFg;
  s.textViewerLineNumberBg  = m_textViewerLineNumberBg;

  s.imageViewerSolidColor   = m_imageViewerSolidColor;
  // checkerColor1/2 はテーマ非依存のため ColorScheme には載せない。

  s.binaryViewerFont        = m_binaryViewerFont;
  s.binaryViewerNormalFg    = m_binaryViewerNormalFg;
  s.binaryViewerNormalBg    = m_binaryViewerNormalBg;
  s.binaryViewerSelectedFg  = m_binaryViewerSelectedFg;
  s.binaryViewerSelectedBg  = m_binaryViewerSelectedBg;
  s.binaryViewerAddressFg   = m_binaryViewerAddressFg;
  s.binaryViewerAddressBg   = m_binaryViewerAddressBg;

  s.csvViewerFont           = m_csvViewerFont;
  s.markdownViewerFont      = m_markdownViewerFont;
  s.markdownViewerFg        = m_markdownViewerFg;
  s.markdownViewerBg        = m_markdownViewerBg;
  s.markdownViewerLink      = m_markdownViewerLink;
  return s;
}

// 渡された ColorScheme の値を m_ フィールドに流し込む。シグナルは出さない
// (呼び出し側で settingsChanged を必要に応じて発火)。
void Settings::applyThemeFields(const ColorScheme& s) {
  m_baseBackground  = s.baseBackground;
  m_baseForeground  = s.baseForeground;
  m_uiFont          = s.uiFont;
  m_font            = s.listFont;
  m_addressFont     = s.addressFont;
  m_fileListRowHeight = s.fileListRowHeight;
  m_colorRules      = s.colorRules;
  for (size_t i = 0; i < s.categoryColors.size(); ++i) {
    m_categoryColors[i]                 = s.categoryColors[i];
    m_selectedCategoryColors[i]         = s.selectedCategoryColors[i];
    m_inactiveCategoryColors[i]         = s.inactiveCategoryColors[i];
    m_inactiveSelectedCategoryColors[i] = s.inactiveSelectedCategoryColors[i];
  }
  m_addressForeground  = s.addressForeground;
  m_addressBackground  = s.addressBackground;
  m_archiveAddressForeground = s.archiveAddressForeground;
  m_archiveAddressBackground = s.archiveAddressBackground;
  m_compareDifferForeground   = s.compareDifferForeground;
  m_compareDifferBackground   = s.compareDifferBackground;
  m_compareOnlyHereForeground = s.compareOnlyHereForeground;
  m_compareOnlyHereBackground = s.compareOnlyHereBackground;
  m_cursorActiveColor  = s.cursorActiveColor;
  m_cursorInactiveColor= s.cursorInactiveColor;

  m_textViewerFont          = s.textViewerFont;
  m_textViewerNormalFg      = s.textViewerNormalFg;
  m_textViewerNormalBg      = s.textViewerNormalBg;
  m_textViewerSelectedFg    = s.textViewerSelectedFg;
  m_textViewerSelectedBg    = s.textViewerSelectedBg;
  m_textViewerLineNumberFg  = s.textViewerLineNumberFg;
  m_textViewerLineNumberBg  = s.textViewerLineNumberBg;

  m_imageViewerSolidColor    = s.imageViewerSolidColor;
  // checkerColor1/2 はテーマ非依存。Settings.flat フィールドが既にユーザー
  // 設定の最新値を持っているので、ここでは上書きしない (load 経路は
  // top-level imageViewer.checker* に限定する)。

  m_binaryViewerFont        = s.binaryViewerFont;
  m_binaryViewerNormalFg    = s.binaryViewerNormalFg;
  m_binaryViewerNormalBg    = s.binaryViewerNormalBg;
  m_binaryViewerSelectedFg  = s.binaryViewerSelectedFg;
  m_binaryViewerSelectedBg  = s.binaryViewerSelectedBg;
  m_binaryViewerAddressFg   = s.binaryViewerAddressFg;
  m_binaryViewerAddressBg   = s.binaryViewerAddressBg;

  m_csvViewerFont           = s.csvViewerFont;
  m_markdownViewerFont      = s.markdownViewerFont;
  m_markdownViewerFg        = s.markdownViewerFg;
  m_markdownViewerBg        = s.markdownViewerBg;
  m_markdownViewerLink      = s.markdownViewerLink;

  // QApplication 全体のパレット + 既定フォントをスキームから派生させる。
  //
  // ベース 2 色 (baseBackground / baseForeground) を起点に、Window /
  // AlternateBase / Button / Mid / Midlight / Light / Dark / Shadow 等を
  // 線形補間 (blend) で算出。Highlight / Link は Light/Dark のどちらかで
  // 適切なアクセント色をハードコードする。
  //
  // qApp への適用 (setPalette / setFont / setColorScheme) は「本体」の Settings
  // だけが行う。各プラグインは自前の Settings インスタンスを持ち、その OS テーマ
  // 判定 (detectOsTheme) は構築タイミング次第で本体の setColorScheme 上書きに
  // 汚染され得る。プラグイン側が qApp パレットを再適用すると、リセット等で本体が
  // 適用した正しいパレットを誤ったテーマで上書きしてしまう (ファイルリストだけ
  // ダークになる等)。qApp は本体が単独で管理し、プラグインは共有 qApp を継承する
  // だけにする。
  if (qApp && g_settingsIsHostApp) {
    const QColor bg = s.baseBackground.isValid() ? s.baseBackground : QColor(Qt::white);
    const bool isDark = bg.lightness() < 128;

    // Qt 6.8+: QStyleHints::setColorScheme() で macOS native chrome を
    // Light/Dark に追従させる。Auto モード時は Unknown で「OS 追従」へ戻す。
    //
    // 重要: setColorScheme() は colorSchemeChanged を同期発火する。特に Auto
    // (Unknown) では、内部 override が Unknown でも colorScheme() は OS の
    // Light/Dark を返すため、同じ Unknown を再設定するたびに信号が再発火する。
    // 本体 + 各プラグインの Settings がそれぞれ colorSchemeChanged を購読して
    // applyThemeFields を呼ぶため、これが無限再帰 → スタックオーバーフローに
    // なる (全設定リセット時に発生)。前回適用した値と異なるときだけ設定する。
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (auto* hints = QGuiApplication::styleHints()) {
      const Qt::ColorScheme target =
        (m_themeMode == ThemeMode::Auto) ? Qt::ColorScheme::Unknown
        : (isDark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
      if (static_cast<int>(target) != m_appliedColorScheme) {
        m_appliedColorScheme = static_cast<int>(target);
        hints->setColorScheme(target);
      }
    }
#endif

    qApp->setPalette(paletteForScheme(s));

    // UI フォント (汎用ウィジェット全体に伝播)。個別 setFont() してある
    // ウィジェット (ファイルリスト / アドレス / 各ビュアー) は影響を受けない。
    if (s.uiFont != QFont()) {
      qApp->setFont(s.uiFont);
    }
  }
}

QPalette Settings::paletteForScheme(const ColorScheme& s) {
  // ベース 2 色 (baseBackground / baseForeground) を起点に、Window /
  // AlternateBase / Button / Mid / Midlight / Light / Dark / Shadow 等を
  // 線形補間 (mix) で算出。Highlight / Link は Light/Dark で別のアクセント色。
  // applyThemeFields() が qApp に適用するのと同じ色を返すので、設定ダイアログが
  // 「未設定の色は結局どのテーマ色になるか」を先読み表示するのにも使える。
  const QColor bg = s.baseBackground.isValid() ? s.baseBackground : QColor(Qt::white);
  const QColor fg = s.baseForeground.isValid() ? s.baseForeground : QColor(Qt::black);
  const bool isDark = bg.lightness() < 128;
  auto mix = [&](qreal t) -> QColor {
    const qreal u = 1.0 - t;
    const int r = qBound(0, int(bg.red()   * u + fg.red()   * t), 255);
    const int g = qBound(0, int(bg.green() * u + fg.green() * t), 255);
    const int b = qBound(0, int(bg.blue()  * u + fg.blue()  * t), 255);
    return QColor(r, g, b);
  };

  QPalette p;
  // ベース 2 色 (そのまま)
  p.setColor(QPalette::Base,              bg);
  p.setColor(QPalette::Text,              fg);
  p.setColor(QPalette::WindowText,        fg);
  p.setColor(QPalette::ButtonText,        fg);
  p.setColor(QPalette::ToolTipText,       fg);
  // bg を少しずつ fg 方向へずらしたもの (グラデーション)
  p.setColor(QPalette::AlternateBase,     mix(0.04));
  p.setColor(QPalette::Window,            mix(0.06));
  p.setColor(QPalette::ToolTipBase,       mix(0.08));
  p.setColor(QPalette::Button,            mix(0.10));
  p.setColor(QPalette::Midlight,          mix(0.15));
  p.setColor(QPalette::Light,             mix(0.18));
  p.setColor(QPalette::Mid,               mix(0.28));
  p.setColor(QPalette::Dark,              mix(0.48));
  // Shadow は Light テーマでは中央寄り、Dark テーマでは bg より暗く。
  p.setColor(QPalette::Shadow,            isDark ? bg.darker(180) : mix(0.65));

  // アクセント色 (Light/Dark で別の青系)
  p.setColor(QPalette::BrightText,        QColor(Qt::red));
  p.setColor(QPalette::Highlight,         isDark ? QColor(0x26, 0x4F, 0x78)
                                                 : QColor(0x00, 0x78, 0xD7));
  p.setColor(QPalette::HighlightedText,   QColor(Qt::white));
  p.setColor(QPalette::Link,              isDark ? QColor(0x4D, 0xA1, 0xFF)
                                                 : QColor(0x00, 0x66, 0xCC));

  // Disabled (各 fg を bg 寄りに 40% ブレンドした薄い色)
  p.setColor(QPalette::Disabled, QPalette::WindowText, mix(0.40));
  p.setColor(QPalette::Disabled, QPalette::Text,       mix(0.40));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, mix(0.40));
  return p;
}

ThemeMode Settings::detectOsTheme() const {
  // プラグインの Settings は、自前の OS 判定 (m_osColorScheme) を使わず、本体が
  // 適用済みの qApp パレットの明度から有効テーマを導く。プラグインの
  // m_osColorScheme は「本体が setColorScheme で上書きした後」に構築されるため
  // 汚染されており、Auto モードで誤ったテーマ (ダーク) を選び、ビュアーの配色が
  // ライトモードでもダークになる不具合につながる。qApp パレットは本体が唯一の
  // ソースとして管理しているので、それに追従するのが正しい。
  if (!g_settingsIsHostApp && qApp) {
    return qApp->palette().color(QPalette::Base).lightness() < 128
             ? ThemeMode::Dark : ThemeMode::Light;
  }
  // 本体: m_osColorScheme は constructor で OS の値を最初に捕まえてあり、その後
  // colorSchemeChanged シグナル経由で更新される (Auto モード時のみ)。ここで
  // QStyleHints::colorScheme() を直接読むと、私たち自身の setColorScheme 上書きが
  // 残って OS 状態を取り違えるので、キャッシュした m_osColorScheme を返す。
  return m_osColorScheme;
}

ThemeMode Settings::themeMode() const { return m_themeMode; }

ThemeMode Settings::effectiveTheme() const {
  if (m_themeMode == ThemeMode::Light) return ThemeMode::Light;
  if (m_themeMode == ThemeMode::Dark)  return ThemeMode::Dark;
  return detectOsTheme();
}

void Settings::setThemeMode(ThemeMode mode) {
  if (m_themeMode == mode) return;
  // 切替前: いま m_ に乗っている値を旧 active スキームへ退避
  ColorScheme snapshot = collectThemeFields();
  if (m_lastEffective == ThemeMode::Light) m_lightScheme = snapshot;
  else                                     m_darkScheme  = snapshot;

  m_themeMode = mode;
  const ThemeMode now = effectiveTheme();
  m_lastEffective = now;
  applyThemeFields(now == ThemeMode::Light ? m_lightScheme : m_darkScheme);
  save();
  emit settingsChanged();
}

ColorScheme Settings::scheme(ThemeMode which) const {
  // Auto は不可だがプログラム上は Light フォールバック。
  if (which == ThemeMode::Dark) {
    return (m_lastEffective == ThemeMode::Dark) ? collectThemeFields() : m_darkScheme;
  }
  return (m_lastEffective == ThemeMode::Light) ? collectThemeFields() : m_lightScheme;
}

void Settings::setScheme(ThemeMode which, const ColorScheme& s) {
  if (which == ThemeMode::Auto) return;
  if (which == ThemeMode::Light) m_lightScheme = s;
  else                            m_darkScheme  = s;
  // 上書き先がアクティブなら m_ にも反映
  if (which == m_lastEffective) {
    applyThemeFields(s);
  }
  save();
  emit settingsChanged();
}

PaneSettings Settings::paneSettings(PaneType pane) const {
  int idx = static_cast<int>(pane);
  if (idx < 0 || idx >= static_cast<int>(PaneType::Count)) {
    qWarning() << "Settings::paneSettings: invalid pane type" << idx;
    return {};
  }
  return m_paneSettings[idx];
}

void Settings::setPaneSettings(PaneType pane, const PaneSettings& s) {
  int idx = static_cast<int>(pane);
  if (idx < 0 || idx >= static_cast<int>(PaneType::Count)) {
    qWarning() << "Settings::setPaneSettings: invalid pane type" << idx;
    return;
  }
  m_paneSettings[idx] = s;
}

bool Settings::hasPathOverride(const QString& path) const {
  return m_pathOverrides.contains(path);
}

PaneSettings Settings::pathOverride(const QString& path) const {
  return m_pathOverrides.value(path);
}

void Settings::setPathOverride(const QString& path, const PaneSettings& s) {
  m_pathOverrides.insert(path, s);
}

void Settings::removePathOverride(const QString& path) {
  m_pathOverrides.remove(path);
}

QColor Settings::baseBackground() const     { return m_baseBackground; }
QColor Settings::baseForeground() const     { return m_baseForeground; }
void   Settings::setBaseBackground(const QColor& c) { m_baseBackground = c; }
void   Settings::setBaseForeground(const QColor& c) { m_baseForeground = c; }

QFont  Settings::uiFont() const             { return m_uiFont; }
void   Settings::setUiFont(const QFont& f)  { m_uiFont = f; }

QFont Settings::font() const {
  return m_font;
}

void Settings::setFont(const QFont& font) {
  m_font = font;
}

QFont Settings::addressFont() const {
  return m_addressFont;
}

void Settings::setAddressFont(const QFont& font) {
  m_addressFont = font;
}

FileSizeFormat Settings::fileSizeFormatDual() const {
  return m_fileSizeFormatDual;
}

void Settings::setFileSizeFormatDual(FileSizeFormat fmt) {
  m_fileSizeFormatDual = fmt;
}

FileSizeFormat Settings::fileSizeFormatSingle() const {
  return m_fileSizeFormatSingle;
}

void Settings::setFileSizeFormatSingle(FileSizeFormat fmt) {
  m_fileSizeFormatSingle = fmt;
}

bool Settings::fileSizeThousandsSeparatorDual() const {
  return m_fileSizeThousandsSeparatorDual;
}

void Settings::setFileSizeThousandsSeparatorDual(bool enabled) {
  m_fileSizeThousandsSeparatorDual = enabled;
}

bool Settings::fileSizeThousandsSeparatorSingle() const {
  return m_fileSizeThousandsSeparatorSingle;
}

void Settings::setFileSizeThousandsSeparatorSingle(bool enabled) {
  m_fileSizeThousandsSeparatorSingle = enabled;
}

Settings::ListColumnVisibility Settings::listColumnVisibilityDual() const {
  return m_listColumnsDual;
}

void Settings::setListColumnVisibilityDual(const ListColumnVisibility& v) {
  m_listColumnsDual = v;
}

Settings::ListColumnVisibility Settings::listColumnVisibilitySingle() const {
  return m_listColumnsSingle;
}

void Settings::setListColumnVisibilitySingle(const ListColumnVisibility& v) {
  m_listColumnsSingle = v;
}

int  Settings::fileListRowHeight() const { return m_fileListRowHeight; }
void Settings::setFileListRowHeight(int px) {
  if (px < 0) px = 0;
  m_fileListRowHeight = px;
}

QString Settings::dateTimeFormatDual() const {
  return m_dateTimeFormatDual;
}

void Settings::setDateTimeFormatDual(const QString& fmt) {
  m_dateTimeFormatDual = fmt;
}

QString Settings::dateTimeFormatSingle() const {
  return m_dateTimeFormatSingle;
}

void Settings::setDateTimeFormatSingle(const QString& fmt) {
  m_dateTimeFormatSingle = fmt;
}

QList<ColorRule> Settings::colorRules() const {
  return m_colorRules;
}

void Settings::setColorRules(const QList<ColorRule>& rules) {
  m_colorRules = rules;
}

CategoryColor Settings::categoryColor(FileCategory cat, bool selected, bool inactive) const {
  int idx = static_cast<int>(cat);
  if (idx < 0 || idx >= static_cast<int>(FileCategory::Count)) return {};
  if (inactive) {
    return selected ? m_inactiveSelectedCategoryColors[idx]
                    : m_inactiveCategoryColors[idx];
  }
  return selected ? m_selectedCategoryColors[idx] : m_categoryColors[idx];
}

void Settings::setCategoryColor(FileCategory cat, bool selected, bool inactive,
                                const CategoryColor& c) {
  int idx = static_cast<int>(cat);
  if (idx < 0 || idx >= static_cast<int>(FileCategory::Count)) return;
  if (inactive) {
    if (selected) m_inactiveSelectedCategoryColors[idx] = c;
    else          m_inactiveCategoryColors[idx]         = c;
  } else {
    if (selected) m_selectedCategoryColors[idx] = c;
    else          m_categoryColors[idx]         = c;
  }
}

bool Settings::useInactivePaneColors() const {
  return m_useInactivePaneColors;
}

void Settings::setUseInactivePaneColors(bool use) {
  m_useInactivePaneColors = use;
}

QColor Settings::addressForeground() const { return m_addressForeground; }
void   Settings::setAddressForeground(const QColor& c) { m_addressForeground = c; }
QColor Settings::addressBackground() const { return m_addressBackground; }
void   Settings::setAddressBackground(const QColor& c) { m_addressBackground = c; }
QColor Settings::archiveAddressForeground() const { return m_archiveAddressForeground; }
void   Settings::setArchiveAddressForeground(const QColor& c) { m_archiveAddressForeground = c; }
QColor Settings::archiveAddressBackground() const { return m_archiveAddressBackground; }
void   Settings::setArchiveAddressBackground(const QColor& c) { m_archiveAddressBackground = c; }
QColor Settings::compareDifferForeground() const { return m_compareDifferForeground; }
void   Settings::setCompareDifferForeground(const QColor& c) { m_compareDifferForeground = c; }
QColor Settings::compareDifferBackground() const { return m_compareDifferBackground; }
void   Settings::setCompareDifferBackground(const QColor& c) { m_compareDifferBackground = c; }
QColor Settings::compareOnlyHereForeground() const { return m_compareOnlyHereForeground; }
void   Settings::setCompareOnlyHereForeground(const QColor& c) { m_compareOnlyHereForeground = c; }
QColor Settings::compareOnlyHereBackground() const { return m_compareOnlyHereBackground; }
void   Settings::setCompareOnlyHereBackground(const QColor& c) { m_compareOnlyHereBackground = c; }

QColor Settings::cursorColor(bool active) const {
  return active ? m_cursorActiveColor : m_cursorInactiveColor;
}
void Settings::setCursorColor(bool active, const QColor& c) {
  if (active) m_cursorActiveColor   = c;
  else        m_cursorInactiveColor = c;
}

CursorShape Settings::cursorShape() const {
  return m_cursorShape;
}

void Settings::setCursorShape(CursorShape shape) {
  m_cursorShape = shape;
}

int Settings::cursorThickness() const {
  return m_cursorThickness;
}

void Settings::setCursorThickness(int px) {
  m_cursorThickness = qBound(1, px, 32);
}

InitialPathMode Settings::initialPathMode(PaneType pane) const {
  int idx = static_cast<int>(pane);
  if (idx < 0 || idx >= static_cast<int>(PaneType::Count)) {
    return InitialPathMode::LastSession;
  }
  return m_initialPathMode[idx];
}

void Settings::setInitialPathMode(PaneType pane, InitialPathMode mode) {
  int idx = static_cast<int>(pane);
  if (idx < 0 || idx >= static_cast<int>(PaneType::Count)) return;
  m_initialPathMode[idx] = mode;
}

QString Settings::customInitialPath(PaneType pane) const {
  int idx = static_cast<int>(pane);
  if (idx < 0 || idx >= static_cast<int>(PaneType::Count)) return {};
  return m_customInitialPath[idx];
}

void Settings::setCustomInitialPath(PaneType pane, const QString& path) {
  int idx = static_cast<int>(pane);
  if (idx < 0 || idx >= static_cast<int>(PaneType::Count)) return;
  m_customInitialPath[idx] = path;
}

bool Settings::confirmOnExit() const {
  return m_confirmOnExit;
}

bool Settings::singleInstance() const {
  return m_singleInstance;
}

ViewerMode Settings::viewerMode() const {
  return m_viewerMode;
}

void Settings::setViewerMode(ViewerMode mode) {
  m_viewerMode = mode;
}

ListViewMode Settings::paneViewMode(PaneType pane) const {
  const int idx = static_cast<int>(pane);
  if (idx < 0 || idx >= static_cast<int>(PaneType::Count)) {
    return ListViewMode::List;
  }
  return m_paneViewMode[idx];
}

void Settings::setPaneViewMode(PaneType pane, ListViewMode mode) {
  const int idx = static_cast<int>(pane);
  if (idx < 0 || idx >= static_cast<int>(PaneType::Count)) return;
  m_paneViewMode[idx] = mode;
}

ThumbnailSize Settings::thumbnailSize() const {
  return m_thumbnailSize;
}

void Settings::setThumbnailSize(ThumbnailSize size) {
  m_thumbnailSize = size;
}

bool Settings::showToolbar() const {
  return m_showToolbar;
}

void Settings::setShowToolbar(bool show) {
  m_showToolbar = show;
}

LayoutMode Settings::layoutMode() const { return m_layoutMode; }
void Settings::setLayoutMode(LayoutMode mode) { m_layoutMode = mode; }
int  Settings::previewDebounceMs() const { return m_previewDebounceMs; }
void Settings::setPreviewDebounceMs(int ms) {
  if (ms < 50)   ms = 50;
  if (ms > 1000) ms = 1000;
  m_previewDebounceMs = ms;
}
qint64 Settings::previewMaxFileSizeBytes() const { return m_previewMaxFileSizeBytes; }
void   Settings::setPreviewMaxFileSizeBytes(qint64 bytes) {
  if (bytes < 1024LL * 1024)        bytes = 1024LL * 1024;          // 下限 1MB
  if (bytes > 500LL * 1024 * 1024)  bytes = 500LL * 1024 * 1024;    // 上限 500MB
  m_previewMaxFileSizeBytes = bytes;
}

void Settings::setSingleInstance(bool enabled) {
  m_singleInstance = enabled;
}

// ── 自動アップデート ─────────────────────────
bool Settings::autoUpdateCheckOnStartup() const { return m_autoUpdateCheckOnStartup; }
void Settings::setAutoUpdateCheckOnStartup(bool on) { m_autoUpdateCheckOnStartup = on; }
bool Settings::autoUpdateSilent() const { return m_autoUpdateSilent; }
void Settings::setAutoUpdateSilent(bool on) { m_autoUpdateSilent = on; }
QDateTime Settings::autoUpdateLastCheckedAt() const { return m_autoUpdateLastCheckedAt; }
void Settings::setAutoUpdateLastCheckedAt(const QDateTime& at) { m_autoUpdateLastCheckedAt = at; }
QStringList Settings::autoUpdateSkippedVersions() const { return m_autoUpdateSkippedVersions; }
void Settings::setAutoUpdateSkippedVersions(const QStringList& versions) { m_autoUpdateSkippedVersions = versions; }
void Settings::addAutoUpdateSkippedVersion(const QString& version) {
  if (!m_autoUpdateSkippedVersions.contains(version)) {
    m_autoUpdateSkippedVersions.append(version);
  }
}
QString Settings::autoUpdateChannel() const { return m_autoUpdateChannel; }
void Settings::setAutoUpdateChannel(const QString& channel) { m_autoUpdateChannel = channel; }
QString Settings::whatsNewShownVersion() const { return m_whatsNewShownVersion; }
void Settings::setWhatsNewShownVersion(const QString& version) { m_whatsNewShownVersion = version; }

QString Settings::pluginsDirectory() const {
  return m_pluginsDirectory;
}

void Settings::setPluginsDirectory(const QString& dir) {
  m_pluginsDirectory = dir;
}

QStringList Settings::disabledViewerPlugins() const {
  return m_disabledViewerPlugins;
}

void Settings::setDisabledViewerPlugins(const QStringList& pluginIds) {
  m_disabledViewerPlugins.clear();
  for (const QString& id : pluginIds) {
    const QString normalized = id.trimmed();
    if (!normalized.isEmpty()
        && !m_disabledViewerPlugins.contains(normalized, Qt::CaseInsensitive)) {
      m_disabledViewerPlugins.append(normalized);
    }
  }
  m_disabledViewerPlugins.sort(Qt::CaseInsensitive);
}

bool Settings::isViewerPluginDisabled(const QString& pluginId) const {
  return m_disabledViewerPlugins.contains(pluginId.trimmed(), Qt::CaseInsensitive);
}

void Settings::setViewerPluginDisabled(const QString& pluginId, bool disabled) {
  const QString normalized = pluginId.trimmed();
  if (normalized.isEmpty()) return;
  QStringList ids = m_disabledViewerPlugins;
  if (disabled) {
    if (!ids.contains(normalized, Qt::CaseInsensitive)) {
      ids.append(normalized);
    }
  } else {
    for (int i = ids.size() - 1; i >= 0; --i) {
      if (ids.at(i).compare(normalized, Qt::CaseInsensitive) == 0) {
        ids.removeAt(i);
      }
    }
  }
  setDisabledViewerPlugins(ids);
}

namespace {

// メディアビュアーの既定対応拡張子のリビジョン。新しい既定拡張子を追加する
// たびに増やす。既存ユーザーの settings.json に保存済みの拡張子リストへ、まだ
// 適用されていないリビジョンで新規追加された拡張子だけをマージするのに使う
// (ユーザーが明示的に削除した拡張子は復活させない。詳細は load() を参照)。
constexpr int kMediaExtensionsRevision = 2;

QString normalizeViewerAssociationExtension(QString extension) {
  extension = extension.trimmed().toLower();
  while (extension.startsWith(QLatin1Char('.'))) {
    extension.remove(0, 1);
  }
  return extension;
}

} // namespace

QMap<QString, QString> Settings::viewerAssociations() const {
  return m_viewerAssociations;
}

void Settings::setViewerAssociations(const QMap<QString, QString>& associations) {
  m_viewerAssociations.clear();
  for (auto it = associations.cbegin(); it != associations.cend(); ++it) {
    const QString extension = normalizeViewerAssociationExtension(it.key());
    const QString pluginId = it.value().trimmed();
    if (!extension.isEmpty() && !pluginId.isEmpty()) {
      m_viewerAssociations.insert(extension, pluginId);
    }
  }
}

QString Settings::viewerAssociationForExtension(const QString& extension) const {
  return m_viewerAssociations.value(normalizeViewerAssociationExtension(extension));
}

void Settings::setViewerAssociationForExtension(const QString& extension,
                                                const QString& pluginId) {
  const QString normalized = normalizeViewerAssociationExtension(extension);
  if (normalized.isEmpty()) return;
  const QString trimmedPluginId = pluginId.trimmed();
  if (trimmedPluginId.isEmpty()) {
    m_viewerAssociations.remove(normalized);
  } else {
    m_viewerAssociations.insert(normalized, trimmedPluginId);
  }
}

QString Settings::defaultPluginsDirectory() {
  // QStandardPaths::AppDataLocation で取れる OS 別ユーザーデータ path の下に
  // "plugins/" を足す。Settings.json と同じ「ユーザー毎のアプリ専用」場所。
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
       + QStringLiteral("/plugins");
}

bool Settings::syncBrowseShowDisabledDialog() const {
  return m_syncBrowseShowDisabledDialog;
}

void Settings::setSyncBrowseShowDisabledDialog(bool show) {
  m_syncBrowseShowDisabledDialog = show;
}

void Settings::setConfirmOnExit(bool confirm) {
  m_confirmOnExit = confirm;
}

LanguageMode Settings::language() const { return m_language; }
void Settings::setLanguage(LanguageMode lang) { m_language = lang; }

bool    Settings::logVisible()  const { return m_logVisible; }
void    Settings::setLogVisible(bool v) { m_logVisible = v; }
int     Settings::logPaneHeight() const { return m_logPaneHeight; }
void    Settings::setLogPaneHeight(int px) {
  if (px < 40) px = 40;
  if (px > 4000) px = 4000;
  m_logPaneHeight = px;
}
bool    Settings::logToFile()   const { return m_logToFile; }
void    Settings::setLogToFile(bool v) { m_logToFile = v; }
QString Settings::logDirectory() const { return m_logDirectory; }
void    Settings::setLogDirectory(const QString& d) { m_logDirectory = d; }
int     Settings::logRetentionDays() const { return m_logRetentionDays; }
void    Settings::setLogRetentionDays(int days) {
  if (days < 0) days = 0;
  m_logRetentionDays = days;
}

bool Settings::cursorLoop() const {
  return m_cursorLoop;
}

void Settings::setCursorLoop(bool loop) {
  m_cursorLoop = loop;
}

bool Settings::typeAheadIncludeDotfiles() const {
  return m_typeAheadIncludeDotfiles;
}

void Settings::setTypeAheadIncludeDotfiles(bool include) {
  m_typeAheadIncludeDotfiles = include;
}

QStringList Settings::textViewerExtensions() const { return m_textViewerExtensions; }
void Settings::setTextViewerExtensions(const QStringList& exts) { m_textViewerExtensions = exts; }

QStringList Settings::markdownViewerExtensions() const { return m_markdownViewerExtensions; }
void Settings::setMarkdownViewerExtensions(const QStringList& exts) { m_markdownViewerExtensions = exts; }

QStringList Settings::pdfViewerExtensions() const { return m_pdfViewerExtensions; }
void Settings::setPdfViewerExtensions(const QStringList& exts) { m_pdfViewerExtensions = exts; }

QStringList Settings::csvViewerExtensions() const { return m_csvViewerExtensions; }
void Settings::setCsvViewerExtensions(const QStringList& exts) { m_csvViewerExtensions = exts; }
QStringList Settings::textViewerMimePatterns() const { return m_textViewerMimePatterns; }
void Settings::setTextViewerMimePatterns(const QStringList& patterns) { m_textViewerMimePatterns = patterns; }

QFont Settings::textViewerFont() const { return m_textViewerFont; }
void  Settings::setTextViewerFont(const QFont& font) { m_textViewerFont = font; }

QString Settings::textViewerEncoding() const { return m_textViewerEncoding; }
void    Settings::setTextViewerEncoding(const QString& encoding) { m_textViewerEncoding = encoding; }

bool Settings::textViewerShowLineNumbers() const { return m_textViewerShowLineNumbers; }
void Settings::setTextViewerShowLineNumbers(bool show) { m_textViewerShowLineNumbers = show; }

bool Settings::textViewerWordWrap() const { return m_textViewerWordWrap; }
void Settings::setTextViewerWordWrap(bool wrap) { m_textViewerWordWrap = wrap; }

QColor Settings::textViewerNormalForeground()       const { return m_textViewerNormalFg; }
QColor Settings::textViewerNormalBackground()       const { return m_textViewerNormalBg; }
QColor Settings::textViewerSelectedForeground()     const { return m_textViewerSelectedFg; }
QColor Settings::textViewerSelectedBackground()     const { return m_textViewerSelectedBg; }
QColor Settings::textViewerLineNumberForeground()   const { return m_textViewerLineNumberFg; }
QColor Settings::textViewerLineNumberBackground()   const { return m_textViewerLineNumberBg; }
void   Settings::setTextViewerNormalForeground(const QColor& c)     { m_textViewerNormalFg = c; }
void   Settings::setTextViewerNormalBackground(const QColor& c)     { m_textViewerNormalBg = c; }
void   Settings::setTextViewerSelectedForeground(const QColor& c)   { m_textViewerSelectedFg = c; }
void   Settings::setTextViewerSelectedBackground(const QColor& c)   { m_textViewerSelectedBg = c; }
void   Settings::setTextViewerLineNumberForeground(const QColor& c) { m_textViewerLineNumberFg = c; }
void   Settings::setTextViewerLineNumberBackground(const QColor& c) { m_textViewerLineNumberBg = c; }

QStringList Settings::imageViewerExtensions() const { return m_imageViewerExtensions; }
void Settings::setImageViewerExtensions(const QStringList& exts) { m_imageViewerExtensions = exts; }
QStringList Settings::imageViewerMimePatterns() const { return m_imageViewerMimePatterns; }
void Settings::setImageViewerMimePatterns(const QStringList& patterns) { m_imageViewerMimePatterns = patterns; }

int   Settings::imageViewerZoomPercent() const { return m_imageViewerZoomPercent; }
void  Settings::setImageViewerZoomPercent(int percent) {
  m_imageViewerZoomPercent = qBound(1, percent, 1000);
}
bool  Settings::imageViewerFitToWindow() const { return m_imageViewerFitToWindow; }
void  Settings::setImageViewerFitToWindow(bool fit) { m_imageViewerFitToWindow = fit; }
bool  Settings::imageViewerAnimation() const { return m_imageViewerAnimation; }
void  Settings::setImageViewerAnimation(bool on) { m_imageViewerAnimation = on; }
ImageTransparencyMode Settings::imageViewerTransparencyMode() const { return m_imageViewerTransparencyMode; }
void  Settings::setImageViewerTransparencyMode(ImageTransparencyMode mode) { m_imageViewerTransparencyMode = mode; }
QColor Settings::imageViewerSolidColor() const { return m_imageViewerSolidColor; }
void   Settings::setImageViewerSolidColor(const QColor& c) { m_imageViewerSolidColor = c; }
QColor Settings::imageViewerCheckerColor1() const { return m_imageViewerCheckerColor1; }
void   Settings::setImageViewerCheckerColor1(const QColor& c) { m_imageViewerCheckerColor1 = c; }
QColor Settings::imageViewerCheckerColor2() const { return m_imageViewerCheckerColor2; }
void   Settings::setImageViewerCheckerColor2(const QColor& c) { m_imageViewerCheckerColor2 = c; }

BinaryViewerUnit Settings::binaryViewerUnit() const {
  return m_binaryViewerUnit;
}

void Settings::setBinaryViewerUnit(BinaryViewerUnit unit) {
  m_binaryViewerUnit = unit;
}

BinaryViewerEndian Settings::binaryViewerEndian() const {
  return m_binaryViewerEndian;
}

void Settings::setBinaryViewerEndian(BinaryViewerEndian endian) {
  m_binaryViewerEndian = endian;
}

QString Settings::binaryViewerEncoding() const {
  return m_binaryViewerEncoding;
}

void Settings::setBinaryViewerEncoding(const QString& encoding) {
  m_binaryViewerEncoding = encoding;
}

// ── PDF ビュアー既定 ──
bool Settings::pdfViewerContinuous() const { return m_pdfViewerContinuous; }
void Settings::setPdfViewerContinuous(bool on) { m_pdfViewerContinuous = on; }
PdfViewerFitMode Settings::pdfViewerFitMode() const { return m_pdfViewerFitMode; }
void Settings::setPdfViewerFitMode(PdfViewerFitMode mode) { m_pdfViewerFitMode = mode; }

// ── CSV/TSV ビュアー既定 ──
QString Settings::csvViewerDelimiter() const { return m_csvViewerDelimiter; }
void Settings::setCsvViewerDelimiter(const QString& d) { m_csvViewerDelimiter = d; }
bool Settings::csvViewerFirstRowAsHeader() const { return m_csvViewerFirstRowAsHeader; }
void Settings::setCsvViewerFirstRowAsHeader(bool on) { m_csvViewerFirstRowAsHeader = on; }
QFont Settings::csvViewerFont() const { return m_csvViewerFont; }
void  Settings::setCsvViewerFont(const QFont& font) { m_csvViewerFont = font; }

// ── Markdown ビュアー既定 ──
bool Settings::markdownViewerShowSource() const { return m_markdownViewerShowSource; }
void Settings::setMarkdownViewerShowSource(bool on) { m_markdownViewerShowSource = on; }
QFont Settings::markdownViewerFont() const { return m_markdownViewerFont; }
void  Settings::setMarkdownViewerFont(const QFont& font) { m_markdownViewerFont = font; }
QColor Settings::markdownViewerForeground() const { return m_markdownViewerFg; }
void   Settings::setMarkdownViewerForeground(const QColor& c) { m_markdownViewerFg = c; }
QColor Settings::markdownViewerBackground() const { return m_markdownViewerBg; }
void   Settings::setMarkdownViewerBackground(const QColor& c) { m_markdownViewerBg = c; }
QColor Settings::markdownViewerLinkColor() const { return m_markdownViewerLink; }
void   Settings::setMarkdownViewerLinkColor(const QColor& c) { m_markdownViewerLink = c; }

// ── メディアビュアー既定 ──
QStringList Settings::mediaViewerExtensions() const { return m_mediaViewerExtensions; }
void Settings::setMediaViewerExtensions(const QStringList& exts) {
  m_mediaViewerExtensions = exts;
}
int Settings::mediaViewerVolume() const { return m_mediaViewerVolume; }
void Settings::setMediaViewerVolume(int volume) {
  m_mediaViewerVolume = qBound(0, volume, 100);
}
bool Settings::mediaViewerLoop() const { return m_mediaViewerLoop; }
void Settings::setMediaViewerLoop(bool on) { m_mediaViewerLoop = on; }
bool Settings::mediaViewerAutoplay() const { return m_mediaViewerAutoplay; }
void Settings::setMediaViewerAutoplay(bool on) { m_mediaViewerAutoplay = on; }
bool Settings::mediaViewerFitToWindow() const { return m_mediaViewerFitToWindow; }
void Settings::setMediaViewerFitToWindow(bool on) { m_mediaViewerFitToWindow = on; }
int  Settings::mediaViewerZoomPercent() const { return m_mediaViewerZoomPercent; }
void Settings::setMediaViewerZoomPercent(int percent) {
  m_mediaViewerZoomPercent = qBound(1, percent, 1000);
}

QFont Settings::binaryViewerFont() const {
  return m_binaryViewerFont;
}

void Settings::setBinaryViewerFont(const QFont& font) {
  m_binaryViewerFont = font;
}

QColor Settings::binaryViewerNormalForeground()   const { return m_binaryViewerNormalFg; }
QColor Settings::binaryViewerNormalBackground()   const { return m_binaryViewerNormalBg; }
QColor Settings::binaryViewerSelectedForeground() const { return m_binaryViewerSelectedFg; }
QColor Settings::binaryViewerSelectedBackground() const { return m_binaryViewerSelectedBg; }
QColor Settings::binaryViewerAddressForeground()  const { return m_binaryViewerAddressFg; }
QColor Settings::binaryViewerAddressBackground()  const { return m_binaryViewerAddressBg; }
void   Settings::setBinaryViewerNormalForeground(const QColor& c)   { m_binaryViewerNormalFg = c; }
void   Settings::setBinaryViewerNormalBackground(const QColor& c)   { m_binaryViewerNormalBg = c; }
void   Settings::setBinaryViewerSelectedForeground(const QColor& c) { m_binaryViewerSelectedFg = c; }
void   Settings::setBinaryViewerSelectedBackground(const QColor& c) { m_binaryViewerSelectedBg = c; }
void   Settings::setBinaryViewerAddressForeground(const QColor& c)  { m_binaryViewerAddressFg = c; }
void   Settings::setBinaryViewerAddressBackground(const QColor& c)  { m_binaryViewerAddressBg = c; }

bool Settings::persistHistory() const {
  return m_persistHistory;
}

void Settings::setPersistHistory(bool persist) {
  m_persistHistory = persist;
}

QStringList Settings::paneHistory(PaneType pane) const {
  int idx = static_cast<int>(pane);
  if (idx < 0 || idx >= static_cast<int>(PaneType::Count)) return {};
  return m_paneHistory[idx];
}

void Settings::setPaneHistory(PaneType pane, const QStringList& entries) {
  int idx = static_cast<int>(pane);
  if (idx < 0 || idx >= static_cast<int>(PaneType::Count)) return;
  m_paneHistory[idx] = entries;
}

QList<Bookmark> Settings::bookmarks() const {
  return m_bookmarks;
}

QList<UserCommand> Settings::userCommands() const {
  return m_userCommands;
}

void Settings::setUserCommands(const QList<UserCommand>& cmds) {
  m_userCommands = cmds;
}

void Settings::setBookmarks(const QList<Bookmark>& list) {
  m_bookmarks = list;
}

QString Settings::autoRenameTemplate() const {
  return m_autoRenameTemplate;
}

void Settings::setAutoRenameTemplate(const QString& tmpl) {
  m_autoRenameTemplate = tmpl;
}

bool Settings::defaultDeleteToTrash() const {
  return m_defaultDeleteToTrash;
}

bool Settings::progressAutoClose() const { return m_progressAutoClose; }
void Settings::setProgressAutoClose(bool v) { m_progressAutoClose = v; }

void Settings::setDefaultDeleteToTrash(bool toTrash) {
  m_defaultDeleteToTrash = toTrash;
}

QStringList Settings::searchExcludeDirs() const {
  return m_searchExcludeDirs;
}

void Settings::setSearchExcludeDirs(const QStringList& patterns) {
  m_searchExcludeDirs = patterns;
}

WindowSizeMode Settings::windowSizeMode() const {
  return m_windowSizeMode;
}

void Settings::setWindowSizeMode(WindowSizeMode mode) {
  m_windowSizeMode = mode;
}

QSize Settings::customWindowSize() const {
  return m_customWindowSize;
}

void Settings::setCustomWindowSize(const QSize& size) {
  m_customWindowSize = size;
}

QSize Settings::lastWindowSize() const {
  return m_lastWindowSize;
}

void Settings::setLastWindowSize(const QSize& size) {
  m_lastWindowSize = size;
}

WindowPositionMode Settings::windowPositionMode() const {
  return m_windowPositionMode;
}

void Settings::setWindowPositionMode(WindowPositionMode mode) {
  m_windowPositionMode = mode;
}

QPoint Settings::customWindowPosition() const {
  return m_customWindowPosition;
}

void Settings::setCustomWindowPosition(const QPoint& pos) {
  m_customWindowPosition = pos;
}

QPoint Settings::lastWindowPosition() const {
  return m_lastWindowPosition;
}

void Settings::setLastWindowPosition(const QPoint& pos) {
  m_lastWindowPosition = pos;
}

// macOS / Windows / Linux 共通のデフォルトブックマークを返す。
// 存在するパスのみを含め、isDefault=true を付与する（= 削除不可）。
// macOS のルート "/" は実用性が低いため除外。必要な /Volumes/* や
// クラウドマウントは動的検出 (Detected locations) で扱う。
static QList<Bookmark> buildDefaultBookmarks() {
  QList<Bookmark> list;

  auto addIfExists = [&](const QString& name, const QString& path) {
    if (!path.isEmpty() && QDir(path).exists()) {
      Bookmark b;
      b.name      = name;
      b.path      = path;
      b.isDefault = true;
      list.append(b);
    }
  };

  addIfExists(QObject::tr("Home"),
              QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
  addIfExists(QObject::tr("Desktop"),
              QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
  addIfExists(QObject::tr("Documents"),
              QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
  addIfExists(QObject::tr("Downloads"),
              QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
  addIfExists(QObject::tr("Pictures"),
              QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
  addIfExists(QObject::tr("Music"),
              QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
  addIfExists(QObject::tr("Movies"),
              QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));

  // ドライブ (Windows の C:/ 等) は既定ブックマークに含めない。ブックマーク
  // 一覧ダイアログの「検出された場所」(buildDetectedLocations) が接続中の
  // 全ドライブをボリューム名付きで動的表示するため、ここでレターのみの既定を
  // 足すと二重表示になっていた。macOS の "/" も従来どおり含めない。

  return list;
}

// Helper functions for JSON conversion
namespace {

QString sortKeyToString(SortKey key) {
  switch (key) {
    case SortKey::None: return "none";
    case SortKey::Name: return "name";
    case SortKey::Size: return "size";
    case SortKey::Type: return "type";
    case SortKey::LastModified: return "lastModified";
  }
  return "name";
}

SortKey stringToSortKey(const QString& str) {
  if (str == "none") return SortKey::None;
  if (str == "size") return SortKey::Size;
  if (str == "type") return SortKey::Type;
  if (str == "lastModified") return SortKey::LastModified;
  return SortKey::Name;
}

QString sortDirsTypeToString(SortDirsType type) {
  switch (type) {
    case SortDirsType::First: return "first";
    case SortDirsType::Last: return "last";
    case SortDirsType::Mixed: return "mixed";
  }
  return "first";
}

SortDirsType stringToSortDirsType(const QString& str) {
  if (str == "last") return SortDirsType::Last;
  if (str == "mixed") return SortDirsType::Mixed;
  return SortDirsType::First;
}

QString fileSizeFormatToString(FileSizeFormat fmt) {
  switch (fmt) {
    case FileSizeFormat::Bytes: return "bytes";
    case FileSizeFormat::SI: return "si";
    case FileSizeFormat::IEC: return "iec";
    case FileSizeFormat::Auto: return "auto";
  }
  return "auto";
}

FileSizeFormat stringToFileSizeFormat(const QString& str) {
  if (str == "bytes") return FileSizeFormat::Bytes;
  if (str == "si") return FileSizeFormat::SI;
  if (str == "iec") return FileSizeFormat::IEC;
  return FileSizeFormat::Auto;
}

QString windowSizeModeToString(WindowSizeMode mode) {
  switch (mode) {
    case WindowSizeMode::Default: return "default";
    case WindowSizeMode::LastSession: return "lastSession";
    case WindowSizeMode::Custom: return "custom";
  }
  return "default";
}

WindowSizeMode stringToWindowSizeMode(const QString& str) {
  if (str == "lastSession") return WindowSizeMode::LastSession;
  if (str == "custom") return WindowSizeMode::Custom;
  return WindowSizeMode::Default;
}

QString windowPositionModeToString(WindowPositionMode mode) {
  switch (mode) {
    case WindowPositionMode::Default: return "default";
    case WindowPositionMode::LastSession: return "lastSession";
    case WindowPositionMode::Custom: return "custom";
  }
  return "default";
}

WindowPositionMode stringToWindowPositionMode(const QString& str) {
  if (str == "lastSession") return WindowPositionMode::LastSession;
  if (str == "custom") return WindowPositionMode::Custom;
  return WindowPositionMode::Default;
}

QString initialPathModeToString(InitialPathMode mode) {
  switch (mode) {
    case InitialPathMode::Default:     return "default";
    case InitialPathMode::LastSession: return "lastSession";
    case InitialPathMode::Custom:      return "custom";
  }
  return "lastSession";
}

InitialPathMode stringToInitialPathMode(const QString& str) {
  if (str == "default") return InitialPathMode::Default;
  if (str == "custom")  return InitialPathMode::Custom;
  return InitialPathMode::LastSession;
}

QJsonObject paneSettingsToJson(const PaneSettings& pane) {
  QJsonObject obj;
  obj["path"] = pane.path;
  obj["sortKey"] = sortKeyToString(pane.sortKey);
  obj["sortOrder"] = (pane.sortOrder == Qt::AscendingOrder) ? "ascending" : "descending";
  obj["sortKey2nd"] = sortKeyToString(pane.sortKey2nd);
  obj["sortDirsType"] = sortDirsTypeToString(pane.sortDirsType);
  obj["sortDotFirst"] = pane.sortDotFirst;
  obj["sortCaseSensitive"] = (pane.sortCS == Qt::CaseSensitive);

  QJsonArray filters;
  for (const QString& filter : pane.nameFilters) {
    filters.append(filter);
  }
  obj["nameFilters"] = filters;

  obj["showHidden"] = static_cast<bool>(pane.attrFilter & AttrFilter::ShowHidden);
  obj["showSystem"] = static_cast<bool>(pane.attrFilter & AttrFilter::ShowSystem);
  obj["dirsOnly"] = static_cast<bool>(pane.attrFilter & AttrFilter::DirsOnly);
  obj["filesOnly"] = static_cast<bool>(pane.attrFilter & AttrFilter::FilesOnly);

  return obj;
}

PaneSettings jsonToPaneSettings(const QJsonObject& obj) {
  PaneSettings pane;
  pane.path = obj.value("path").toString(QDir::homePath());
  pane.sortKey = stringToSortKey(obj.value("sortKey").toString());
  pane.sortOrder = (obj.value("sortOrder").toString() == "descending") ?
                   Qt::DescendingOrder : Qt::AscendingOrder;
  pane.sortKey2nd = stringToSortKey(obj.value("sortKey2nd").toString());
  pane.sortDirsType = stringToSortDirsType(obj.value("sortDirsType").toString());
  pane.sortDotFirst = obj.value("sortDotFirst").toBool(true);
  pane.sortCS = obj.value("sortCaseSensitive").toBool(false) ?
                Qt::CaseSensitive : Qt::CaseInsensitive;

  QJsonArray filters = obj.value("nameFilters").toArray();
  for (const QJsonValue& val : filters) {
    pane.nameFilters.append(val.toString());
  }

  AttrFilterFlags flags = AttrFilter::None;
  if (obj.value("showHidden").toBool(false))
    flags |= AttrFilter::ShowHidden;
  if (obj.value("showSystem").toBool(false))
    flags |= AttrFilter::ShowSystem;
  if (obj.value("dirsOnly").toBool(false))
    flags |= AttrFilter::DirsOnly;
  if (obj.value("filesOnly").toBool(false))
    flags |= AttrFilter::FilesOnly;
  pane.attrFilter = flags;

  return pane;
}

QString fileCategoryToString(FileCategory cat) {
  switch (cat) {
    case FileCategory::Normal:    return "normal";
    case FileCategory::Hidden:    return "hidden";
    case FileCategory::Directory: return "directory";
    case FileCategory::Count:     break;
  }
  return "normal";
}

QJsonObject categoryColorToJson(const CategoryColor& c) {
  QJsonObject obj;
  obj["foreground"] = c.foreground.isValid() ? c.foreground.name(QColor::HexArgb) : QString();
  obj["background"] = c.background.isValid() ? c.background.name(QColor::HexArgb) : QString();
  obj["bold"]       = c.bold;
  return obj;
}

CategoryColor jsonToCategoryColor(const QJsonObject& obj, const CategoryColor& fallback) {
  CategoryColor c = fallback;
  const QString fg = obj.value("foreground").toString();
  const QString bg = obj.value("background").toString();
  if (!fg.isEmpty()) c.foreground = QColor(fg);
  if (!bg.isEmpty()) c.background = QColor(bg);
  if (obj.contains("bold")) c.bold = obj.value("bold").toBool();
  return c;
}

QJsonObject colorRuleToJson(const ColorRule& rule) {
  QJsonObject obj;
  obj["pattern"] = rule.pattern;
  obj["foreground"] = rule.foreground.name(QColor::HexArgb);
  obj["background"] = rule.background.name(QColor::HexArgb);
  obj["bold"] = rule.bold;
  return obj;
}

ColorRule jsonToColorRule(const QJsonObject& obj) {
  ColorRule rule;
  rule.pattern = obj.value("pattern").toString();
  rule.foreground = QColor(obj.value("foreground").toString());
  rule.background = QColor(obj.value("background").toString());
  rule.bold = obj.value("bold").toBool(false);
  return rule;
}

} // anonymous namespace

int binaryViewerUnitToBytes(BinaryViewerUnit unit) {
  switch (unit) {
    case BinaryViewerUnit::Byte1: return 1;
    case BinaryViewerUnit::Byte2: return 2;
    case BinaryViewerUnit::Byte4: return 4;
    case BinaryViewerUnit::Byte8: return 8;
  }
  return 1;
}

BinaryViewerUnit bytesToBinaryViewerUnit(int bytes) {
  switch (bytes) {
    case 2: return BinaryViewerUnit::Byte2;
    case 4: return BinaryViewerUnit::Byte4;
    case 8: return BinaryViewerUnit::Byte8;
    default: return BinaryViewerUnit::Byte1;
  }
}

QString binaryViewerEndianToString(BinaryViewerEndian endian) {
  return (endian == BinaryViewerEndian::Big) ? QStringLiteral("big") : QStringLiteral("little");
}

BinaryViewerEndian stringToBinaryViewerEndian(const QString& str) {
  return (str == QLatin1String("big")) ? BinaryViewerEndian::Big : BinaryViewerEndian::Little;
}

QString Settings::settingsFilePath() {
  return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
       + QStringLiteral("/settings.json");
}

bool Settings::exportTo(const QString& path, QString* errorOut) const {
  // 最新の in-memory 状態を settings.json に書き出してから、それを path へ
  // コピーする (= 「dialog の編集も含めて全部出す」ためには呼ぶ前に
  // 呼出側が必要な applyTo / save をしておくこと)。
  save();
  const QString src = settingsFilePath();
  QFile srcFile(src);
  if (!srcFile.exists()) {
    if (errorOut) *errorOut = QObject::tr("Settings file does not exist yet.");
    return false;
  }
  // 上書きコピー (QFile::copy は dst が既存だと失敗するため、先に消す)。
  if (QFile::exists(path) && !QFile::remove(path)) {
    if (errorOut) *errorOut = QObject::tr("Cannot overwrite existing file: %1").arg(path);
    return false;
  }
  if (!QFile::copy(src, path)) {
    if (errorOut) *errorOut = QObject::tr("Failed to copy settings to: %1").arg(path);
    return false;
  }
  return true;
}

bool Settings::importFrom(const QString& path, QString* errorOut) {
  // path を読み込んで簡易検証してから settings.json を上書きし、
  // load() で in-memory を再構築する。
  QFile in(path);
  if (!in.open(QIODevice::ReadOnly)) {
    if (errorOut) *errorOut = QObject::tr("Cannot open file: %1").arg(path);
    return false;
  }
  const QByteArray data = in.readAll();
  in.close();

  QJsonParseError perr;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
  if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
    if (errorOut) *errorOut = QObject::tr("Not a valid JSON settings file: %1")
                                .arg(perr.errorString());
    return false;
  }
  const QJsonObject root = doc.object();
  if (!root.contains("version")) {
    if (errorOut) *errorOut = QObject::tr("Missing \"version\" field — not a farman settings file?");
    return false;
  }

  // settings.json を上書き。AppConfigLocation が無ければ作る。
  const QString dst = settingsFilePath();
  const QFileInfo dstInfo(dst);
  QDir().mkpath(dstInfo.absolutePath());
  if (QFile::exists(dst) && !QFile::remove(dst)) {
    if (errorOut) *errorOut = QObject::tr("Cannot replace existing settings file: %1").arg(dst);
    return false;
  }
  if (!QFile::copy(path, dst)) {
    if (errorOut) *errorOut = QObject::tr("Failed to install imported settings file");
    return false;
  }

  // in-memory を読み直して通知。Settings ダイアログを表示中の場合は
  // 受け手側で UI を再構築する責任 (= settingsChanged を受けて reload する)。
  load();
  emit settingsChanged();
  return true;
}

void Settings::load() {
  QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  QString filePath = configPath + "/settings.json";

  QFile file(filePath);
  if (!file.exists()) {
    qDebug() << "Settings::load: settings file not found, using defaults:" << filePath;
    // 完全に初回起動なのでデフォルトブックマークを注入して完了とする。
    m_bookmarks = buildDefaultBookmarks();
    m_defaultBookmarksInstalled = true;
    return;
  }

  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Settings::load: failed to open settings file:" << filePath;
    return;
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject()) {
    qWarning() << "Settings::load: invalid JSON format";
    return;
  }

  QJsonObject root = doc.object();
  const int fileVersion = root.value("version").toInt(1);

  // Load appearance settings
  QJsonObject appearance = root.value("appearance").toObject();
  if (appearance.contains("font")) {
    QJsonObject fontObj = appearance.value("font").toObject();
    m_font.setFamily(fontObj.value("family").toString());
    if (fontObj.contains("pointSize")) {
      m_font.setPointSize(fontObj.value("pointSize").toInt());
    }
  }
  // 新キー addressFont、旧キー pathFont の順でフォールバック
  if (appearance.contains("addressFont") || appearance.contains("pathFont")) {
    QJsonObject fontObj = appearance.contains("addressFont")
                          ? appearance.value("addressFont").toObject()
                          : appearance.value("pathFont").toObject();
    m_addressFont.setFamily(fontObj.value("family").toString());
    if (fontObj.contains("pointSize")) {
      m_addressFont.setPointSize(fontObj.value("pointSize").toInt());
    }
  }

  // ファイルサイズ / 日時表示形式: Dual / Single の 2 系統。旧キー
  // (fileSizeFormat / dateTimeFormat) があったらそれを両方の既定値に
  // して移行する。
  const QString legacySize = appearance.value("fileSizeFormat").toString();
  const QString legacyDate = appearance.value("dateTimeFormat").toString();

  if (appearance.contains("fileSizeFormatDual")) {
    m_fileSizeFormatDual = stringToFileSizeFormat(
      appearance.value("fileSizeFormatDual").toString());
  } else if (!legacySize.isEmpty()) {
    m_fileSizeFormatDual = stringToFileSizeFormat(legacySize);
  }
  if (appearance.contains("fileSizeFormatSingle")) {
    m_fileSizeFormatSingle = stringToFileSizeFormat(
      appearance.value("fileSizeFormatSingle").toString());
  } else if (!legacySize.isEmpty()) {
    m_fileSizeFormatSingle = stringToFileSizeFormat(legacySize);
  }

  // 桁区切りカンマも Dual / Single に分割。旧キーがあれば両方の既定値にする。
  if (appearance.contains("fileSizeThousandsSeparatorDual")) {
    m_fileSizeThousandsSeparatorDual =
      appearance.value("fileSizeThousandsSeparatorDual").toBool(true);
  } else if (appearance.contains("fileSizeThousandsSeparator")) {
    m_fileSizeThousandsSeparatorDual =
      appearance.value("fileSizeThousandsSeparator").toBool(true);
  }
  if (appearance.contains("fileSizeThousandsSeparatorSingle")) {
    m_fileSizeThousandsSeparatorSingle =
      appearance.value("fileSizeThousandsSeparatorSingle").toBool(true);
  } else if (appearance.contains("fileSizeThousandsSeparator")) {
    m_fileSizeThousandsSeparatorSingle =
      appearance.value("fileSizeThousandsSeparator").toBool(true);
  }

  // 列表示 (Dual / Single)。値が無いキーは構造体既定値のまま。
  auto loadCols = [&](const QJsonObject& src, ListColumnVisibility& dst) {
    if (src.contains("type"))         dst.type         = src.value("type").toBool(dst.type);
    if (src.contains("size"))         dst.size         = src.value("size").toBool(dst.size);
    if (src.contains("lastModified")) dst.lastModified = src.value("lastModified").toBool(dst.lastModified);
    if (src.contains("created"))      dst.created      = src.value("created").toBool(dst.created);
    if (src.contains("permissions"))  dst.permissions  = src.value("permissions").toBool(dst.permissions);
    if (src.contains("attributes"))   dst.attributes   = src.value("attributes").toBool(dst.attributes);
    if (src.contains("owner"))        dst.owner        = src.value("owner").toBool(dst.owner);
    if (src.contains("group"))        dst.group        = src.value("group").toBool(dst.group);
    if (src.contains("linkTarget"))   dst.linkTarget   = src.value("linkTarget").toBool(dst.linkTarget);
  };
  loadCols(appearance.value("listColumnsDual").toObject(),   m_listColumnsDual);
  loadCols(appearance.value("listColumnsSingle").toObject(), m_listColumnsSingle);

  m_fileListRowHeight = appearance.value("fileListRowHeight").toInt(0);
  if (m_fileListRowHeight < 0) m_fileListRowHeight = 0;

  if (appearance.contains("dateTimeFormatDual")) {
    m_dateTimeFormatDual = appearance.value("dateTimeFormatDual")
                             .toString("yyyy/MM/dd HH:mm:ss");
  } else if (!legacyDate.isEmpty()) {
    m_dateTimeFormatDual = legacyDate;
  }
  if (appearance.contains("dateTimeFormatSingle")) {
    m_dateTimeFormatSingle = appearance.value("dateTimeFormatSingle")
                               .toString("yyyy/MM/dd HH:mm:ss");
  } else if (!legacyDate.isEmpty()) {
    m_dateTimeFormatSingle = legacyDate;
  }

  // Load color rules
  QJsonArray colorRulesArray = appearance.value("colorRules").toArray();
  m_colorRules.clear();
  for (const QJsonValue& val : colorRulesArray) {
    if (val.isObject()) {
      m_colorRules.append(jsonToColorRule(val.toObject()));
    }
  }

  // Load category colors:
  //   新形式:  { active: { normal:{}, selected:{} }, inactive: { normal:{}, selected:{} } }
  //   中期形式: { normal:{}, selected:{} }                        ← active として扱う
  //   旧形式:  { <category>:{} ... }                               ← active/normal として扱う
  QJsonObject catColors = appearance.value("categoryColors").toObject();

  auto extractGroup = [](const QJsonObject& root, const QString& key) {
    return root.contains(key) && root.value(key).isObject()
             ? root.value(key).toObject()
             : QJsonObject();
  };

  QJsonObject activeNormal;
  QJsonObject activeSelected;
  QJsonObject inactiveNormal;
  QJsonObject inactiveSel;

  if (catColors.contains("active") && catColors.value("active").isObject()) {
    // 新形式: categoryColors: { active: {...}, inactive: {...} }
    QJsonObject active   = catColors.value("active").toObject();
    QJsonObject inactive = extractGroup(catColors, "inactive");
    activeNormal   = extractGroup(active,   "normal");
    activeSelected = extractGroup(active,   "selected");
    inactiveNormal = extractGroup(inactive, "normal");
    inactiveSel    = extractGroup(inactive, "selected");
  } else if (catColors.value("normal").toObject().contains("foreground")) {
    // 最古のフラット形式: categoryColors: { normal: {fg/bg/bold}, hidden: {...}, directory: {...} }
    activeNormal = catColors;
  } else {
    // 中期形式: categoryColors: { normal: {<category>:{...}}, selected: {<category>:{...}} }
    activeNormal   = extractGroup(catColors, "normal");
    activeSelected = extractGroup(catColors, "selected");
  }

  auto loadCat = [&](FileCategory cat) {
    int idx = static_cast<int>(cat);
    const QString key = fileCategoryToString(cat);
    if (activeNormal.contains(key)) {
      m_categoryColors[idx] = jsonToCategoryColor(
        activeNormal.value(key).toObject(), m_categoryColors[idx]);
    }
    if (activeSelected.contains(key)) {
      m_selectedCategoryColors[idx] = jsonToCategoryColor(
        activeSelected.value(key).toObject(), m_selectedCategoryColors[idx]);
    }
    if (inactiveNormal.contains(key)) {
      m_inactiveCategoryColors[idx] = jsonToCategoryColor(
        inactiveNormal.value(key).toObject(), m_inactiveCategoryColors[idx]);
    }
    if (inactiveSel.contains(key)) {
      m_inactiveSelectedCategoryColors[idx] = jsonToCategoryColor(
        inactiveSel.value(key).toObject(), m_inactiveSelectedCategoryColors[idx]);
    }
  };
  loadCat(FileCategory::Normal);
  loadCat(FileCategory::Hidden);
  loadCat(FileCategory::Directory);

  m_useInactivePaneColors =
    appearance.value("useInactivePaneColors").toBool(false);

  // Address bar colors (新キー addressColors、旧キー pathColors の順でフォールバック)
  QJsonObject addressColors = appearance.contains("addressColors")
                              ? appearance.value("addressColors").toObject()
                              : appearance.value("pathColors").toObject();
  if (addressColors.contains("foreground")) {
    QColor fg(addressColors.value("foreground").toString());
    if (fg.isValid()) m_addressForeground = fg;
  }
  if (addressColors.contains("background")) {
    QColor bg(addressColors.value("background").toString());
    if (bg.isValid()) m_addressBackground = bg;
  }

  // Cursor colors
  QJsonObject cursorColors = appearance.value("cursorColors").toObject();
  if (cursorColors.contains("active")) {
    QColor a(cursorColors.value("active").toString());
    if (a.isValid()) m_cursorActiveColor = a;
  }
  {
    const QString shapeStr = appearance.value("cursorShape").toString();
    m_cursorShape = (shapeStr == QLatin1String("rowBackground"))
                      ? CursorShape::RowBackground
                      : CursorShape::Underline;
  }
  if (appearance.contains("cursorThickness")) {
    m_cursorThickness = qBound(1, appearance.value("cursorThickness").toInt(2), 32);
  }
  if (cursorColors.contains("inactive")) {
    QColor i(cursorColors.value("inactive").toString());
    if (i.isValid()) m_cursorInactiveColor = i;
  }

  // Load behavior settings
  QJsonObject behavior = root.value("behavior").toObject();
  m_confirmOnExit = behavior.value("confirmOnExit").toBool(false);
  m_singleInstance = behavior.value("singleInstance").toBool(true);
  m_pluginsDirectory = behavior.value("pluginsDirectory").toString();
  m_disabledViewerPlugins.clear();
  {
    QStringList disabled;
    const QJsonArray plugins = behavior.value("disabledViewerPlugins").toArray();
    for (const QJsonValue& value : plugins) {
      disabled.append(value.toString());
    }
    setDisabledViewerPlugins(disabled);
  }
  m_viewerAssociations.clear();
  {
    const QJsonObject associations = behavior.value("viewerAssociations").toObject();
    for (auto it = associations.constBegin(); it != associations.constEnd(); ++it) {
      setViewerAssociationForExtension(it.key(), it.value().toString());
    }
  }
  m_syncBrowseShowDisabledDialog = behavior.value("syncBrowseShowDisabledDialog").toBool(true);
  {
    const QString modeStr = behavior.value("viewerMode").toString("inline");
    m_viewerMode = (modeStr == QLatin1String("external"))
                     ? ViewerMode::External
                     : ViewerMode::Inline;
  }
  m_showToolbar = behavior.value("showToolbar").toBool(true);
  // レイアウト (dual / single / preview)。未指定は dual。
  m_layoutMode = layoutModeFromKey(behavior.value("layoutMode").toString());
  // プレビュー動作パラメタ
  {
    const QJsonObject preview = behavior.value("preview").toObject();
    setPreviewDebounceMs(preview.value("debounceMs").toInt(200));
    const qint64 defaultMax = 10LL * 1024 * 1024;
    setPreviewMaxFileSizeBytes(static_cast<qint64>(
      preview.value("maxFileSizeBytes").toDouble(static_cast<double>(defaultMax))));
  }
  // 自動アップデート関連 ("autoUpdate" サブオブジェクト下にまとめる)。
  {
    const QJsonObject au = behavior.value("autoUpdate").toObject();
    m_autoUpdateCheckOnStartup = au.value("checkOnStartup").toBool(true);
    m_autoUpdateSilent         = au.value("silent").toBool(false);
    const QString iso = au.value("lastCheckedAt").toString();
    m_autoUpdateLastCheckedAt = iso.isEmpty()
      ? QDateTime()
      : QDateTime::fromString(iso, Qt::ISODate);
    m_autoUpdateSkippedVersions.clear();
    const QJsonArray skipped = au.value("skipped").toArray();
    for (const QJsonValue& v : skipped) {
      const QString s = v.toString();
      if (!s.isEmpty()) m_autoUpdateSkippedVersions.append(s);
    }
    m_autoUpdateChannel = au.value("channel").toString(QStringLiteral("stable"));
  }
  m_whatsNewShownVersion = behavior.value("whatsNewShownVersion").toString();
  {
    const QString langStr = behavior.value("language").toString("auto");
    if      (langStr == "en") m_language = LanguageMode::English;
    else if (langStr == "ja") m_language = LanguageMode::Japanese;
    else                       m_language = LanguageMode::Auto;
  }
  // ペインの表示モード (4 値: list / thumbnail-small / thumbnail-medium /
  // thumbnail-large) を左右別々に復元。旧フォーマット ("thumbnail") は
  // 旧グローバル thumbnailSize 値があればそれを反映、無ければ Medium として
  // 復元 (mig 互換)。
  {
    // 旧形式互換: グローバル thumbnailSize ("small" / "medium" / "large") を一旦解釈。
    ThumbnailSize legacySize = ThumbnailSize::Medium;
    {
      const QString s = behavior.value("thumbnailSize").toString("medium");
      if      (s == QLatin1String("small"))  legacySize = ThumbnailSize::Small;
      else if (s == QLatin1String("large"))  legacySize = ThumbnailSize::Large;
      else                                    legacySize = ThumbnailSize::Medium;
    }
    auto parsePaneMode = [legacySize](const QString& s) -> ListViewMode {
      if (s == QLatin1String("thumbnail-small"))  return ListViewMode::ThumbnailSmall;
      if (s == QLatin1String("thumbnail-medium")) return ListViewMode::ThumbnailMedium;
      if (s == QLatin1String("thumbnail-large"))  return ListViewMode::ThumbnailLarge;
      if (s == QLatin1String("thumbnail")) {
        // 旧フォーマット (List / Thumbnail の 2 値時代)。グローバル thumbnailSize
        // と組み合わせて 4 値に展開する。
        switch (legacySize) {
          case ThumbnailSize::Small:  return ListViewMode::ThumbnailSmall;
          case ThumbnailSize::Large:  return ListViewMode::ThumbnailLarge;
          case ThumbnailSize::Medium:
          default:                     return ListViewMode::ThumbnailMedium;
        }
      }
      return ListViewMode::List;
    };
    m_paneViewMode[static_cast<int>(PaneType::Left)] =
      parsePaneMode(behavior.value("leftPaneViewMode").toString("list"));
    m_paneViewMode[static_cast<int>(PaneType::Right)] =
      parsePaneMode(behavior.value("rightPaneViewMode").toString("list"));
  }
  m_cursorLoop = behavior.value("cursorLoop").toBool(false);
  m_typeAheadIncludeDotfiles = behavior.value("typeAheadIncludeDotfiles").toBool(true);
  m_persistHistory = behavior.value("persistHistory").toBool(false);
  m_autoRenameTemplate = behavior.value("autoRenameTemplate").toString(" ({n})");
  m_defaultDeleteToTrash = behavior.value("defaultDeleteToTrash").toBool(true);
  m_progressAutoClose    = behavior.value("progressAutoClose").toBool(false);
  if (behavior.contains("searchExcludeDirs")) {
    m_searchExcludeDirs.clear();
    const QJsonArray arr = behavior.value("searchExcludeDirs").toArray();
    for (const QJsonValue& v : arr) {
      const QString s = v.toString();
      if (!s.isEmpty()) m_searchExcludeDirs.append(s);
    }
  }
  m_defaultBookmarksInstalled = behavior.value("defaultBookmarksInstalled").toBool(false);

  // Load log settings
  QJsonObject logObj = root.value("log").toObject();
  m_logVisible    = logObj.value("visible").toBool(true);
  m_logPaneHeight = logObj.value("paneHeight").toInt(m_logPaneHeight);
  m_logToFile     = logObj.value("toFile").toBool(true);
  if (logObj.contains("directory")) {
    const QString p = logObj.value("directory").toString();
    if (!p.isEmpty()) m_logDirectory = p;
  }
  m_logRetentionDays = logObj.value("retentionDays").toInt(m_logRetentionDays);
  if (m_logRetentionDays < 0) m_logRetentionDays = 0;

  // Load text viewer settings
  QJsonObject textViewer = root.value("textViewer").toObject();
  if (textViewer.contains("extensions")) {
    QStringList list;
    for (const QJsonValue& v : textViewer.value("extensions").toArray()) {
      const QString s = v.toString().trimmed();
      if (!s.isEmpty()) list.append(s);
    }
    if (!list.isEmpty()) m_textViewerExtensions = list;
  }

  // Markdown ビュアー: テキストビュアーと並列で扱う
  QJsonObject markdownViewer = root.value("markdownViewer").toObject();
  if (markdownViewer.contains("extensions")) {
    QStringList list;
    for (const QJsonValue& v : markdownViewer.value("extensions").toArray()) {
      const QString s = v.toString().trimmed();
      if (!s.isEmpty()) list.append(s);
    }
    if (!list.isEmpty()) m_markdownViewerExtensions = list;
  }
  m_markdownViewerShowSource = markdownViewer.value("showSource").toBool(false);
  if (markdownViewer.contains("font")) {
    QFont f;
    if (f.fromString(markdownViewer.value("font").toString())) {
      m_markdownViewerFont = f;
    }
  }
  {
    // fg/bg: 空文字列 (= 無効色) はテーマ既定を維持する。
    auto loadMdColor = [&](const QString& key, QColor& dst) {
      if (markdownViewer.contains(key)) {
        const QString str = markdownViewer.value(key).toString();
        dst = str.isEmpty() ? QColor() : QColor(str);
      }
    };
    loadMdColor("fg",   m_markdownViewerFg);
    loadMdColor("bg",   m_markdownViewerBg);
    loadMdColor("link", m_markdownViewerLink);
  }

  // PDF ビュアー: バイナリ判定より先に評価される
  QJsonObject pdfViewer = root.value("pdfViewer").toObject();
  if (pdfViewer.contains("extensions")) {
    QStringList list;
    for (const QJsonValue& v : pdfViewer.value("extensions").toArray()) {
      const QString s = v.toString().trimmed();
      if (!s.isEmpty()) list.append(s);
    }
    if (!list.isEmpty()) m_pdfViewerExtensions = list;
  }
  m_pdfViewerContinuous = pdfViewer.value("continuous").toBool(true);
  {
    const QString fit = pdfViewer.value("fitMode").toString(QStringLiteral("actual"));
    m_pdfViewerFitMode = (fit == QLatin1String("fitWidth")) ? PdfViewerFitMode::FitWidth
                       : (fit == QLatin1String("fitPage"))  ? PdfViewerFitMode::FitPage
                                                            : PdfViewerFitMode::ActualSize;
  }

  // CSV / TSV ビュアー: テキスト判定より先に評価される
  QJsonObject csvViewer = root.value("csvViewer").toObject();
  if (csvViewer.contains("extensions")) {
    QStringList list;
    for (const QJsonValue& v : csvViewer.value("extensions").toArray()) {
      const QString s = v.toString().trimmed();
      if (!s.isEmpty()) list.append(s);
    }
    if (!list.isEmpty()) m_csvViewerExtensions = list;
  }
  m_csvViewerDelimiter        = csvViewer.value("delimiter").toString(QStringLiteral("auto"));
  m_csvViewerFirstRowAsHeader = csvViewer.value("firstRowAsHeader").toBool(false);
  if (csvViewer.contains("font")) {
    QFont f;
    if (f.fromString(csvViewer.value("font").toString())) {
      m_csvViewerFont = f;
    }
  }
  if (textViewer.contains("mimePatterns")) {
    QStringList list;
    for (const QJsonValue& v : textViewer.value("mimePatterns").toArray()) {
      const QString s = v.toString().trimmed();
      if (!s.isEmpty()) list.append(s);
    }
    if (!list.isEmpty()) m_textViewerMimePatterns = list;
  }
  if (textViewer.contains("font")) {
    QFont f;
    if (f.fromString(textViewer.value("font").toString())) {
      m_textViewerFont = f;
    }
  }
  m_textViewerEncoding        = textViewer.value("encoding").toString(QStringLiteral("UTF-8"));
  m_textViewerShowLineNumbers = textViewer.value("showLineNumbers").toBool(true);
  m_textViewerWordWrap        = textViewer.value("wordWrap").toBool(false);
  auto loadColor = [&](const QString& key, QColor& dst) {
    if (textViewer.contains(key)) {
      QColor c(textViewer.value(key).toString());
      if (c.isValid()) dst = c;
    }
  };
  loadColor("normalFg",     m_textViewerNormalFg);
  loadColor("normalBg",     m_textViewerNormalBg);
  loadColor("selectedFg",   m_textViewerSelectedFg);
  loadColor("selectedBg",   m_textViewerSelectedBg);
  loadColor("lineNumberFg", m_textViewerLineNumberFg);
  loadColor("lineNumberBg", m_textViewerLineNumberBg);

  // Load image viewer settings
  QJsonObject imageViewer = root.value("imageViewer").toObject();
  if (imageViewer.contains("extensions")) {
    QStringList list;
    for (const QJsonValue& v : imageViewer.value("extensions").toArray()) {
      const QString s = v.toString().trimmed();
      if (!s.isEmpty()) list.append(s);
    }
    if (!list.isEmpty()) m_imageViewerExtensions = list;
  }
  if (imageViewer.contains("mimePatterns")) {
    QStringList list;
    for (const QJsonValue& v : imageViewer.value("mimePatterns").toArray()) {
      const QString s = v.toString().trimmed();
      if (!s.isEmpty()) list.append(s);
    }
    if (!list.isEmpty()) m_imageViewerMimePatterns = list;
  }
  m_imageViewerZoomPercent = qBound(1, imageViewer.value("zoomPercent").toInt(100), 1000);
  m_imageViewerFitToWindow = imageViewer.value("fitToWindow").toBool(true);
  m_imageViewerAnimation   = imageViewer.value("animation").toBool(false);
  {
    const QString modeStr = imageViewer.value("transparencyMode").toString();
    m_imageViewerTransparencyMode = (modeStr == QLatin1String("solidColor"))
                                      ? ImageTransparencyMode::SolidColor
                                      : ImageTransparencyMode::Checker;
  }
  // 旧スキーマ (backgroundColor) との後方互換: solidColor 未保存ならそちらから読む
  if (imageViewer.contains("solidColor")) {
    QColor c(imageViewer.value("solidColor").toString());
    if (c.isValid()) m_imageViewerSolidColor = c;
  } else if (imageViewer.contains("backgroundColor")) {
    QColor c(imageViewer.value("backgroundColor").toString());
    if (c.isValid()) m_imageViewerSolidColor = c;
  }
  if (imageViewer.contains("checkerColor1")) {
    QColor c(imageViewer.value("checkerColor1").toString());
    if (c.isValid()) m_imageViewerCheckerColor1 = c;
  }
  if (imageViewer.contains("checkerColor2")) {
    QColor c(imageViewer.value("checkerColor2").toString());
    if (c.isValid()) m_imageViewerCheckerColor2 = c;
  }

  // Load binary viewer settings
  QJsonObject binaryViewer = root.value("binaryViewer").toObject();
  m_binaryViewerUnit     = bytesToBinaryViewerUnit(binaryViewer.value("unitBytes").toInt(1));
  m_binaryViewerEndian   = stringToBinaryViewerEndian(binaryViewer.value("endian").toString());
  m_binaryViewerEncoding = binaryViewer.value("encoding").toString(QStringLiteral("UTF-8"));
  if (binaryViewer.contains("font")) {
    QFont f;
    if (f.fromString(binaryViewer.value("font").toString())) {
      m_binaryViewerFont = f;
    }
  }
  auto loadBinColor = [&](const QString& key, QColor& dst) {
    if (binaryViewer.contains(key)) {
      QColor c(binaryViewer.value(key).toString());
      if (c.isValid()) dst = c;
    }
  };
  loadBinColor("normalFg",   m_binaryViewerNormalFg);
  loadBinColor("normalBg",   m_binaryViewerNormalBg);
  loadBinColor("selectedFg", m_binaryViewerSelectedFg);
  loadBinColor("selectedBg", m_binaryViewerSelectedBg);
  loadBinColor("addressFg",  m_binaryViewerAddressFg);
  loadBinColor("addressBg",  m_binaryViewerAddressBg);

  // メディアビュアー既定 (pdf/csv/markdown の既定は上の各ブロックで読み込み済み)
  QJsonObject mediaViewer = root.value("mediaViewer").toObject();
  if (mediaViewer.contains("extensions")) {
    QStringList list;
    for (const QJsonValue& v : mediaViewer.value("extensions").toArray()) {
      const QString s = v.toString().trimmed();
      if (!s.isEmpty()) list.append(s);
    }
    if (!list.isEmpty()) {
      m_mediaViewerExtensions = list;
      // 既存ユーザーが保存した拡張子リストに、まだ適用されていないリビジョンで
      // 新規追加された既定拡張子だけをマージする。extensionsRevision を持たない
      // 設定 (0.9.6 より前) は revision 1 とみなす。ユーザーが意図的に削除した
      // 拡張子は復活させない (追加分だけを足す)。
      static const QMap<int, QStringList> kMediaExtAddedInRevision = {
        // revision 2 (farman 0.9.6): WMV / MPEG / M2TS 系の動画と WMA 音声を追加
        {2, {"wmv", "mpg", "mpeg", "m2v", "m2ts", "mts", "ts", "wma"}},
      };
      const int savedRev = mediaViewer.value("extensionsRevision").toInt(1);
      for (int r = savedRev + 1; r <= kMediaExtensionsRevision; ++r) {
        for (const QString& ext : kMediaExtAddedInRevision.value(r)) {
          if (!m_mediaViewerExtensions.contains(ext, Qt::CaseInsensitive)) {
            m_mediaViewerExtensions.append(ext);
          }
        }
      }
    }
  }
  m_mediaViewerVolume   = qBound(0, mediaViewer.value("volume").toInt(80), 100);
  m_mediaViewerLoop     = mediaViewer.value("loop").toBool(false);
  m_mediaViewerAutoplay = mediaViewer.value("autoplay").toBool(true);
  m_mediaViewerFitToWindow = mediaViewer.value("fitToWindow").toBool(true);
  m_mediaViewerZoomPercent =
    qBound(1, mediaViewer.value("zoomPercent").toInt(100), 1000);

  // ペイン履歴（ON の時のみ読む。OFF の時は必ず空にする）
  for (int i = 0; i < static_cast<int>(PaneType::Count); ++i) {
    m_paneHistory[i].clear();
  }
  if (m_persistHistory) {
    QJsonObject hist = root.value("paneHistory").toObject();
    auto load = [&](PaneType pane, const QString& key) {
      int idx = static_cast<int>(pane);
      QJsonArray arr = hist.value(key).toArray();
      for (const QJsonValue& v : arr) {
        const QString s = v.toString();
        if (!s.isEmpty()) m_paneHistory[idx].append(s);
      }
    };
    load(PaneType::Left,  "left");
    load(PaneType::Right, "right");
  }

  // Per-pane 初期表示ディレクトリ。旧 restoreLastPath が残っていれば
  // LastSession/Default にマップして互換性を保つ。
  const bool legacyRestore = behavior.value("restoreLastPath").toBool(true);
  const InitialPathMode legacyMode = legacyRestore ? InitialPathMode::LastSession
                                                   : InitialPathMode::Default;

  QJsonObject initialPaths = root.value("initialPaths").toObject();
  auto loadInitialPath = [&](PaneType pane, const QString& key) {
    int idx = static_cast<int>(pane);
    if (initialPaths.contains(key)) {
      QJsonObject obj = initialPaths.value(key).toObject();
      m_initialPathMode[idx]   = stringToInitialPathMode(obj.value("mode").toString());
      m_customInitialPath[idx] = obj.value("customPath").toString();
    } else {
      m_initialPathMode[idx]   = legacyMode;
      m_customInitialPath[idx].clear();
    }
  };
  loadInitialPath(PaneType::Left,  "left");
  loadInitialPath(PaneType::Right, "right");

  // Load window settings
  QJsonObject window = root.value("window").toObject();
  m_windowSizeMode = stringToWindowSizeMode(window.value("sizeMode").toString());
  if (window.contains("customSize")) {
    QJsonObject customSize = window.value("customSize").toObject();
    m_customWindowSize = QSize(
      customSize.value("width").toInt(1200),
      customSize.value("height").toInt(600)
    );
  }
  if (window.contains("lastSize")) {
    QJsonObject lastSize = window.value("lastSize").toObject();
    m_lastWindowSize = QSize(
      lastSize.value("width").toInt(1200),
      lastSize.value("height").toInt(600)
    );
  }

  m_windowPositionMode = stringToWindowPositionMode(window.value("positionMode").toString());
  if (window.contains("customPosition")) {
    QJsonObject customPos = window.value("customPosition").toObject();
    m_customWindowPosition = QPoint(
      customPos.value("x").toInt(0),
      customPos.value("y").toInt(0)
    );
  }
  if (window.contains("lastPosition")) {
    QJsonObject lastPos = window.value("lastPosition").toObject();
    m_lastWindowPosition = QPoint(
      lastPos.value("x").toInt(0),
      lastPos.value("y").toInt(0)
    );
  }

  // Load pane settings
  QJsonObject panes = root.value("panes").toObject();
  if (panes.contains("left")) {
    m_paneSettings[static_cast<int>(PaneType::Left)] =
      jsonToPaneSettings(panes.value("left").toObject());
  }
  if (panes.contains("right")) {
    m_paneSettings[static_cast<int>(PaneType::Right)] =
      jsonToPaneSettings(panes.value("right").toObject());
  }

  // Load per-path overrides
  m_pathOverrides.clear();
  QJsonObject overrides = root.value("pathOverrides").toObject();
  for (auto it = overrides.begin(); it != overrides.end(); ++it) {
    if (it.value().isObject()) {
      m_pathOverrides.insert(it.key(), jsonToPaneSettings(it.value().toObject()));
    }
  }

  // Load bookmarks
  m_bookmarks.clear();
  const QList<Bookmark> defaults = buildDefaultBookmarks();
  QJsonArray bmArr = root.value("bookmarks").toArray();
  for (const QJsonValue& val : bmArr) {
    if (!val.isObject()) continue;
    QJsonObject obj = val.toObject();
    Bookmark b;
    b.name = obj.value("name").toString();
    b.path = obj.value("path").toString();
    if (obj.contains("isDefault")) {
      b.isDefault = obj.value("isDefault").toBool(false);
    } else {
      // 旧フォーマット: isDefault フィールドなし。現行デフォルトリストに
      // パスが含まれていれば、デフォルトとして扱う（削除不可に昇格）。
      for (const Bookmark& d : defaults) {
        if (d.path == b.path) { b.isDefault = true; break; }
      }
    }
    if (!b.path.isEmpty()) m_bookmarks.append(b);
  }

  // version < 2: 旧 Farman で Root ("/") が自動注入されたことがあるため除去。
  // ユーザーが明示的に追加した可能性もあるが、現行仕様では Root をデフォルトに
  // 含めないため、一律撤去する（必要なら再度手動追加できる）。
  if (fileVersion < 2) {
    for (int i = m_bookmarks.size() - 1; i >= 0; --i) {
      if (m_bookmarks[i].path == QStringLiteral("/")) {
        m_bookmarks.removeAt(i);
      }
    }
  }

  // version < 4: 以前はドライブ (Windows の C:/ 等) を isDefault 既定ブックマーク
  // として保存していた。ブックマーク一覧の「検出された場所」がボリューム名付きで
  // 同じドライブを出すため二重表示になっていた。既定として保存済みのドライブ
  // ルート (例 "C:/") を撤去する。ユーザーが手動登録したもの (isDefault=false) は
  // 残す。ドライブは今後「検出された場所」にのみ出る。
  if (fileVersion < 4) {
    static const QRegularExpression driveRoot(QStringLiteral("^[A-Za-z]:/$"));
    for (int i = m_bookmarks.size() - 1; i >= 0; --i) {
      if (m_bookmarks[i].isDefault &&
          driveRoot.match(m_bookmarks[i].path).hasMatch()) {
        m_bookmarks.removeAt(i);
      }
    }
  }

  // 初回のみ: デフォルトブックマークを既存リストにマージ（重複パスはスキップ）。
  if (!m_defaultBookmarksInstalled) {
    for (const Bookmark& d : defaults) {
      bool dup = false;
      for (const Bookmark& b : m_bookmarks) {
        if (b.path == d.path) { dup = true; break; }
      }
      if (!dup) m_bookmarks.append(d);
    }
    m_defaultBookmarksInstalled = true;
  }

  // 外部アプリ (UserCommand) のロード。userCommands キーが無ければ
  // applyDefaults() で投入された組み込み terminal / editor をそのまま使う。
  // ある場合は完全に置換する (= ユーザーが builtin を「削除」状態で保存した
  // ファイルでは、Tools メニューも空になる)。
  if (root.contains("userCommands")) {
    m_userCommands.clear();
    const QJsonArray arr = root.value("userCommands").toArray();
    for (const QJsonValue& v : arr) {
      if (!v.isObject()) continue;
      UserCommand c = userCommandFromJson(v.toObject());
      if (c.id.isEmpty()) continue;
      m_userCommands.append(c);
    }
  }

  // ── テーマ (Light / Dark スキーム) ────────────
  // `themes` ブロックがあれば新形式 (themes.mode + themes.light + themes.dark)。
  // 無ければ旧形式 → m_ に既に「実質 Light」の値が入っている前提で、両スキーム
  // ともそのスナップショットで初期化する (ユーザーが Dark に切替したら自分で
  // 編集する想定)。
  ColorScheme migrationSnapshot = collectThemeFields();
  if (root.contains("themes") && root.value("themes").isObject()) {
    QJsonObject themes = root.value("themes").toObject();
    const QString modeStr = themes.value("mode").toString("auto");
    if      (modeStr == "light") m_themeMode = ThemeMode::Light;
    else if (modeStr == "dark")  m_themeMode = ThemeMode::Dark;
    else                          m_themeMode = ThemeMode::Auto;

    auto loadScheme = [&](const QJsonObject& obj, ColorScheme fallback) -> ColorScheme {
      colorSchemeFromJson(obj, fallback);
      return fallback;
    };
    m_lightScheme = loadScheme(themes.value("light").toObject(), m_lightScheme);
    m_darkScheme  = loadScheme(themes.value("dark").toObject(),  m_darkScheme);

    // m_ フィールドの値は appearance/textViewer/... ブロックを読んだ
    // 直後の状態 = 「旧アクティブ値」になっている。新 effectiveTheme に
    // 合わせて m_ を上書きしないと、Dark を選んでいたのに Light の m_ が
    // 残るような混在状態になる。
    m_lastEffective = effectiveTheme();
    applyThemeFields(m_lastEffective == ThemeMode::Light ? m_lightScheme : m_darkScheme);
  } else {
    // 旧形式: いま m_ に入っている値を Light/Dark 双方のベースにする。
    m_lightScheme   = migrationSnapshot;
    m_darkScheme    = migrationSnapshot;
    m_themeMode     = ThemeMode::Auto;
    m_lastEffective = detectOsTheme();
  }

  // version < 3: カテゴリ色の既定を正規化する (非破壊)。
  //  - ディレクトリは全状態 (通常/選択/非アクティブ/非アクティブ選択) で太字。
  //    「ディレクトリは常に太字」を既定方針としたため一律 true にする。
  //  - 選択中 / 非アクティブのカテゴリ色が未設定 (fg/bg とも無効) の古いファイルは
  //    テーマ既定色で埋める。既定色はパレットのフォールバック色と一致させてある
  //    ので見た目は変わらない (設定 UI の "(none)" 表示だけが解消される)。
  //  - 明示的に設定済みの色は尊重する (無効セルのみ補完)。
  if (fileVersion < 3) {
    auto normalize = [](ColorScheme& sc, const ColorScheme& def) {
      const int dirIdx = static_cast<int>(FileCategory::Directory);
      sc.categoryColors[dirIdx].bold                 = true;
      sc.selectedCategoryColors[dirIdx].bold         = true;
      sc.inactiveCategoryColors[dirIdx].bold         = true;
      sc.inactiveSelectedCategoryColors[dirIdx].bold = true;
      auto fillEmpty = [](auto& arr, const auto& darr) {
        for (size_t i = 0; i < arr.size(); ++i) {
          if (!arr[i].foreground.isValid() && !arr[i].background.isValid()) {
            arr[i].foreground = darr[i].foreground;
            arr[i].background = darr[i].background;
          }
        }
      };
      fillEmpty(sc.selectedCategoryColors,         def.selectedCategoryColors);
      fillEmpty(sc.inactiveCategoryColors,         def.inactiveCategoryColors);
      fillEmpty(sc.inactiveSelectedCategoryColors, def.inactiveSelectedCategoryColors);
    };
    normalize(m_lightScheme, defaultLightScheme());
    normalize(m_darkScheme,  defaultDarkScheme());
    // アクティブ側を m_ 作業コピーへ再反映。
    applyThemeFields(m_lastEffective == ThemeMode::Light ? m_lightScheme : m_darkScheme);
  }

  // version < 5: フォント (形状・サイズ) とファイルリスト行高をライト/ダーク
  // 共通にする方針へ変更。以前は per-theme 保存だったため、有効テーマ側の値を
  // 両スキームへコピーして揃える (現在の見た目を維持)。色・カテゴリ太字は
  // per-theme のまま。
  if (fileVersion < 5) {
    const ColorScheme& srcS =
      (m_lastEffective == ThemeMode::Dark) ? m_darkScheme : m_lightScheme;
    auto syncCommon = [&](ColorScheme& dst) {
      dst.uiFont             = srcS.uiFont;
      dst.listFont           = srcS.listFont;
      dst.addressFont        = srcS.addressFont;
      dst.textViewerFont     = srcS.textViewerFont;
      dst.binaryViewerFont   = srcS.binaryViewerFont;
      dst.csvViewerFont      = srcS.csvViewerFont;
      dst.markdownViewerFont = srcS.markdownViewerFont;
      dst.fileListRowHeight  = srcS.fileListRowHeight;
    };
    syncCommon(m_lightScheme);
    syncCommon(m_darkScheme);
    applyThemeFields(m_lastEffective == ThemeMode::Light ? m_lightScheme : m_darkScheme);
  }

  qDebug() << "Settings::load: loaded settings from" << filePath
           << "(theme mode =" << static_cast<int>(m_themeMode)
           << ", effective =" << static_cast<int>(m_lastEffective) << ")";
  emit settingsChanged();
}

void Settings::save() const {
  QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  QDir dir;
  if (!dir.exists(configPath)) {
    if (!dir.mkpath(configPath)) {
      qWarning() << "Settings::save: failed to create config directory:" << configPath;
      return;
    }
  }

  QString filePath = configPath + "/settings.json";

  QJsonObject root;
  // version 5: カテゴリ色の既定正規化 (v3) / ドライブルート既定ブックマーク
  // 撤去 (v4) に加え、フォントのライト・ダーク共通化 (v5) を済ませたことを示す。
  // load 側の fileVersion < 3 / < 4 / < 5 移行と対応。
  root["version"] = 5;

  // Save appearance settings
  QJsonObject appearance;
  QJsonObject fontObj;
  fontObj["family"] = m_font.family();
  fontObj["pointSize"] = m_font.pointSize();
  appearance["font"] = fontObj;
  QJsonObject addressFontObj;
  addressFontObj["family"] = m_addressFont.family();
  addressFontObj["pointSize"] = m_addressFont.pointSize();
  appearance["addressFont"] = addressFontObj;
  appearance["fileSizeFormatDual"]         = fileSizeFormatToString(m_fileSizeFormatDual);
  appearance["fileSizeFormatSingle"]       = fileSizeFormatToString(m_fileSizeFormatSingle);
  appearance["fileSizeThousandsSeparatorDual"]   = m_fileSizeThousandsSeparatorDual;
  appearance["fileSizeThousandsSeparatorSingle"] = m_fileSizeThousandsSeparatorSingle;
  auto saveCols = [](const ListColumnVisibility& v) -> QJsonObject {
    QJsonObject o;
    o["type"]         = v.type;
    o["size"]         = v.size;
    o["lastModified"] = v.lastModified;
    o["created"]      = v.created;
    o["permissions"]  = v.permissions;
    o["attributes"]   = v.attributes;
    o["owner"]        = v.owner;
    o["group"]        = v.group;
    o["linkTarget"]   = v.linkTarget;
    return o;
  };
  appearance["listColumnsDual"]   = saveCols(m_listColumnsDual);
  appearance["listColumnsSingle"] = saveCols(m_listColumnsSingle);
  appearance["fileListRowHeight"]          = m_fileListRowHeight;
  appearance["dateTimeFormatDual"]         = m_dateTimeFormatDual;
  appearance["dateTimeFormatSingle"]       = m_dateTimeFormatSingle;

  QJsonArray colorRulesArray;
  for (const ColorRule& rule : m_colorRules) {
    colorRulesArray.append(colorRuleToJson(rule));
  }
  appearance["colorRules"] = colorRulesArray;

  auto catStateToJson = [](const CategoryColor colors[]) {
    QJsonObject obj;
    for (int i = 0; i < static_cast<int>(FileCategory::Count); ++i) {
      const QString key = fileCategoryToString(static_cast<FileCategory>(i));
      obj[key] = categoryColorToJson(colors[i]);
    }
    return obj;
  };
  QJsonObject active;
  active["normal"]   = catStateToJson(m_categoryColors);
  active["selected"] = catStateToJson(m_selectedCategoryColors);
  QJsonObject inactive;
  inactive["normal"]   = catStateToJson(m_inactiveCategoryColors);
  inactive["selected"] = catStateToJson(m_inactiveSelectedCategoryColors);
  QJsonObject catColors;
  catColors["active"]   = active;
  catColors["inactive"] = inactive;
  appearance["categoryColors"]      = catColors;
  appearance["useInactivePaneColors"] = m_useInactivePaneColors;

  QJsonObject addressColors;
  addressColors["foreground"] = m_addressForeground.name(QColor::HexArgb);
  addressColors["background"] = m_addressBackground.name(QColor::HexArgb);
  appearance["addressColors"] = addressColors;

  QJsonObject cursorColorsJson;
  cursorColorsJson["active"]   = m_cursorActiveColor.name(QColor::HexArgb);
  cursorColorsJson["inactive"] = m_cursorInactiveColor.name(QColor::HexArgb);
  appearance["cursorColors"] = cursorColorsJson;
  appearance["cursorShape"] = (m_cursorShape == CursorShape::RowBackground)
                                ? QStringLiteral("rowBackground")
                                : QStringLiteral("underline");
  appearance["cursorThickness"] = m_cursorThickness;

  root["appearance"] = appearance;

  // Save behavior settings
  QJsonObject behavior;
  behavior["confirmOnExit"] = m_confirmOnExit;
  behavior["singleInstance"] = m_singleInstance;
  behavior["pluginsDirectory"] = m_pluginsDirectory;
  {
    QJsonArray disabled;
    for (const QString& pluginId : m_disabledViewerPlugins) {
      disabled.append(pluginId);
    }
    behavior["disabledViewerPlugins"] = disabled;
  }
  {
    QJsonObject associations;
    for (auto it = m_viewerAssociations.cbegin(); it != m_viewerAssociations.cend(); ++it) {
      associations[it.key()] = it.value();
    }
    behavior["viewerAssociations"] = associations;
  }
  behavior["syncBrowseShowDisabledDialog"] = m_syncBrowseShowDisabledDialog;
  behavior["viewerMode"] = (m_viewerMode == ViewerMode::External)
                             ? QStringLiteral("external")
                             : QStringLiteral("inline");
  behavior["showToolbar"] = m_showToolbar;
  behavior["layoutMode"]  = QString::fromLatin1(layoutModeKey(m_layoutMode));
  {
    QJsonObject preview;
    preview["debounceMs"]       = m_previewDebounceMs;
    preview["maxFileSizeBytes"] = static_cast<double>(m_previewMaxFileSizeBytes);
    behavior["preview"] = preview;
  }
  {
    QJsonObject au;
    au["checkOnStartup"] = m_autoUpdateCheckOnStartup;
    au["silent"]         = m_autoUpdateSilent;
    au["lastCheckedAt"]  = m_autoUpdateLastCheckedAt.isValid()
      ? m_autoUpdateLastCheckedAt.toString(Qt::ISODate)
      : QString();
    QJsonArray skipped;
    for (const QString& s : m_autoUpdateSkippedVersions) skipped.append(s);
    au["skipped"] = skipped;
    au["channel"] = m_autoUpdateChannel;
    behavior["autoUpdate"] = au;
  }
  behavior["whatsNewShownVersion"] = m_whatsNewShownVersion;
  switch (m_language) {
    case LanguageMode::English:  behavior["language"] = "en";   break;
    case LanguageMode::Japanese: behavior["language"] = "ja";   break;
    case LanguageMode::Auto:     behavior["language"] = "auto"; break;
  }
  {
    auto paneModeStr = [](ListViewMode m) -> QString {
      switch (m) {
        case ListViewMode::ThumbnailSmall:  return QStringLiteral("thumbnail-small");
        case ListViewMode::ThumbnailMedium: return QStringLiteral("thumbnail-medium");
        case ListViewMode::ThumbnailLarge:  return QStringLiteral("thumbnail-large");
        case ListViewMode::List:
        default:                            return QStringLiteral("list");
      }
    };
    behavior["leftPaneViewMode"]  = paneModeStr(m_paneViewMode[static_cast<int>(PaneType::Left)]);
    behavior["rightPaneViewMode"] = paneModeStr(m_paneViewMode[static_cast<int>(PaneType::Right)]);
  }
  behavior["cursorLoop"] = m_cursorLoop;
  behavior["typeAheadIncludeDotfiles"] = m_typeAheadIncludeDotfiles;
  behavior["persistHistory"] = m_persistHistory;
  behavior["autoRenameTemplate"] = m_autoRenameTemplate;
  behavior["defaultDeleteToTrash"] = m_defaultDeleteToTrash;
  behavior["progressAutoClose"]    = m_progressAutoClose;
  {
    QJsonArray arr;
    for (const QString& p : m_searchExcludeDirs) arr.append(p);
    behavior["searchExcludeDirs"] = arr;
  }
  behavior["defaultBookmarksInstalled"] = m_defaultBookmarksInstalled;
  root["behavior"] = behavior;

  // Save log settings
  {
    QJsonObject logObj;
    logObj["visible"]       = m_logVisible;
    logObj["paneHeight"]    = m_logPaneHeight;
    logObj["toFile"]        = m_logToFile;
    logObj["retentionDays"] = m_logRetentionDays;
    logObj["directory"]     = m_logDirectory;
    root["log"] = logObj;
  }

  // Save markdown viewer settings (Text の手前で出す)
  {
    QJsonObject mdViewer;
    QJsonArray arr;
    for (const QString& s : m_markdownViewerExtensions) arr.append(s);
    mdViewer["extensions"] = arr;
    mdViewer["showSource"] = m_markdownViewerShowSource;
    mdViewer["font"]       = m_markdownViewerFont.toString();
    // 無効色は空文字列で保存 (テーマ既定に追従)。
    mdViewer["fg"]   = m_markdownViewerFg.isValid()   ? m_markdownViewerFg.name(QColor::HexArgb)   : QString();
    mdViewer["bg"]   = m_markdownViewerBg.isValid()   ? m_markdownViewerBg.name(QColor::HexArgb)   : QString();
    mdViewer["link"] = m_markdownViewerLink.isValid() ? m_markdownViewerLink.name(QColor::HexArgb) : QString();
    root["markdownViewer"] = mdViewer;
  }

  // Save PDF viewer settings (Text の手前で出す)
  {
    QJsonObject pdfViewer;
    QJsonArray arr;
    for (const QString& s : m_pdfViewerExtensions) arr.append(s);
    pdfViewer["extensions"] = arr;
    pdfViewer["continuous"] = m_pdfViewerContinuous;
    pdfViewer["fitMode"] =
      (m_pdfViewerFitMode == PdfViewerFitMode::FitWidth) ? QStringLiteral("fitWidth")
      : (m_pdfViewerFitMode == PdfViewerFitMode::FitPage) ? QStringLiteral("fitPage")
                                                          : QStringLiteral("actual");
    root["pdfViewer"] = pdfViewer;
  }

  // Save CSV / TSV viewer settings (Text の手前で出す)
  {
    QJsonObject csvViewer;
    QJsonArray arr;
    for (const QString& s : m_csvViewerExtensions) arr.append(s);
    csvViewer["extensions"] = arr;
    csvViewer["delimiter"]        = m_csvViewerDelimiter;
    csvViewer["firstRowAsHeader"] = m_csvViewerFirstRowAsHeader;
    csvViewer["font"]             = m_csvViewerFont.toString();
    root["csvViewer"] = csvViewer;
  }

  // Save text viewer settings
  QJsonObject textViewer;
  {
    QJsonArray arr;
    for (const QString& s : m_textViewerExtensions) arr.append(s);
    textViewer["extensions"] = arr;
  }
  {
    QJsonArray arr;
    for (const QString& s : m_textViewerMimePatterns) arr.append(s);
    textViewer["mimePatterns"] = arr;
  }
  textViewer["font"]            = m_textViewerFont.toString();
  textViewer["encoding"]        = m_textViewerEncoding;
  textViewer["showLineNumbers"] = m_textViewerShowLineNumbers;
  textViewer["wordWrap"]        = m_textViewerWordWrap;
  textViewer["normalFg"]        = m_textViewerNormalFg.name(QColor::HexArgb);
  if (m_textViewerNormalBg.isValid()) {
    textViewer["normalBg"]      = m_textViewerNormalBg.name(QColor::HexArgb);
  }
  textViewer["selectedFg"]      = m_textViewerSelectedFg.name(QColor::HexArgb);
  textViewer["selectedBg"]      = m_textViewerSelectedBg.name(QColor::HexArgb);
  textViewer["lineNumberFg"]    = m_textViewerLineNumberFg.name(QColor::HexArgb);
  textViewer["lineNumberBg"]    = m_textViewerLineNumberBg.name(QColor::HexArgb);
  root["textViewer"] = textViewer;

  // Save image viewer settings
  QJsonObject imageViewer;
  {
    QJsonArray arr;
    for (const QString& s : m_imageViewerExtensions) arr.append(s);
    imageViewer["extensions"] = arr;
  }
  {
    QJsonArray arr;
    for (const QString& s : m_imageViewerMimePatterns) arr.append(s);
    imageViewer["mimePatterns"] = arr;
  }
  imageViewer["zoomPercent"]      = m_imageViewerZoomPercent;
  imageViewer["fitToWindow"]      = m_imageViewerFitToWindow;
  imageViewer["animation"]        = m_imageViewerAnimation;
  imageViewer["transparencyMode"] = (m_imageViewerTransparencyMode == ImageTransparencyMode::SolidColor)
                                     ? QStringLiteral("solidColor")
                                     : QStringLiteral("checker");
  imageViewer["solidColor"]       = m_imageViewerSolidColor.name(QColor::HexArgb);
  imageViewer["checkerColor1"]    = m_imageViewerCheckerColor1.name(QColor::HexArgb);
  imageViewer["checkerColor2"]    = m_imageViewerCheckerColor2.name(QColor::HexArgb);
  root["imageViewer"] = imageViewer;

  // Save binary viewer settings
  QJsonObject binaryViewer;
  binaryViewer["unitBytes"] = binaryViewerUnitToBytes(m_binaryViewerUnit);
  binaryViewer["endian"]    = binaryViewerEndianToString(m_binaryViewerEndian);
  binaryViewer["encoding"]  = m_binaryViewerEncoding;
  binaryViewer["font"]      = m_binaryViewerFont.toString();
  binaryViewer["normalFg"]   = m_binaryViewerNormalFg.name(QColor::HexArgb);
  if (m_binaryViewerNormalBg.isValid()) {
    binaryViewer["normalBg"] = m_binaryViewerNormalBg.name(QColor::HexArgb);
  }
  binaryViewer["selectedFg"] = m_binaryViewerSelectedFg.name(QColor::HexArgb);
  binaryViewer["selectedBg"] = m_binaryViewerSelectedBg.name(QColor::HexArgb);
  binaryViewer["addressFg"]  = m_binaryViewerAddressFg.name(QColor::HexArgb);
  binaryViewer["addressBg"]  = m_binaryViewerAddressBg.name(QColor::HexArgb);
  root["binaryViewer"] = binaryViewer;

  // メディアビュアー既定 (pdf/csv/markdown の既定は拡張子保存ブロックに同梱)
  QJsonObject mediaViewer;
  {
    QJsonArray arr;
    for (const QString& s : m_mediaViewerExtensions) arr.append(s);
    mediaViewer["extensions"] = arr;
  }
  // 適用済みの既定拡張子リビジョンを記録する (次回 load 時の差分マージ用)。
  mediaViewer["extensionsRevision"] = kMediaExtensionsRevision;
  mediaViewer["volume"]   = m_mediaViewerVolume;
  mediaViewer["loop"]     = m_mediaViewerLoop;
  mediaViewer["autoplay"] = m_mediaViewerAutoplay;
  mediaViewer["fitToWindow"] = m_mediaViewerFitToWindow;
  mediaViewer["zoomPercent"] = m_mediaViewerZoomPercent;
  root["mediaViewer"] = mediaViewer;

  // ペイン履歴（ON のときだけディスクに出す）
  if (m_persistHistory) {
    QJsonObject hist;
    auto toArr = [](const QStringList& list) {
      QJsonArray arr;
      for (const QString& s : list) arr.append(s);
      return arr;
    };
    hist["left"]  = toArr(m_paneHistory[static_cast<int>(PaneType::Left)]);
    hist["right"] = toArr(m_paneHistory[static_cast<int>(PaneType::Right)]);
    root["paneHistory"] = hist;
  }

  // Per-pane 初期表示ディレクトリ
  QJsonObject initialPaths;
  auto paneInitial = [this](PaneType pane) -> QJsonObject {
    int idx = static_cast<int>(pane);
    QJsonObject obj;
    obj["mode"]       = initialPathModeToString(m_initialPathMode[idx]);
    obj["customPath"] = m_customInitialPath[idx];
    return obj;
  };
  initialPaths["left"]  = paneInitial(PaneType::Left);
  initialPaths["right"] = paneInitial(PaneType::Right);
  root["initialPaths"]  = initialPaths;

  // Save window settings
  QJsonObject window;
  window["sizeMode"] = windowSizeModeToString(m_windowSizeMode);

  QJsonObject customSize;
  customSize["width"] = m_customWindowSize.width();
  customSize["height"] = m_customWindowSize.height();
  window["customSize"] = customSize;

  QJsonObject lastSize;
  lastSize["width"] = m_lastWindowSize.width();
  lastSize["height"] = m_lastWindowSize.height();
  window["lastSize"] = lastSize;

  window["positionMode"] = windowPositionModeToString(m_windowPositionMode);

  QJsonObject customPos;
  customPos["x"] = m_customWindowPosition.x();
  customPos["y"] = m_customWindowPosition.y();
  window["customPosition"] = customPos;

  QJsonObject lastPos;
  lastPos["x"] = m_lastWindowPosition.x();
  lastPos["y"] = m_lastWindowPosition.y();
  window["lastPosition"] = lastPos;

  root["window"] = window;

  // Save pane settings
  QJsonObject panes;
  panes["left"] = paneSettingsToJson(m_paneSettings[static_cast<int>(PaneType::Left)]);
  panes["right"] = paneSettingsToJson(m_paneSettings[static_cast<int>(PaneType::Right)]);
  root["panes"] = panes;

  // Save per-path overrides
  QJsonObject overrides;
  for (auto it = m_pathOverrides.begin(); it != m_pathOverrides.end(); ++it) {
    overrides[it.key()] = paneSettingsToJson(it.value());
  }
  root["pathOverrides"] = overrides;

  // Save bookmarks
  QJsonArray bmArr;
  for (const Bookmark& b : m_bookmarks) {
    QJsonObject obj;
    obj["name"] = b.name;
    obj["path"] = b.path;
    if (b.isDefault) obj["isDefault"] = true;
    bmArr.append(obj);
  }
  root["bookmarks"] = bmArr;

  // Save user commands (外部アプリ連携)
  {
    QJsonArray arr;
    for (const UserCommand& c : m_userCommands) {
      arr.append(userCommandToJson(c));
    }
    root["userCommands"] = arr;
  }

  // Save themes (Light / Dark スキームの永続化)
  // m_ フィールドには「現在 active なテーマの値」が乗っている前提で、
  // active 側のスキームスナップショットを今ここで取り直してから書き出す。
  // これにより「set...() でいじった値 → save()」の流れで両スキーム JSON が
  // 矛盾しないことを保証する。
  {
    ColorScheme snap = collectThemeFields();
    Settings* mutSelf = const_cast<Settings*>(this);
    if (m_lastEffective == ThemeMode::Light) mutSelf->m_lightScheme = snap;
    else                                      mutSelf->m_darkScheme  = snap;

    QJsonObject themes;
    switch (m_themeMode) {
      case ThemeMode::Light: themes["mode"] = "light"; break;
      case ThemeMode::Dark:  themes["mode"] = "dark";  break;
      case ThemeMode::Auto:  themes["mode"] = "auto";  break;
    }
    themes["light"] = colorSchemeToJson(m_lightScheme);
    themes["dark"]  = colorSchemeToJson(m_darkScheme);
    root["themes"] = themes;
  }

  QJsonDocument doc(root);
  QByteArray data = doc.toJson(QJsonDocument::Indented);

  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly)) {
    qWarning() << "Settings::save: failed to open settings file for writing:" << filePath;
    return;
  }

  file.write(data);
  file.close();

  qDebug() << "Settings::save: saved settings to" << filePath;
  emit settingsChanged();
}

} // namespace Farman
