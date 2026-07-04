#pragma once

#include "types.h"
#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QList>
#include <array>

namespace Farman {

// テーマ単位で切り替わる「色とフォントのスナップショット」。
//
// Settings はこれを Light / Dark の 2 セット保持し、`themeMode()` で
// アクティブな側 (= 現在の見た目に反映されている側) を選ぶ。
// Settings 内の m_ フィールド (m_font / m_addressForeground / ...) は
// 「アクティブスキームのワーキングコピー」として常に同期されており、
// 個別の getter/setter は m_ を介して動く。
//
// SPEC.md L1554-1559 の `Theme JSON` の `light` / `dark` ブロック内の
// フィールドはこの構造体と 1:1 に対応する。
//
// 含まれない (= テーマ非依存) 項目:
//  - cursorShape / cursorThickness — UI 要素の構造的選択
//  - useInactivePaneColors          — 機能 ON/OFF のトグル
//  - 各種 extensions / mime / encoding / unit など viewer の挙動設定
struct ColorScheme {
  // ── ベース色 (= UI 全体の地色) ─────────────
  // QPalette の Window/Base/Text/WindowText 等の派生元。
  // ここから applyThemeFields() でグラデーション計算して
  // Mid / Midlight / Dark / Shadow / Button / AlternateBase 等を自動算出する。
  // 「個別設定で覆われていない部分」(ダイアログ背景、ボタン、メニュー、
  // ツールチップ、スクロールバー、グループ枠、…) のすべてがこの 2 色から決まる。
  QColor  baseBackground;
  QColor  baseForeground;

  // ── UI フォント (汎用ウィジェット用) ──────────
  // QApplication::setFont() に流し込むフォント。これが指定されると、
  // 個別に setFont() されていない全ウィジェット (= ダイアログのラベル、
  // ボタン、メニュー、ツールチップ等) がこのフォントで描画される。
  // ファイルリスト / アドレスバー / 各ビュアーは個別の font 設定を保持する
  // ので影響を受けない。
  QFont   uiFont;

  // ── ファイルリスト ─────────────────────────
  QFont   listFont;       // ファイル一覧
  QFont   addressFont;    // アドレスバー (各ペイン上部)
  int     fileListRowHeight = 0;  // 0 = Auto

  QList<ColorRule>     colorRules;
  // (cat × selected × inactive × inactive-selected) の 4 状態 × 3 カテゴリ
  std::array<CategoryColor, static_cast<size_t>(FileCategory::Count)>
      categoryColors,
      selectedCategoryColors,
      inactiveCategoryColors,
      inactiveSelectedCategoryColors;

  // アドレスバー / カーソル
  QColor  addressForeground;
  QColor  addressBackground;
  // アーカイブ内ブラウジング中のアドレスバー色 (通常 FS と視覚的に区別する)
  QColor  archiveAddressForeground;
  QColor  archiveAddressBackground;
  QColor  cursorActiveColor;
  QColor  cursorInactiveColor;

  // ── ディレクトリ比較 ───────────────────────
  // Differ:   両側にあるが内容が違う行
  // OnlyHere: このペインにしかない行
  // Same は専用色を持たず、通常のカテゴリ色をそのまま使う
  QColor  compareDifferForeground;
  QColor  compareDifferBackground;
  QColor  compareOnlyHereForeground;
  QColor  compareOnlyHereBackground;

  // ── テキストビュアー ───────────────────────
  QFont   textViewerFont;
  QColor  textViewerNormalFg;
  QColor  textViewerNormalBg;
  QColor  textViewerSelectedFg;
  QColor  textViewerSelectedBg;
  QColor  textViewerLineNumberFg;
  QColor  textViewerLineNumberBg;

  // ── 画像ビュアー (透明部分の色) ────────────
  // Solid 色のみテーマ依存。Checker 柄は「透明部分のインジケータ」として
  // テーマに依存しない固定値を使うため、ここには持たない (Settings の
  // フラットフィールド m_imageViewerCheckerColor1/2 に直接保存される)。
  QColor  imageViewerSolidColor;

  // ── バイナリビュアー ───────────────────────
  QFont   binaryViewerFont;
  QColor  binaryViewerNormalFg;
  QColor  binaryViewerNormalBg;
  QColor  binaryViewerSelectedFg;
  QColor  binaryViewerSelectedBg;
  QColor  binaryViewerAddressFg;
  QColor  binaryViewerAddressBg;

  // ── CSV / TSV ビュアー ──
  // フォントのみテーマ依存 (文字色設定は持たない)。
  QFont   csvViewerFont;

  // ── Markdown ビュアー ──
  // フォント + 本文の文字色 / 背景色 / リンク色 (テーマ依存)。色が無効色の
  // ときはパレット既定色 (= テーマ追従) で描画する。
  QFont   markdownViewerFont;
  QColor  markdownViewerFg;
  QColor  markdownViewerBg;
  QColor  markdownViewerLink;
};

// 出荷時の Light テーマ (= これまでの farman の既定値そのまま)
ColorScheme defaultLightScheme();
// 出荷時の Dark テーマ (Light を反転させたシンプルなダーク)
ColorScheme defaultDarkScheme();

// ── JSON シリアライズ ────────────────────────────
// 設定ファイル `themes.{light,dark}` ブロックや、テーマプリセット
// (`*.json`) のインポート / エクスポートで使用する。
// `from` は欠落キーがあれば `fallback` の値を維持する non-strict ロード。
QJsonObject colorSchemeToJson(const ColorScheme& s);
void        colorSchemeFromJson(const QJsonObject& obj, ColorScheme& fallback);

} // namespace Farman
