#include "ViewerShortcutMap.h"

#include <QKeyEvent>
#include <QStringList>

namespace Farman {

void ViewerShortcutMap::applyBindings(const QVariantMap& bindings) {
  m_reverse.clear();
  for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
    const QString commandId = it.key();
    const QStringList keys = it.value().toStringList();
    for (const QString& s : keys) {
      const QString t = s.trimmed();
      if (t.isEmpty()) {
        continue;
      }
      const QKeySequence seq(t);
      if (seq.isEmpty()) {
        continue;
      }
      // 同一キーの重複は先勝ち（本体 UI 側で競合を検出して防ぐ）。
      if (!m_reverse.contains(seq)) {
        m_reverse.insert(seq, commandId);
      }
    }
  }
}

QString ViewerShortcutMap::commandForSeq(const QKeySequence& seq) const {
  if (seq.isEmpty()) {
    return QString();
  }
  return m_reverse.value(seq);
}

QKeySequence ViewerShortcutMap::sequenceForEvent(const QKeyEvent* ke) {
  if (!ke) {
    return QKeySequence();
  }
  const int key = ke->key();
  if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt
      || key == Qt::Key_Meta || key == 0) {
    return QKeySequence();
  }
  Qt::KeyboardModifiers mods = ke->modifiers();
  mods &= ~Qt::KeypadModifier;
  return QKeySequence(QKeyCombination(mods, static_cast<Qt::Key>(key)));
}

} // namespace Farman
