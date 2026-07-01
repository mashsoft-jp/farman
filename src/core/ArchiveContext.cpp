#include "ArchiveContext.h"
#include "ArchiveEntryName.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <archive.h>
#include <archive_entry.h>

namespace Farman {

namespace {

#ifdef Q_OS_WIN
inline const wchar_t* asWChar(const QString& s) {
  return reinterpret_cast<const wchar_t*>(s.utf16());
}
#endif

// libarchive のエントリパス (アーカイブの内部表現) を取得して
// 「先頭 '/' なし、末尾 '/' なし」のキー文字列に整える。
QString readEntryPath(struct archive_entry* entry) {
  // UTF-8 フラグ無しの CP932 (Shift-JIS) zip 等でも文字化けしないよう、
  // 共通ヘルパでエンコーディングを判定して QString 化する。
  QString path = decodeArchiveEntryName(entry);
  // 末尾 '/' (ディレクトリ表記) を落とす
  while (path.size() > 0 && path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }
  // 先頭 '/' (絶対パス入りエントリ) を落とす
  while (path.size() > 0 && path.startsWith(QLatin1Char('/'))) {
    path.remove(0, 1);
  }
  return path;
}

// Zip Slip 兆候のあるエントリ名を弾く。
// 一覧表示用 (load) でも extractEntryTo でも、`..` や NUL を含むパスはモデル
// に取り込まない / 抽出対象にしないという防御線を引いておく。
// Windows のバックスラッシュ (`..\evil.txt`) も `/` に正規化してから検査
// するので、libarchive がバックスラッシュ付きのエントリ名を返してくる
// ケースでも取りこぼさない。
bool isSafeArchiveEntryName(const QString& path) {
  if (path.isEmpty()) return false;
  if (path.contains(QChar(0))) return false;
  // `\` を `/` に揃えてから `..` セグメントを検査する
  const QString p = QDir::fromNativeSeparators(path);
  // `..` が独立セグメントとして含まれる場合のみ拒否 ("foo..bar" は許可)
  if (p == QStringLiteral("..")) return false;
  if (p.startsWith(QStringLiteral("../"))) return false;
  if (p.endsWith(QStringLiteral("/.."))) return false;
  if (p.contains(QStringLiteral("/../"))) return false;
  return true;
}

} // namespace

QString ArchiveContext::normalizeDirKey(const QString& innerDir) {
  if (innerDir.isEmpty() || innerDir == QStringLiteral("/")) return {};
  QString k = innerDir;
  while (k.startsWith(QLatin1Char('/'))) k.remove(0, 1);
  while (k.endsWith(QLatin1Char('/')))   k.chop(1);
  return k;
}

std::shared_ptr<ArchiveContext> ArchiveContext::load(
  const QString&         archivePath,
  QString*               errorOut,
  std::atomic<bool>*     cancelFlag,
  std::atomic<int>*      entriesRead) {
  auto ctx = std::make_shared<ArchiveContext>();
  ctx->archivePath  = archivePath;
  ctx->archiveMtime = QFileInfo(archivePath).lastModified();

  struct archive* a = archive_read_new();
  archive_read_support_format_all(a);
  archive_read_support_filter_all(a);

#ifdef Q_OS_WIN
  const int openResult = archive_read_open_filename_w(a, asWChar(archivePath), 64 * 1024);
#else
  const int openResult = archive_read_open_filename(a, archivePath.toUtf8().constData(), 64 * 1024);
#endif
  if (openResult != ARCHIVE_OK) {
    if (errorOut) {
      *errorOut = QObject::tr("Failed to open archive: %1")
                    .arg(QString::fromUtf8(archive_error_string(a)));
    }
    archive_read_free(a);
    return nullptr;
  }

  // パスワード付きアーカイブの早期検出 (zip 中央ディレクトリのフラグから取れる)。
  // ZIP の暗号化はデータ部分にのみかかるので、エントリ "一覧" は password 無し
  // で取得できる。後で実データを extractEntryTo するときに ctx->password を
  // 使って復号する。
  if (archive_read_has_encrypted_entries(a) > 0) {
    ctx->hasEncryptedEntries = true;
  }

  struct archive_entry* entry = nullptr;
  while (true) {
    // キャンセル要求のチェック (大きいアーカイブの中断用)
    if (cancelFlag && cancelFlag->load()) {
      if (errorOut) *errorOut = QObject::tr("Archive load cancelled.");
      archive_read_close(a);
      archive_read_free(a);
      return nullptr;
    }
    const int r = archive_read_next_header(a, &entry);
    if (r == ARCHIVE_EOF) break;
    if (r < ARCHIVE_WARN) {
      // 致命エラー: 部分構築された ctx を捨てて呼び出し側にエラーを返す。
      // ここで break して ctx を返すと「途中まで読めた壊れたアーカイブ」が
      // 正常な一覧として見えてしまう。
      if (errorOut) {
        *errorOut = QObject::tr("Archive read error: %1")
                      .arg(QString::fromUtf8(archive_error_string(a)));
      }
      archive_read_close(a);
      archive_read_free(a);
      return nullptr;
    }
    if (r < ARCHIVE_OK)  continue; // 警告は黙って流す

    // パスワード付き検出 (フォーマットが直接答えない tar 等のフォールバック)。
    // hasEncryptedEntries を遅延設定するだけで、リスト構築は継続する。
    if (archive_entry_is_encrypted(entry)) {
      ctx->hasEncryptedEntries = true;
    }

    const QString path = readEntryPath(entry);
    if (path.isEmpty()) continue;  // ルート自身などはスキップ
    // Zip Slip 防衛: `..` / NUL を含むエントリはモデルに取り込まない。
    // (展開時点でもチェックするが、一覧表示・コピー先パス組み立ての段階でも
    //  弾いておくことで防御を二重にする)。
    if (!isSafeArchiveEntryName(path)) continue;

    ArchiveEntry e;
    e.pathInArchive  = path;
    const int slash  = path.lastIndexOf(QLatin1Char('/'));
    e.name           = (slash < 0) ? path : path.mid(slash + 1);
    e.isDir          = (archive_entry_filetype(entry) == AE_IFDIR);
    e.size           = e.isDir ? -1 : static_cast<qint64>(archive_entry_size(entry));
    e.compressedSize = -1;  // libarchive は per-entry の圧縮後サイズを安定に提供しない
    const time_t mt  = archive_entry_mtime(entry);
    e.mtime          = (mt > 0) ? QDateTime::fromSecsSinceEpoch(mt) : QDateTime();
#ifdef Q_OS_WIN
    if (const wchar_t* wlink = archive_entry_symlink_w(entry)) {
      e.linkTarget = QString::fromWCharArray(wlink);
    } else if (const char* link = archive_entry_symlink_utf8(entry)) {
      e.linkTarget = QString::fromUtf8(link);
    }
#else
    if (const char* link = archive_entry_symlink_utf8(entry)) {
      e.linkTarget = QString::fromUtf8(link);
    } else if (const char* link2 = archive_entry_symlink(entry)) {
      e.linkTarget = QString::fromUtf8(link2);
    }
#endif

    ctx->entries.insert(path, e);
    if (entriesRead) entriesRead->fetch_add(1, std::memory_order_relaxed);

    // 親ディレクトリのエントリが明示されていないアーカイブのために、
    // 親パスから順次「合成ディレクトリ」エントリを作る (zip でよくある)。
    int cur = slash;
    while (cur > 0) {
      const QString dirPath = path.left(cur);
      if (!ctx->entries.contains(dirPath)) {
        ArchiveEntry d;
        d.pathInArchive = dirPath;
        const int s2    = dirPath.lastIndexOf(QLatin1Char('/'));
        d.name          = (s2 < 0) ? dirPath : dirPath.mid(s2 + 1);
        d.isDir         = true;
        ctx->entries.insert(dirPath, d);
      }
      cur = dirPath.lastIndexOf(QLatin1Char('/'));
    }
  }

  archive_read_close(a);
  archive_read_free(a);
  return ctx;
}

QList<const ArchiveEntry*> ArchiveContext::childrenOf(const QString& innerDir) const {
  const QString parentKey = normalizeDirKey(innerDir);
  QList<const ArchiveEntry*> out;
  for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
    const QString& p = it.key();
    const int slash  = p.lastIndexOf(QLatin1Char('/'));
    const QString parent = (slash < 0) ? QString() : p.left(slash);
    if (parent == parentKey) {
      out.append(&it.value());
    }
  }
  return out;
}

