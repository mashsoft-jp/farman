#pragma once

// プラグインの設定ページ (IPluginSettingsPage) で、テーマ (ColorScheme) 依存の
// フォント / 配色を編集するための小さな表示ヘルパ。色ボタン / フォントボタンの
// 外観を現在値に合わせて更新するだけで、各公式ビュアーの設定ページが共有する。
//
// 定義は ViewerThemeFields.cpp にある (tr() 文字列を lupdate に拾わせるため、
// inline ヘッダではなく .cpp に置く)。プラグイン dylib / 本体それぞれが
// ViewerThemeFields.cpp を自前でコンパイルする。

#include <QColor>
#include <QFont>
#include <QString>

class QPushButton;

namespace Farman {

// 色ボタンの外観を現在値に合わせて更新する。
//  - c が有効色     : 背景をその色で塗り、ラベルに RGB (#RRGGBB) を表示。
//  - c が無効 (未設定): fallback (= その色を空欄にしたときに実際に使われる
//                       テーマ既定色) で背景を塗り、ラベルは "(none)"。
//                       fallback も無効なら無地 + "(none)"。
void styleThemeColorButton(QPushButton* btn, const QColor& c,
                           const QColor& fallback = QColor());

// フォントボタン等に出すファミリー表示名を返す。macOS のシステムフォント
// (".AppleSystemUIFont" 等、先頭がドット) は QFontDatabase の選択肢に出ない
// 隠しフォントで、生の内部名だと分かりづらいため "System Font" と表示する。
QString fontFamilyLabel(const QFont& f);

// フォントボタンのラベルを "Family, Npt" に更新する。
void styleThemeFontButton(QPushButton* btn, const QFont& f);

} // namespace Farman
