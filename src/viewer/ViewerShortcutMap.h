#pragma once

#include <QMap>
#include <QString>
#include <QKeySequence>
#include <QVariantMap>

class QKeyEvent;

namespace Farman {

// 1 ビュアー分のショートカット割当を「ローカルに」保持する軽量ホルダ。
//
// データフローは一方通行（本体→ビュアー）: ビュアーはストレージを一切読まず、
// 本体から applyBindings() で push された割当だけを保持して、キー入力の照合に使う。
// 本体（farman）が保存・所有し、開くタイミングと変更時に各ビュアーへ渡す。
class ViewerShortcutMap {
public:
  // 本体から渡す割当。bindings: commandId(QString) -> キー文字列のリスト(QStringList、
  // QKeySequence::toString() 形式)。呼ぶたびに総入れ替えする。
  void applyBindings(const QVariantMap& bindings);

  // seq に割り当てられたコマンド ID（無ければ空文字）。
  QString commandForSeq(const QKeySequence& seq) const;

  // キーイベントから照合用の QKeySequence を作る（修飾キー単独は空、Keypad 除去）。
  static QKeySequence sequenceForEvent(const QKeyEvent* ke);

private:
  // key -> commandId（QKeySequence は operator< を持つので QMap）
  QMap<QKeySequence, QString> m_reverse;
};

} // namespace Farman
