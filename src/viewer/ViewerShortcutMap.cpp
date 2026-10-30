#include "ViewerShortcutMap.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QStringList>

namespace Farman {

void ViewerShortcutMap::applyBindings(const QVariantMap& bindings) {
  m_reverse.clear();
  m_forward.clear();
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
      // 逆引き (照合用): 同一キーの重複は先勝ち（本体 UI 側で競合を検出して防ぐ）。
      if (!m_reverse.contains(seq)) {
        m_reverse.insert(seq, commandId);
      }
      // 正引き (hints 用): commandId ごとに割り当て順で保持。
      m_forward[commandId].append(seq);
    }
  }
}

QString ViewerShortcutMap::primaryKeyText(const QString& commandId,
                                          QKeySequence::SequenceFormat format) const {
  const auto it = m_forward.constFind(commandId);
  if (it == m_forward.constEnd() || it.value().isEmpty()) {
    return QString();
  }
  return it.value().first().toString(format);
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

bool ViewerShortcutMap::isTypingIntoTextField(const QKeyEvent* ke) {
  if (!ke || (ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
    return false;
  }
  // F3 / Enter などの非文字キーは対象外 (文字として入力されるキーだけ手放す)。
  const QString text = ke->text();
  if (text.isEmpty() || !text.at(0).isPrint()) {
    return false;
  }
  QWidget* w = QApplication::focusWidget();
  if (!w) {
    return false;
  }
  if (qobject_cast<QLineEdit*>(w) || qobject_cast<QAbstractSpinBox*>(w)) {
    return true;
  }
  if (auto* te = qobject_cast<QTextEdit*>(w)) {
    return !te->isReadOnly();
  }
  if (auto* pe = qobject_cast<QPlainTextEdit*>(w)) {
    return !pe->isReadOnly();
  }
  return false;
}

} // namespace Farman
