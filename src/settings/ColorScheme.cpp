#include "ColorScheme.h"
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonObject>

namespace Farman {

namespace {

QFont defaultMonospaceFont() {
  QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  f.setPointSize(12);
  return f;
}

QFont defaultUiFont() {
  // ファイルリスト用は OS 標準の UI フォント。サイズだけ少し詰める。
  QFont f;
  f.setPointSize(11);
  return f;
}

void fillLightCategories(ColorScheme& s) {
  // Normal / Hidden / Directory の 4 状態 (通常 / 選択 / 非アクティブ /
  // 非アクティブ選択)。Light テーマは「文字色のみ淡く差を付ける」既定。
  // (これまでの applyDefaults() の振る舞いを踏襲)
  for (auto& c : s.categoryColors)                    c = {};
  for (auto& c : s.selectedCategoryColors)            c = {};
  for (auto& c : s.inactiveCategoryColors)            c = {};
  for (auto& c : s.inactiveSelectedCategoryColors)    c = {};

  // Directory はデフォルトで「太字 + 青系」
  const int dirIdx = static_cast<int>(FileCategory::Directory);
  s.categoryColors[dirIdx].foreground = QColor(0x10, 0x40, 0xC0);
  s.categoryColors[dirIdx].bold       = true;
  // Hidden は薄字
  const int hidIdx = static_cast<int>(FileCategory::Hidden);
  s.categoryColors[hidIdx].foreground = QColor(0x80, 0x80, 0x80);
}

void fillDarkCategories(ColorScheme& s) {
  for (auto& c : s.categoryColors)                    c = {};
  for (auto& c : s.selectedCategoryColors)            c = {};
  for (auto& c : s.inactiveCategoryColors)            c = {};
  for (auto& c : s.inactiveSelectedCategoryColors)    c = {};

  // Dark では通常テキストを明るく
  const int normIdx = static_cast<int>(FileCategory::Normal);
  s.categoryColors[normIdx].foreground = QColor(0xE0, 0xE0, 0xE0);

  // Directory は明るい青 + 太字
  const int dirIdx = static_cast<int>(FileCategory::Directory);
  s.categoryColors[dirIdx].foreground = QColor(0x6E, 0x9F, 0xFF);
  s.categoryColors[dirIdx].bold       = true;
  // Hidden は灰色
  const int hidIdx = static_cast<int>(FileCategory::Hidden);
  s.categoryColors[hidIdx].foreground = QColor(0x80, 0x80, 0x80);
}

} // namespace

ColorScheme defaultLightScheme() {
  ColorScheme s;

  // ベース色 (Light): 白背景 + 黒文字
  s.baseBackground     = QColor(Qt::white);
  s.baseForeground     = QColor(Qt::black);
  s.uiFont             = defaultUiFont();

  // フォント / 行高
  s.listFont           = defaultUiFont();
  s.addressFont        = defaultUiFont();
  s.fileListRowHeight  = 0;  // Auto

  // ファイル種別
  fillLightCategories(s);

  // アドレス / カーソル
  s.addressForeground  = QColor(Qt::black);
  s.addressBackground  = QColor(0xE0, 0xE0, 0xE0);
  // アーカイブ内: 明るい暖色 (黄系) で「いま中にいる」ことを明示
  s.archiveAddressForeground = QColor(Qt::black);
  s.archiveAddressBackground = QColor(0xFF, 0xE9, 0xA8);
  s.cursorActiveColor  = QColor(Qt::black);
  s.cursorInactiveColor= QColor(Qt::lightGray);

  // ディレクトリ比較 (Light)
  s.compareDifferForeground   = QColor(Qt::black);
  s.compareDifferBackground   = QColor(0xFF, 0xD8, 0xA8);  // 薄いオレンジ
  s.compareOnlyHereForeground = QColor(Qt::black);
  s.compareOnlyHereBackground = QColor(0xC8, 0xE6, 0xB4);  // 薄い緑

  // テキストビュアー
  s.textViewerFont          = defaultMonospaceFont();
  s.textViewerNormalFg      = QColor(Qt::black);
  s.textViewerNormalBg      = QColor();  // パレット既定
  s.textViewerSelectedFg    = QColor(Qt::white);
  s.textViewerSelectedBg    = QColor(0x31, 0x6A, 0xC5);
  s.textViewerLineNumberFg  = QColor(Qt::darkGray);
  s.textViewerLineNumberBg  = QColor(0xF0, 0xF0, 0xF0);

  // 画像ビュアー
  s.imageViewerSolidColor   = QColor(Qt::white);

  // バイナリビュアー
  s.binaryViewerFont        = defaultMonospaceFont();
  s.binaryViewerNormalFg    = QColor(Qt::black);
  s.binaryViewerNormalBg    = QColor();  // パレット既定
  s.binaryViewerSelectedFg  = QColor(Qt::white);
  s.binaryViewerSelectedBg  = QColor(0x31, 0x6A, 0xC5);
  s.binaryViewerAddressFg   = QColor(Qt::darkGray);
  s.binaryViewerAddressBg   = QColor(0xF0, 0xF0, 0xF0);

  // CSV / TSV ビュアー (等幅で表を揃える) / Markdown ビュアー (本文は比例フォント)
  s.csvViewerFont           = defaultMonospaceFont();
  s.markdownViewerFont      = defaultUiFont();

  return s;
}

ColorScheme defaultDarkScheme() {
  ColorScheme s;

  // ベース色 (Dark): 暗背景 + 明文字
  s.baseBackground     = QColor(0x1E, 0x1E, 0x1E);
  s.baseForeground     = QColor(0xE0, 0xE0, 0xE0);
  s.uiFont             = defaultUiFont();

  s.listFont           = defaultUiFont();
  s.addressFont        = defaultUiFont();
  s.fileListRowHeight  = 0;

  fillDarkCategories(s);

  // アドレス: 暗背景 + 明文字
  s.addressForeground  = QColor(0xE0, 0xE0, 0xE0);
  s.addressBackground  = QColor(0x30, 0x30, 0x30);
  // アーカイブ内: 暗いオレンジ系を地色に
  s.archiveAddressForeground = QColor(0xFF, 0xE9, 0xA8);
  s.archiveAddressBackground = QColor(0x5A, 0x42, 0x10);
  s.cursorActiveColor  = QColor(0xE0, 0xE0, 0xE0);
  s.cursorInactiveColor= QColor(0x70, 0x70, 0x70);

  // ディレクトリ比較 (Dark)
  s.compareDifferForeground   = QColor(0xFF, 0xE0, 0xB0);
  s.compareDifferBackground   = QColor(0x5A, 0x3A, 0x10);   // 暗いオレンジ
  s.compareOnlyHereForeground = QColor(0xC8, 0xE6, 0xB4);
  s.compareOnlyHereBackground = QColor(0x1F, 0x4A, 0x20);   // 暗い緑

  // テキストビュアー (Solarized Dark 寄りの色)
  s.textViewerFont          = defaultMonospaceFont();
  s.textViewerNormalFg      = QColor(0xE0, 0xE0, 0xE0);
  s.textViewerNormalBg      = QColor(0x1E, 0x1E, 0x1E);
  s.textViewerSelectedFg    = QColor(Qt::white);
  s.textViewerSelectedBg    = QColor(0x26, 0x4F, 0x78);
  s.textViewerLineNumberFg  = QColor(0x80, 0x80, 0x80);
  s.textViewerLineNumberBg  = QColor(0x25, 0x25, 0x26);

  // 画像ビュアー: 透明部分の色も暗めに
  s.imageViewerSolidColor   = QColor(0x1E, 0x1E, 0x1E);

  // バイナリビュアー
  s.binaryViewerFont        = defaultMonospaceFont();
  s.binaryViewerNormalFg    = QColor(0xE0, 0xE0, 0xE0);
  s.binaryViewerNormalBg    = QColor(0x1E, 0x1E, 0x1E);
  s.binaryViewerSelectedFg  = QColor(Qt::white);
  s.binaryViewerSelectedBg  = QColor(0x26, 0x4F, 0x78);
  s.binaryViewerAddressFg   = QColor(0x80, 0x80, 0x80);
  s.binaryViewerAddressBg   = QColor(0x25, 0x25, 0x26);

  // CSV / TSV ビュアー / Markdown ビュアー (フォントはテーマで変えない既定値)
  s.csvViewerFont           = defaultMonospaceFont();
  s.markdownViewerFont      = defaultUiFont();

  return s;
}

// ── JSON シリアライズ ─────────────────────────────
// 注意: 既存の Settings::save / load 内のフォーマットと矛盾しないよう、
// 同じキー名を使う ("foreground" / "background" / "bold" / "name" 等)。

namespace {

QJsonObject fontToJson(const QFont& f) {
  QJsonObject o;
  // QFont::toString() は family/size/weight/italic 等を 1 行にまとめた可逆形式
  o["spec"] = f.toString();
  return o;
}

QFont fontFromJson(const QJsonObject& obj, const QFont& fallback) {
  if (obj.contains("spec")) {
    QFont f;
    if (f.fromString(obj.value("spec").toString())) return f;
  }
  // 旧形式互換: family + pointSize 直書き
  if (obj.contains("family") || obj.contains("pointSize")) {
    QFont f = fallback;
    if (obj.contains("family"))    f.setFamily(obj.value("family").toString());
    if (obj.contains("pointSize")) f.setPointSize(obj.value("pointSize").toInt());
    return f;
  }
  return fallback;
}

QJsonObject colorRuleJson(const ColorRule& r) {
  QJsonObject o;
  o["pattern"] = r.pattern;
  if (r.foreground.isValid()) o["foreground"] = r.foreground.name(QColor::HexArgb);
  if (r.background.isValid()) o["background"] = r.background.name(QColor::HexArgb);
  o["bold"] = r.bold;
  return o;
}

ColorRule colorRuleFrom(const QJsonObject& o) {
  ColorRule r;
  r.pattern = o.value("pattern").toString();
  if (o.contains("foreground")) r.foreground = QColor(o.value("foreground").toString());
  if (o.contains("background")) r.background = QColor(o.value("background").toString());
  r.bold = o.value("bold").toBool(false);
  return r;
}

QJsonObject categoryColorJson(const CategoryColor& c) {
  QJsonObject o;
  if (c.foreground.isValid()) o["foreground"] = c.foreground.name(QColor::HexArgb);
  if (c.background.isValid()) o["background"] = c.background.name(QColor::HexArgb);
  o["bold"] = c.bold;
  return o;
}

CategoryColor categoryColorFrom(const QJsonObject& o, const CategoryColor& fallback) {
  CategoryColor c = fallback;
  if (o.contains("foreground")) {
    const QString s = o.value("foreground").toString();
    c.foreground = s.isEmpty() ? QColor() : QColor(s);
  }
  if (o.contains("background")) {
    const QString s = o.value("background").toString();
    c.background = s.isEmpty() ? QColor() : QColor(s);
  }
  if (o.contains("bold")) c.bold = o.value("bold").toBool(c.bold);
  return c;
}

const char* categoryKey(int idx) {
  switch (idx) {
    case 0: return "normal";
    case 1: return "hidden";
    case 2: return "directory";
    default: return "unknown";
  }
}

QJsonObject categoriesJson(const std::array<CategoryColor, 3>& arr) {
  QJsonObject o;
  for (int i = 0; i < 3; ++i) {
    o[QLatin1String(categoryKey(i))] = categoryColorJson(arr[i]);
  }
  return o;
}

void categoriesFrom(const QJsonObject& o, std::array<CategoryColor, 3>& arr) {
  for (int i = 0; i < 3; ++i) {
    const QString k = QLatin1String(categoryKey(i));
    if (o.contains(k)) {
      arr[i] = categoryColorFrom(o.value(k).toObject(), arr[i]);
    }
  }
}

QString colorString(const QColor& c) {
  // 無効色は空文字 (= 未指定 / パレット既定にフォールバック) として扱えるように
  return c.isValid() ? c.name(QColor::HexArgb) : QString();
}

void readColor(const QJsonObject& o, const char* key, QColor& dst) {
  if (!o.contains(QLatin1String(key))) return;
  const QString s = o.value(QLatin1String(key)).toString();
  // 空文字なら無効化扱い (パレット既定)
  dst = s.isEmpty() ? QColor() : QColor(s);
}

} // namespace

QJsonObject colorSchemeToJson(const ColorScheme& s) {
  QJsonObject o;
  // ── ベース色 + UI フォント (新規; 旧 JSON にはフィールド無し)
  o["baseBackground"] = colorString(s.baseBackground);
  o["baseForeground"] = colorString(s.baseForeground);
  o["uiFont"]         = fontToJson(s.uiFont);

  // ── ファイルリスト
  o["listFont"]          = fontToJson(s.listFont);
  o["addressFont"]       = fontToJson(s.addressFont);
  o["fileListRowHeight"] = s.fileListRowHeight;

  QJsonArray rules;
  for (const ColorRule& r : s.colorRules) rules.append(colorRuleJson(r));
  o["colorRules"] = rules;

  QJsonObject cats;
  cats["normal"]            = categoriesJson(s.categoryColors);
  cats["selected"]          = categoriesJson(s.selectedCategoryColors);
  cats["inactiveNormal"]    = categoriesJson(s.inactiveCategoryColors);
  cats["inactiveSelected"]  = categoriesJson(s.inactiveSelectedCategoryColors);
  o["categories"] = cats;

  o["addressForeground"]  = colorString(s.addressForeground);
  o["addressBackground"]  = colorString(s.addressBackground);
  o["archiveAddressForeground"] = colorString(s.archiveAddressForeground);
  o["archiveAddressBackground"] = colorString(s.archiveAddressBackground);
  o["compareDifferForeground"]   = colorString(s.compareDifferForeground);
  o["compareDifferBackground"]   = colorString(s.compareDifferBackground);
  o["compareOnlyHereForeground"] = colorString(s.compareOnlyHereForeground);
  o["compareOnlyHereBackground"] = colorString(s.compareOnlyHereBackground);
  o["cursorActive"]       = colorString(s.cursorActiveColor);
  o["cursorInactive"]     = colorString(s.cursorInactiveColor);

  // ── テキストビュアー
  QJsonObject tv;
  tv["font"]         = fontToJson(s.textViewerFont);
  tv["normalFg"]     = colorString(s.textViewerNormalFg);
  tv["normalBg"]     = colorString(s.textViewerNormalBg);
  tv["selectedFg"]   = colorString(s.textViewerSelectedFg);
  tv["selectedBg"]   = colorString(s.textViewerSelectedBg);
  tv["lineNumberFg"] = colorString(s.textViewerLineNumberFg);
  tv["lineNumberBg"] = colorString(s.textViewerLineNumberBg);
  o["textViewer"] = tv;

  // ── 画像ビュアー (色のみ)
  QJsonObject iv;
  iv["solidColor"]    = colorString(s.imageViewerSolidColor);
  // checkerColor1/2 はテーマ非依存。ここでは保存しない (Settings の
  // top-level "imageViewer" ブロックに 1 セットだけ保存される)。
  o["imageViewer"] = iv;

  // ── バイナリビュアー
  QJsonObject bv;
  bv["font"]       = fontToJson(s.binaryViewerFont);
  bv["normalFg"]   = colorString(s.binaryViewerNormalFg);
  bv["normalBg"]   = colorString(s.binaryViewerNormalBg);
  bv["selectedFg"] = colorString(s.binaryViewerSelectedFg);
  bv["selectedBg"] = colorString(s.binaryViewerSelectedBg);
  bv["addressFg"]  = colorString(s.binaryViewerAddressFg);
  bv["addressBg"]  = colorString(s.binaryViewerAddressBg);
  o["binaryViewer"] = bv;

  // ── CSV / TSV ビュアー (フォントのみ)
  QJsonObject cv;
  cv["font"] = fontToJson(s.csvViewerFont);
  o["csvViewer"] = cv;

  // ── Markdown ビュアー (フォント + 本文 文字色 / 背景色 / リンク色)
  QJsonObject mv;
  mv["font"] = fontToJson(s.markdownViewerFont);
  mv["fg"]   = colorString(s.markdownViewerFg);
  mv["bg"]   = colorString(s.markdownViewerBg);
  mv["link"] = colorString(s.markdownViewerLink);
  o["markdownViewer"] = mv;

  return o;
}

void colorSchemeFromJson(const QJsonObject& o, ColorScheme& s) {
  // ベース色 + UI フォント (新規)。フィールド未存在なら fallback を維持。
  readColor(o, "baseBackground", s.baseBackground);
  readColor(o, "baseForeground", s.baseForeground);
  if (o.contains("uiFont")) s.uiFont = fontFromJson(o.value("uiFont").toObject(), s.uiFont);

  if (o.contains("listFont"))    s.listFont    = fontFromJson(o.value("listFont").toObject(),    s.listFont);
  if (o.contains("addressFont")) s.addressFont = fontFromJson(o.value("addressFont").toObject(), s.addressFont);
  if (o.contains("fileListRowHeight"))
    s.fileListRowHeight = qMax(0, o.value("fileListRowHeight").toInt(s.fileListRowHeight));

  if (o.contains("colorRules")) {
    s.colorRules.clear();
    for (const QJsonValue& v : o.value("colorRules").toArray()) {
      if (v.isObject()) s.colorRules.append(colorRuleFrom(v.toObject()));
    }
  }

  if (o.contains("categories")) {
    QJsonObject cats = o.value("categories").toObject();
    if (cats.contains("normal"))            categoriesFrom(cats.value("normal").toObject(),            s.categoryColors);
    if (cats.contains("selected"))          categoriesFrom(cats.value("selected").toObject(),          s.selectedCategoryColors);
    if (cats.contains("inactiveNormal"))    categoriesFrom(cats.value("inactiveNormal").toObject(),    s.inactiveCategoryColors);
    if (cats.contains("inactiveSelected"))  categoriesFrom(cats.value("inactiveSelected").toObject(),  s.inactiveSelectedCategoryColors);
  }

  readColor(o, "addressForeground", s.addressForeground);
  readColor(o, "addressBackground", s.addressBackground);
  readColor(o, "archiveAddressForeground", s.archiveAddressForeground);
  readColor(o, "archiveAddressBackground", s.archiveAddressBackground);
  readColor(o, "compareDifferForeground",   s.compareDifferForeground);
  readColor(o, "compareDifferBackground",   s.compareDifferBackground);
  readColor(o, "compareOnlyHereForeground", s.compareOnlyHereForeground);
  readColor(o, "compareOnlyHereBackground", s.compareOnlyHereBackground);
  readColor(o, "cursorActive",      s.cursorActiveColor);
  readColor(o, "cursorInactive",    s.cursorInactiveColor);

  if (o.contains("textViewer")) {
    QJsonObject tv = o.value("textViewer").toObject();
    if (tv.contains("font")) s.textViewerFont = fontFromJson(tv.value("font").toObject(), s.textViewerFont);
    readColor(tv, "normalFg",     s.textViewerNormalFg);
    readColor(tv, "normalBg",     s.textViewerNormalBg);
    readColor(tv, "selectedFg",   s.textViewerSelectedFg);
    readColor(tv, "selectedBg",   s.textViewerSelectedBg);
    readColor(tv, "lineNumberFg", s.textViewerLineNumberFg);
    readColor(tv, "lineNumberBg", s.textViewerLineNumberBg);
  }

  if (o.contains("imageViewer")) {
    QJsonObject iv = o.value("imageViewer").toObject();
    readColor(iv, "solidColor", s.imageViewerSolidColor);
    // checkerColor1/2 はテーマ非依存のため、ここでは読み飛ばす (旧形式の
    // themes.{light,dark}.imageViewer.checker* も後方互換のため明示的に
    // 無視する。値は Settings の top-level imageViewer 側で管理する)。
  }

  if (o.contains("binaryViewer")) {
    QJsonObject bv = o.value("binaryViewer").toObject();
    if (bv.contains("font")) s.binaryViewerFont = fontFromJson(bv.value("font").toObject(), s.binaryViewerFont);
    readColor(bv, "normalFg",   s.binaryViewerNormalFg);
    readColor(bv, "normalBg",   s.binaryViewerNormalBg);
    readColor(bv, "selectedFg", s.binaryViewerSelectedFg);
    readColor(bv, "selectedBg", s.binaryViewerSelectedBg);
    readColor(bv, "addressFg",  s.binaryViewerAddressFg);
    readColor(bv, "addressBg",  s.binaryViewerAddressBg);
  }

  if (o.contains("csvViewer")) {
    QJsonObject cv = o.value("csvViewer").toObject();
    if (cv.contains("font")) s.csvViewerFont = fontFromJson(cv.value("font").toObject(), s.csvViewerFont);
  }

  if (o.contains("markdownViewer")) {
    QJsonObject mv = o.value("markdownViewer").toObject();
    if (mv.contains("font")) s.markdownViewerFont = fontFromJson(mv.value("font").toObject(), s.markdownViewerFont);
    readColor(mv, "fg",   s.markdownViewerFg);
    readColor(mv, "bg",   s.markdownViewerBg);
    readColor(mv, "link", s.markdownViewerLink);
  }
}

} // namespace Farman
