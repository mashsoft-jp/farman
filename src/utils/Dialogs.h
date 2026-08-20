#pragma once

#include <QList>
#include <QString>
#include <Qt>

class QWidget;
class QPushButton;

namespace Farman {

// Yes / No 2 択の確認ダイアログをキーボードのみで完結できるヘルパ。
// - `Y` / `N` キー: 即応答（macOS のキーボードナビゲーション設定に依存しない）
// - `Enter`: デフォルトボタン（defaultYes=true なら Yes、false なら No）
// - `Escape`: No
// 戻り値: Yes を選んだなら true、No / キャンセルなら false。
bool confirm(QWidget* parent,
             const QString& title,
             const QString& text,
             bool defaultYes = false);

// 情報 / 警告 / エラーの OK 単独ダイアログ。
// QMessageBox::information / warning / critical の置き換え。
// 標準 QMessageBox は macOS のキーボードナビゲーション設定が OFF だと Tab
// フォーカス移動や Alt+O が効かないため、独自ダイアログにする。
// - 左にアイコン (Information / Warning / Critical で色分け)、右にメッセージ
// - OK ボタン (Alt+O) のみ
// - `Enter`: OK / `Escape`: OK
void inform  (QWidget* parent, const QString& title, const QString& text);
void warn    (QWidget* parent, const QString& title, const QString& text);
void critical(QWidget* parent, const QString& title, const QString& text);

// 任意個数のボタンを並べる選択ダイアログ。
// 動的に addButton() で組み立てていた QMessageBox の置き換え。
// - icon: アイコン種別 (左に表示)。None ならアイコン無し
// - buttons: 並べるボタン。altKey に Qt::Key を指定すれば Alt+key で発火
// - defaultIndex: Enter で発火するボタンの index
// - cancelIndex: Esc で発火させたいボタンの index (-1 なら閉じ動作のみ)
// 戻り値: 押されたボタンの 0-based index。Esc キャンセル時は cancelIndex。
enum class DialogIcon { None, Information, Question, Warning, Critical };

struct DialogButton {
  QString label;                    // 表示テキスト (例: "Copy")
  Qt::Key altKey = Qt::Key(0);      // Alt+altKey で発火。0 = ショートカット無し
};

int choose(QWidget* parent,
           const QString& title,
           const QString& text,
           const QList<DialogButton>& buttons,
           int defaultIndex = 0,
           int cancelIndex = -1,
           DialogIcon icon = DialogIcon::Question);

// ラベル文字列に Alt+key ショートカットの視覚的ヒントを埋め込む。
// 戻り値はそのまま QLabel / QRadioButton / QPushButton の text に使える。
//   Windows / Linux: 該当文字の前に '&' を挿入 (例: "&Foo")。
//                    Qt が該当文字をアンダーラインで描画し、QPushButton や
//                    QRadioButton では Alt+key で自動アクティベートされる。
//                    該当字が無ければ末尾 "(&F)" 形式にフォールバック
//                    (例: 日本語訳 "コピー" + Qt::Key_C → "コピー (&C)")。
//   macOS:           HIG により '&' mnemonic は表示されないため、末尾に
//                    "(⌥X)" 形式で添える (例: "Foo (⌥F)")。
// 入力中の '&' は一旦除去するため、繰り返し呼び出しても二重付加しない。
QString withAltMnemonic(const QString& text, Qt::Key key);

// ボタンにショートカットキー (Alt+key) を設定し、表記を withAltMnemonic で
// 整える。Tab でフォーカスを当てられるよう focusPolicy も StrongFocus に
// 明示する。再呼び出し安全。
void applyAltShortcut(QPushButton* btn, Qt::Key key);

// 情報通知ダイアログ + 「次回以降表示しない」チェックを 1 つ持つ汎用ヘルパ。
// initiallyShow = true で開かれた直後はチェック OFF。ユーザーが OK を押した
// 時点での「次回も表示するか」 (= チェックの逆) を戻り値で返す。
//   bool keep = informWithSuppress(parent, title, message, true);
//   if (!keep) Settings::instance().setXxxShow(false);
// initiallyShow = false で呼んでも普通に表示するが、その場合チェックは
// 既定で ON (表示しない側) になる。
bool informWithSuppress(QWidget* parent,
                        const QString& title,
                        const QString& message,
                        bool initiallyShow);

// テキスト入力ダイアログ（QInputDialog の置き換え）。
// OK/Cancel にショートカット（Alt+O/Alt+X）を設定し、Tab 順は
// 入力欄 → Cancel → OK（実行系が最後）。
// OK で accepted のとき ok=true、戻り値に入力文字列。
//
// CursorPolicy: ダイアログを開いた直後の入力欄カーソル位置／選択状態。
//  - SelectAll: 既定。defaultValue 全体を選択 (新規入力で上書きしやすい)
//  - BeforeExtension: 拡張子の手前 = 最後の '.' の直前にカーソル、
//      かつそこまでのベース名を選択状態にする。
//      ("foo.tar.gz" なら "foo.tar" を選択、カーソルは最後の '.' の直前)
//      ファイルのリネーム向け。先頭が '.' のドットファイル ('.gitignore' 等) は
//      拡張子として扱わないので全選択になる。
//      ディレクトリ名は拡張子に意味が無いので、呼び出し側が SelectAll を渡す。
enum class TextInputCursor {
  SelectAll,
  BeforeExtension,
};

QString inputText(QWidget* parent,
                  const QString& title,
                  const QString& label,
                  const QString& defaultValue = QString(),
                  bool* ok = nullptr,
                  TextInputCursor cursor = TextInputCursor::SelectAll);

} // namespace Farman
