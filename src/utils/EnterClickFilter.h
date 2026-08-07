#pragma once

#include "settings/Settings.h"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QObject>
#include <QPalette>
#include <QString>
#include <QToolBar>

class QWidget;

namespace Farman {

// メインツールバー / 各ビュアーのツールバーで使う共通スタイルシート。
// 1 つの文字列に「フォーカス枠」「checkable トグルの押下状態」「ホバー」を
// 全部入れて、ツールバー単位で setStyleSheet する。
//
// 個別ウィジェットに stylesheet を設定すると Qt は native スタイル描画から
// 切り替わるので、:checked の押下状態も明示しないと「ON にしても見た目が
// 変わらない」状態になる。ここで一括カバーする。
// hPad: ツールバー左右の内側余白 (px)。ビュアーのツールバーは端の
// ラベル/ボタンが窓の縁に貼り付かないよう余白を付ける。本体ツールバーは 0。
inline QString toolbarStyleSheet(int hPad = 8) {
  // QSS の palette() は setStyleSheet 時点の色で固定されテーマ変更に追従しない
  // (Light↔Dark 切替で色が残る)。そこで現在のパレットから実際の色を計算して
  // 埋め込み、テーマ変更時は applyToolbarStyle() が再適用して計算し直す。
  const QPalette pal = qApp ? qApp->palette() : QPalette();
  const QColor win = pal.color(QPalette::Active, QPalette::Window);
  const bool dark = win.lightness() < 128;
  // ホバー / 押下 (checked) の背景。ライトは少し暗く、ダークは少し明るくして、
  // どちらのテーマでも「控えめだが分かる」ハイライトにする (押下がライトで
  // 暗くなり過ぎないよう darker は弱め)。
  const QColor hover        = dark ? win.lighter(140) : win.darker(106);
  const QColor checked      = dark ? win.lighter(175) : win.darker(112);
  const QColor checkedHover = dark ? win.lighter(195) : win.darker(117);
  const QColor focusBorder  = pal.color(QPalette::Active, QPalette::Highlight);

  QString s;
  // QToolBar 背景を明示してスタイルシート描画 (フラット) にする。これをやらないと
  // macOS はネイティブツールバーが押下 (checked) 状態を暗く描いてスタイルシートを
  // 上書きし、Linux は QToolBar 背景をデスクトップテーマで描いてダーク配色が
  // 反映されない。全 OS で背景を敷くことで、押下色やテーマ追従を一貫させる。
  const QString pad = hPad > 0
    ? QStringLiteral(" padding-left: %1px; padding-right: %1px;").arg(hPad)
    : QString();
  s += QStringLiteral("QToolBar { background-color: %1; border: 0px;%2 }")
         .arg(win.name(), pad);
  s += QStringLiteral(
    "QToolButton { padding: 3px; border: 1px solid transparent; border-radius: 3px; }"
    "QToolButton:hover { background-color: %1; }"
    "QToolButton:checked { background-color: %2; }"
    "QToolButton:checked:hover { background-color: %3; }"
    "QToolButton:focus { border: 2px solid %4; padding: 2px; }"
    "QToolButton:checked:focus { background-color: %2; border: 2px solid %4; padding: 2px; }"
  ).arg(hover.name(), checked.name(), checkedHover.name(), focusBorder.name());
  return s;
}

// ツールバーに共通スタイルを適用し、テーマ変更に追従させる。
// setStyleSheet(toolbarStyleSheet()) の代わりにこれを使う。
//
// toolbarStyleSheet() は現在のパレットから実際の色を計算して埋め込むが、QSS の
// 値はテーマ変更で自動更新されない。そこで farman のテーマ / 設定変更通知
// (Settings::settingsChanged) を受けて計算し直して再適用する。settingsChanged は
// applyThemeFields (qApp->setPalette) の後に emit されるので、この時点で
// qApp->palette() は既に新テーマ = 正しい色になっている。tb を context にして
// いるので、ツールバー破棄時に接続は自動解除される。
inline void applyToolbarStyle(QToolBar* tb, int hPad = 8) {
  if (!tb) return;
  tb->setStyleSheet(toolbarStyleSheet(hPad));
  QObject::connect(&Settings::instance(), &Settings::settingsChanged, tb, [tb, hPad]() {
    tb->setStyleSheet(toolbarStyleSheet(hPad));
  });
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
