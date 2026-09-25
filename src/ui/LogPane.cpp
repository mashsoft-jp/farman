#include "LogPane.h"
#include "core/Logger.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QScrollBar>

namespace Farman {

LogPane::LogPane(QWidget* parent)
  : QPlainTextEdit(parent)
{
  setReadOnly(true);
  setLineWrapMode(QPlainTextEdit::NoWrap);
  setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  setMaximumBlockCount(2000);

  // 高さは Settings の logPaneHeight を FileManagerPanel 側で setFixedHeight する。
  // 最低限 1 行ぶんは確保しておく。
  setMinimumHeight(fontMetrics().lineSpacing());

  // 既存の履歴を流し込む
  for (const QString& line : Logger::instance().recent()) {
    appendPlainText(line);
  }

  connect(&Logger::instance(), &Logger::entryAppended, this,
          [this](const QString& formatted) {
    appendPlainText(formatted);
    if (auto* sb = verticalScrollBar()) sb->setValue(sb->maximum());
  });
}

bool LogPane::event(QEvent* event) {
  // 読み取り専用の QPlainTextEdit は、コピー / すべて選択のキーでも
  // ShortcutOverride を受け取らない。そのままだとパネル側のショートカット
  // (Cmd+C = パスのコピー、Cmd+A = ファイルのすべて選択) が先に発火するので、
  // フォーカスがあるときはここで受け取って keyPressEvent に回す。
  if (event->type() == QEvent::ShortcutOverride) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->matches(QKeySequence::Copy) ||
        keyEvent->matches(QKeySequence::SelectAll)) {
      event->accept();
      return true;
    }
  }
  return QPlainTextEdit::event(event);
}

void LogPane::keyPressEvent(QKeyEvent* event) {
  if (event->matches(QKeySequence::Copy)) {
    copy();  // 選択が無ければ何もしない
    event->accept();
    return;
  }
  if (event->matches(QKeySequence::SelectAll)) {
    selectAll();
    event->accept();
    return;
  }
  QPlainTextEdit::keyPressEvent(event);
}

} // namespace Farman