bool ArchiveContext::extractEntryTo(const QString& entryPath,
                                    const QString& destPath) const {
  // Zip Slip 防衛: アーカイブ内パス側に `..` や NUL があれば即拒否。
  // (呼び出し側が destPath を信頼できる場所に組み立てている前提だが、念のため
  //  entryPath 単体でも検査しておく)。
  if (!isSafeArchiveEntryName(entryPath)) return false;

  // 親ディレクトリを確保
  QFileInfo destInfo(destPath);
  if (!QDir().mkpath(destInfo.absolutePath())) return false;

  struct archive* a = archive_read_new();
  archive_read_support_format_all(a);
  archive_read_support_filter_all(a);
  // 暗号化エントリ用パスワードを設定 (空文字列は無視される)。
  if (!password.isEmpty()) {
    archive_read_add_passphrase(a, password.toUtf8().constData());
  }

#ifdef Q_OS_WIN
  const int openResult = archive_read_open_filename_w(a, asWChar(archivePath), 64 * 1024);
#else
  const int openResult = archive_read_open_filename(
    a, archivePath.toUtf8().constData(), 64 * 1024);
#endif
  if (openResult != ARCHIVE_OK) {
    archive_read_free(a);
    return false;
  }

  bool found = false;
  bool ok    = false;
  struct archive_entry* entry = nullptr;
  while (true) {
    const int r = archive_read_next_header(a, &entry);
    if (r == ARCHIVE_EOF) break;
    if (r < ARCHIVE_WARN) break;
    if (r < ARCHIVE_OK)  continue;

    const QString name = readEntryPath(entry);
    if (name != entryPath) continue;

    found = true;
    QFile out(destPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) break;

    bool writeOk = true;
    const void* buf = nullptr;
    size_t      sz  = 0;
    la_int64_t  off = 0;
    while (true) {
      const int rr = archive_read_data_block(a, &buf, &sz, &off);
      if (rr == ARCHIVE_EOF) break;
      if (rr < ARCHIVE_OK)  { writeOk = false; break; }
      const qint64 written = out.write(reinterpret_cast<const char*>(buf), sz);
      if (written < 0 || static_cast<size_t>(written) != sz) {
        writeOk = false; break;
      }
    }
    out.close();
    ok = writeOk;
    break;
  }

  archive_read_close(a);
  archive_read_free(a);
  if (!found || !ok) {
    QFile::remove(destPath);
    return false;
  }
  return true;
}

