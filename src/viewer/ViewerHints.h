#pragma once

#include <QString>

class QWidget;

namespace Farman {

class ViewerShortcutMap;

// ビュアーのツールチップ / プレースホルダに載せるショートカット表記を、割り当て
// 変更に追随させるための小ヘルパ。
//
// 各ウィジェットに「どの commandId のキーを、どのテンプレート (%1 に展開) で
// 表示するか」をプロパティとして持たせ (tag)、本体からの再 push 時に refresh で
// 現在の割り当てに更新する。tag 時点では既定キーで即時適用するので、push 前でも
// 正しい初期表示になる。
namespace ViewerHints {

// w のツールチップ (placeholder=false) または placeholder (true) を、commandId の
// キーで tmpl の %1 を展開して設定する。tmpl は tr() 済みのテンプレート文字列。
// 設定時点では既定キーで即時適用し、あとで refresh() が現在割り当てで更新できる
// よう w にプロパティを保存する。
void tag(QWidget* w, const QString& commandId, const QString& tmpl,
         bool placeholder = false);

// root と全子孫のうち tag 済みウィジェットを、map の現在割り当て (無ければ既定)
// で再設定する。ビュアーの applyShortcutBindings から呼ぶ。
void refresh(QWidget* root, const ViewerShortcutMap& map);

}  // namespace ViewerHints
}  // namespace Farman
