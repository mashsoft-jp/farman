#pragma once

#include <QObject>
#include <QString>

class QWidget;

namespace Farman {

// メインツールバー / 各ビュアーのツールバーで使う共通スタイルシート。
// 1 つの文字列に「フォーカス枠」「checkable トグルの押下状態」「ホバー」を
// 全部入れて、ツールバー単位で setStyleSheet する。
//
// 個別ウィジェットに stylesheet を設定すると Qt は native スタイル描画から
// 切り替わるので、:checked の押下状態も明示しないと「ON にしても見た目が
// 変わらない」状態になる。ここで一括カバーする。
inline QString toolbarStyleSheet() {
  return QStringLiteral(
    "QToolButton { padding: 3px; border: 1px solid transparent; border-radius: 3px; }"
    "QToolButton:hover { background-color: palette(midlight); }"
    "QToolButton:checked { background-color: palette(mid); }"
    "QToolButton:checked:hover { background-color: palette(mid); }"
    "QToolButton:focus { border: 2px solid palette(highlight); padding: 2px; }"
    "QToolButton:checked:focus { background-color: palette(mid); border: 2px solid palette(highlight); padding: 2px; }"
  );
}

// QTableView / QTreeView / QListView (および QTableWidget 等の Widget 系派生)
// に setStyleSheet() するための、「フォーカスを失った時の選択行ハイライトを
// 弱める」スタイル文字列。
//
// 既定の Qt スタイルだとフォーカスが他に移っても palette(highlight) (= 青) で
// 選択行を強調表示し続けるので、「あれ、こっちのリストにフォーカスがあるのかな」
// と紛らわしくなる。Mac の NSTableView などは非アクティブ時はグレーになるが、
// それを Qt のクロスプラットフォーム上で再現する形。
//
// 既存の setStyleSheet と合わせて使うときは、両方の文字列を連結して渡すこと。
inline QString inactiveSelectionStyleSheet() {
  return QStringLiteral(
    "QAbstractItemView::item:selected:!active {"
    "  background: palette(midlight);"
    "  color: palette(text);"
    "}"
  );
}

// ツールバー上のフォーカス可能ウィジェットで Enter / Return キーが押された
// ときに、親 (= ファイラ / ビュアーウィンドウ) へバブルさせず、
// そのウィジェット自身のアクションとして処理する QObject フィルタ。
//
//   - QAbstractButton (QToolButton / QPushButton 等):
//       click() を呼んでクリック扱い (= checkable ならトグル、通常なら
//       triggered 発火)。
//   - 編集不可 (read-only) の QComboBox:
//       showPopup() でドロップダウンを開く。何もしないより素直な挙動。
//   - 編集可能な QComboBox:
//       インストールしない (= 内部 QLineEdit が Enter を確定として処理する)。
//
// ツールバー (メイン / ビュアー) で Tab → Enter の素直な操作を提供しつつ、
// 「Enter が親まで届いて誤って "ファイルを開く" 等が動く」のを防ぐ目的。
//
// 使い方:
//   auto* f = new EnterClickFilter(parent);
//   f->installOnButtonsIn(toolbarWidget);  // QAbstractButton + 非 editable Combo
//
// install したフィルタは parent のライフタイムに従う。後から動的に追加された
// ボタンには別途 installOnButtonsIn を呼ぶこと。
class EnterClickFilter : public QObject {
  Q_OBJECT

public:
  explicit EnterClickFilter(QObject* parent = nullptr);

  // root 配下にある対象ウィジェット (QAbstractButton 全般 + 編集不可な
  // QComboBox) すべてに本フィルタを installEventFilter する。root 自身が
  // 対象の場合も含める。
  void installOnButtonsIn(QWidget* root);

protected:
  bool eventFilter(QObject* obj, QEvent* ev) override;
};

} // namespace Farman