bool ArchiveContext::verifyPassword(const QString& archivePath,
                                    const QString& password) {
  // 暗号化エントリを 1 件だけ試し読みする。少しでも復号できれば true。
  // ARCHIVE_FATAL (-30) や負値が返れば失敗 (incorrect passphrase 等)。
  // 暗号化エントリが 1 件も無いアーカイブでは「password が要らない」ので
  // 検証成功扱い (true)。
  struct archive* a = archive_read_new();
  archive_read_support_format_all(a);
  archive_read_support_filter_all(a);
  if (!password.isEmpty()) {
    archive_read_add_passphrase(a, password.toUtf8().constData());
  }
#ifdef Q_OS_WIN
  const int openResult = archive_read_open_filename_w(a, asWChar(archivePath), 64 * 1024);
#else
  const int openResult = archive_read_open_filename(
    a, archivePath.toUtf8().constData(), 64 * 1024);
#endif
  if (openResult != ARCHIVE_OK) {
    archive_read_free(a);
    return false;
  }

  bool result = true;  // デフォルト: 暗号化エントリが見つからなければ OK
  bool foundEncrypted = false;
  struct archive_entry* entry = nullptr;
  while (true) {
    const int r = archive_read_next_header(a, &entry);
    if (r == ARCHIVE_EOF) break;
    if (r < ARCHIVE_WARN) break;
    if (r < ARCHIVE_OK)  continue;
    if (!archive_entry_is_encrypted(entry)) continue;
    if (archive_entry_size(entry) <= 0) continue;  // 空ファイルは復号できないので次へ

    foundEncrypted = true;
    char buf[1024];
    const la_ssize_t n = archive_read_data(a, buf, sizeof(buf));
    if (n < 0) {
      // ARCHIVE_FATAL 等 → incorrect passphrase の可能性が高い
      result = false;
    } else {
      // 0 (EOF) または > 0 (一部復号できた) のいずれも成功扱い
      result = true;
    }
    break;
  }
  // 暗号化エントリが 1 件も無かった場合は password 不要 → 検証成功
  if (!foundEncrypted) result = true;

  archive_read_close(a);
  archive_read_free(a);
  return result;
}

bool ArchiveContext::isValidDirectory(const QString& innerDir) const {
  if (innerDir.isEmpty() || innerDir == QStringLiteral("/")) return true;
  const QString key = normalizeDirKey(innerDir);
  auto it = entries.constFind(key);
  if (it != entries.cend()) return it.value().isDir;
  // 明示エントリが無くても、子孫が存在すれば「合成ディレクトリ」として有効
  for (auto i = entries.cbegin(); i != entries.cend(); ++i) {
    if (i.key().startsWith(key + QLatin1Char('/'))) return true;
  }
  return false;
}

} // namespace Farman
