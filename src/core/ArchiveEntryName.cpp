#include "ArchiveEntryName.h"

#include "utils/MediaMatchers.h"

#include <QByteArray>
#include <QStringDecoder>
#include <QTextCodec>
#include <QtGlobal>

#include <archive_entry.h>

namespace Farman {

namespace {

// 形式ごとの文字コードルール。setFilenameEncodingRules() で差し替える。
QList<FilenameEncodingRule>& encodingRules() {
  static QList<FilenameEncodingRule> rules;
  return rules;
}

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

// ユーザーが形式に指定した文字コードで復号する。コーデックを引けなければ
// null QString を返し、呼び出し側が自動判別へ倒す。
QString decodeBytesWith(const QByteArray& raw, const QString& encoding) {
  if (encoding.compare(QLatin1String("UTF-8"), Qt::CaseInsensitive) == 0) {
    return QString::fromUtf8(raw);
  }
  if (QTextCodec* codec = QTextCodec::codecForName(encoding.toLatin1())) {
    return codec->toUnicode(raw);
  }
  return QString();
}

// 指定があればその文字コードで、無ければ自動判別で復号する。
QString decodeBytes(const QByteArray& raw, const QString& encoding) {
  if (!encoding.isEmpty()) {
    const QString decoded = decodeBytesWith(raw, encoding);
    if (!decoded.isNull()) return decoded;
  }
  return decodeBytesBestEffort(raw);
}

} // namespace

void setFilenameEncodingRules(const QList<FilenameEncodingRule>& rules) {
  encodingRules() = rules;
}

QString filenameEncodingFor(const QString& archiveFileName) {
  if (archiveFileName.isEmpty()) return QString();
  for (const FilenameEncodingRule& rule : encodingRules()) {
    // 自動判別の形式はルールとして持たない (空 = 指定なし)。
    if (rule.encoding.isEmpty()) continue;
    if (MediaMatchers::fileNameMatches(rule.patterns, archiveFileName)) {
      return rule.encoding;
    }
  }
  return QString();
}

QString decodeArchiveEntryName(struct archive_entry* entry,
                               const QString& encoding) {
  if (!entry) return QString();

  // 生の格納バイト列を取得し、自前でエンコーディングを判定する
  // (UTF-8 → Shift_JIS)。libarchive の _w / _utf8 変換は、UTF-8 フラグの無い
  // CP932 (Shift-JIS) の zip / lzh 名をプロセスロケール依存で誤変換して文字化け
  // させる。farman は日本語ロケールを設定していないため、特に Windows で
  // archive_entry_pathname_w() が化ける。macOS / Linux で実績のあるこの生バイト
  // 経路を全プラットフォームで使う (Windows の "C" ロケール下でも
  // archive_entry_pathname() は生バイトを返す)。
  if (const char* name = archive_entry_pathname(entry)) {
    return decodeBytes(QByteArray(name), encoding);
  }
  if (const char* uname = archive_entry_pathname_utf8(entry)) {
    return decodeBytes(QByteArray(uname), encoding);
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
