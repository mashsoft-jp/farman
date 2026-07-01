#include "ArchiveEntryName.h"

#include <QByteArray>
#include <QStringDecoder>
#include <QTextCodec>
#include <QtGlobal>

#include <archive_entry.h>

namespace Farman {

namespace {

// 生バイト列を「UTF-8 として妥当ならそのまま、そうでなければ Shift_JIS」で
// 復号する。Shift_JIS でも解釈できなければ最後にロケール依存で復号する。
QString decodeBytesBestEffort(const QByteArray& raw) {
  if (raw.isEmpty()) return QString();

  // 1) 妥当な UTF-8 か? (UTF-8 フラグ付き zip / tar / 現代的なアーカイブ)
  //    Stateless で末尾の途切れも不正扱いにして厳密に判定する。
  {
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString s = utf8.decode(raw);
    if (!utf8.hasError()) return s;
  }

  // 2) 非 UTF-8 = レガシーコードページ。日本語アプリの既定として Shift_JIS。
  if (QTextCodec* codec = QTextCodec::codecForName("Shift_JIS")) {
    return codec->toUnicode(raw);
  }

  // 3) 最後の砦。
  return QString::fromLocal8Bit(raw);
}

} // namespace

QString decodeArchiveEntryName(struct archive_entry* entry) {
  if (!entry) return QString();

#ifdef Q_OS_WIN
  // Windows では libarchive がワイド文字 (ACP 変換済み) を返せることが多い。
  // 取れたらそれを優先する。取れなければ MBS 経路へフォールバック。
  if (const wchar_t* wname = archive_entry_pathname_w(entry)) {
    const QString s = QString::fromWCharArray(wname);
    if (!s.isEmpty()) return s;
  }
#endif

  const char* name = archive_entry_pathname(entry);
  if (!name) {
    name = archive_entry_pathname_utf8(entry);
  }
  if (!name) return QString();
  return decodeBytesBestEffort(QByteArray(name));
}

} // namespace Farman
