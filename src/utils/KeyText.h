#pragma once

#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>

namespace Farman {

// キーバインドの表示用テキスト (キーバインド一覧 / TIPS で共用)。
inline QString keysToText(const QList<QKeySequence>& keys) {
  // 複数バインドがあるときは "C, Ctrl+C" のようにカンマ区切り。
  // ネイティブ表記を使うので macOS では ⌘C / Win/Linux では Ctrl+C 表示。
  // テンキー (Numpad) の Enter は Key_Return とペアで登録するのが慣例
  // (例: file.execute / view.choose) で表示が冗長になるので、一覧からは
  // 除外する。バインド自体は有効。
  QStringList parts;
  for (const auto& k : keys) {
    bool isNumpadEnter = false;
    for (int i = 0; i < k.count(); ++i) {
      if (k[i].key() == Qt::Key_Enter) { isNumpadEnter = true; break; }
    }
    if (isNumpadEnter) continue;

    QString s = k.toString(QKeySequence::NativeText);
    // macOS の NativeText は Home/End/PageUp/PageDown を斜め矢印で出して
    // 区別しづらいので、わかりやすい文字列に置換する。
    s.replace(QChar(0x2196), QStringLiteral("Home"));   // ↖
    s.replace(QChar(0x2198), QStringLiteral("End"));    // ↘
    s.replace(QChar(0x21DE), QStringLiteral("PgUp"));   // ⇞
    s.replace(QChar(0x21DF), QStringLiteral("PgDn"));   // ⇟

    if (!s.isEmpty()) parts << s;
  }
  return parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral(", "));
}

} // namespace Farman
