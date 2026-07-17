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

  // 生の格納バイト列を取得し、自前でエンコーディングを判定する
  // (UTF-8 → Shift_JIS)。libarchive の _w / _utf8 変換は、UTF-8 フラグの無い
  // CP932 (Shift-JIS) の zip / lzh 名をプロセスロケール依存で誤変換して文字化け
  // させる。farman は日本語ロケールを設定していないため、特に Windows で
  // archive_entry_pathname_w() が化ける。macOS / Linux で実績のあるこの生バイト
  // 経路を全プラットフォームで使う (Windows の "C" ロケール下でも
  // archive_entry_pathname() は生バイトを返す)。
  if (const char* name = archive_entry_pathname(entry)) {
    return decodeBytesBestEffort(QByteArray(name));
  }
  if (const char* uname = archive_entry_pathname_utf8(entry)) {
    return decodeBytesBestEffort(QByteArray(uname));
  }
#ifdef Q_OS_WIN
  // 生バイトがどうしても取れないときだけワイド版にフォールバック。
  if (const wchar_t* wname = archive_entry_pathname_w(entry)) {
    return QString::fromWCharArray(wname);
  }
#endif
  return QString();
}

} // namespace Farman
