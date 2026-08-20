#include "ArchiveContext.h"
#include "ArchiveFormatCatalog.h"
#include "utils/ArchivePath.h"
#include "ArchiveDispatcher.h"
#include "ArchiveEntryName.h"
#include "IArchivePlugin.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <archive.h>
#include <archive_entry.h>

namespace Farman {

namespace {

// 単一ファイル圧縮 (.gz / .xz など、tar を伴わないもの) を読むための設定。
//
// libarchive はこの手のファイルを "raw" フォーマットとして 1 エントリの書庫の
// ように読める。ただし raw は中身を問わず何でも受け入れる catch-all なので、
// archive_read_support_format_all() には含まれていない。無条件に足すと壊れた
// zip まで「1 エントリの書庫」として開けてしまうため、カタログ上その拡張子が
// 単一ファイル圧縮として有効になっているときにだけ足す。
void addFormatSupport(struct archive* a, const QString& archivePath) {
  archive_read_support_format_all(a);
  if (ArchiveFormatCatalog::isSingleFileCompression(archivePath)) {
    archive_read_support_format_raw(a);
  }
  archive_read_support_filter_all(a);
}

// サイズ未設定のエントリを、実際に伸長して数えるときの上限 (圧縮後サイズ)。
// これを超えるものは「数えるためだけに全体を伸長する」コストが見合わないので
// 不明 (-1) のままにする。
constexpr qint64 kMeasureMaxCompressedBytes = 32LL * 1024 * 1024;

// 現在のエントリのデータを読み飛ばしながらバイト数を数える。読み切れなければ
// -1 (不明)。呼び出した時点で archive_read_next_header 直後であること。
qint64 measureEntrySize(struct archive* a, std::atomic<bool>* cancelFlag) {
  qint64 total = 0;
  const void* buf = nullptr;
  size_t      sz  = 0;
  la_int64_t  off = 0;
  while (true) {
    if (cancelFlag && cancelFlag->load()) return -1;
    const int r = archive_read_data_block(a, &buf, &sz, &off);
    if (r == ARCHIVE_EOF) break;
    if (r < ARCHIVE_OK)   return -1;
    total += static_cast<qint64>(sz);
  }
  return total;
}

// raw で読んだエントリの表示名。libarchive は "data" という固定名を返すので、
// アーカイブ名から圧縮拡張子を剥がした名前 ("hello.txt.gz" → "hello.txt") に
// 置き換える。ユーザーから見て自然な名前にするため。
QString singleFileEntryName(const QString& archivePath) {
  const QString fileName = QFileInfo(archivePath).fileName();
  const QString base     = ArchivePath::archiveBaseName(fileName);
  return base.isEmpty() ? fileName : base;
}

} // namespace


namespace {

#ifdef Q_OS_WIN
inline const wchar_t* asWChar(const QString& s) {
  return reinterpret_cast<const wchar_t*>(s.utf16());
}
#endif

// libarchive のエントリパス (アーカイブの内部表現) を取得して
// 「先頭 '/' なし、末尾 '/' なし」のキー文字列に整える。
// encoding は設定 → アーカイブの形式ごとの「ファイル名の文字コード」。
// 空なら従来どおり自動判別する。アーカイブ 1 つにつき 1 回引いた値を
// 呼び出し側から渡す。
QString readEntryPath(struct archive_entry* entry, const QString& encoding) {
  // UTF-8 フラグ無しの CP932 (Shift-JIS) zip 等でも文字化けしないよう、
  // 共通ヘルパでエンコーディングを判定して QString 化する。
  QString path = decodeArchiveEntryName(entry, encoding);
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

// アーカイブプラグインが返した raw エントリ一覧を ctx->entries に取り込む。
// **セキュリティはここ (ホスト側) で担保**する: プラグインの実装品質に依存せず、
// `..`/NUL を含む危険エントリを弾き、親ディレクトリが明示されていないアーカイブ
// のために合成ディレクトリを補う。組み込み libarchive 経路 (load) の該当ロジック
// と同じ防御・整形をプラグイン経路にも適用するためのヘルパ。
void finalizeEntriesInto(ArchiveContext& ctx, const QList<ArchiveEntry>& raw) {
  for (const ArchiveEntry& src : raw) {
    // プラグインが末尾/先頭 '/' を残していても揃える。
    QString path = src.pathInArchive;
    while (path.size() > 0 && path.endsWith(QLatin1Char('/')))   path.chop(1);
    while (path.size() > 0 && path.startsWith(QLatin1Char('/'))) path.remove(0, 1);
    if (path.isEmpty()) continue;
    if (!isSafeArchiveEntryName(path)) continue;

    ArchiveEntry e = src;
    e.pathInArchive  = path;
    const int slash  = path.lastIndexOf(QLatin1Char('/'));
    e.name           = (slash < 0) ? path : path.mid(slash + 1);
    ctx.entries.insert(path, e);

    // 親ディレクトリが明示されていない場合の合成ディレクトリ生成。
    int cur = slash;
    while (cur > 0) {
      const QString dirPath = path.left(cur);
      if (!ctx.entries.contains(dirPath)) {
        ArchiveEntry d;
        d.pathInArchive = dirPath;
        const int s2    = dirPath.lastIndexOf(QLatin1Char('/'));
        d.name          = (s2 < 0) ? dirPath : dirPath.mid(s2 + 1);
        d.isDir         = true;
        ctx.entries.insert(dirPath, d);
      }
      cur = dirPath.lastIndexOf(QLatin1Char('/'));
    }
  }
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
  std::atomic<int>*      entriesRead,
  const QString&         localPath) {
  auto ctx = std::make_shared<ArchiveContext>();
  ctx->archivePath  = archivePath;
  ctx->localPath    = localPath;
  // 実体を読むのは readPath()。入れ子アーカイブでは archivePath は論理パスで、
  // ディスク上に無いので、ここから下は readPath 側だけを見る。
  const QString readFrom = ctx->readPath();
  ctx->archiveMtime = QFileInfo(readFrom).lastModified();

  // ── アーカイブプラグイン委譲 ──────────────────
  // 拡張子をアーカイブプラグイン (例 lzh) が所有していれば、エントリ列挙を
  // そのプラグインに委ねる。プラグインは raw な一覧を返すだけで、Zip Slip 防御・
  // 合成ディレクトリ生成といったセキュリティ整形は finalizeEntriesInto (ホスト側)
  // で行う。
  if (IArchivePlugin* plugin =
        ArchiveDispatcher::instance().pluginForPath(readFrom)) {
    ArchiveListResult res = plugin->listEntries(readFrom, cancelFlag, entriesRead);
    if (!res.ok) {
      if (errorOut) {
        *errorOut = res.error.isEmpty()
          ? QObject::tr("Failed to read archive: %1").arg(archivePath)
          : res.error;
      }
      return nullptr;
    }
    ctx->hasEncryptedEntries = res.hasEncryptedEntries;
    finalizeEntriesInto(*ctx, res.entries);
    return ctx;
  }

  const bool isSingleFile =
    ArchiveFormatCatalog::isSingleFileCompression(archivePath);

  // エントリ名の文字コードは形式ごとの設定 (空 = 自動判別)。エントリのループ
  // 内で毎回引くとパターン照合が効いてくるので、ここで 1 回だけ引く。
  const QString nameEncoding =
    filenameEncodingFor(QFileInfo(archivePath).fileName());

  struct archive* a = archive_read_new();
  addFormatSupport(a, archivePath);

#ifdef Q_OS_WIN
  const int openResult = archive_read_open_filename_w(a, asWChar(readFrom), 64 * 1024);
#else
  const int openResult = archive_read_open_filename(a, readFrom.toUtf8().constData(), 64 * 1024);
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

    QString path = readEntryPath(entry, nameEncoding);
    // raw (単一ファイル圧縮) は名前を持たず libarchive が "data" を返す。
    // ユーザーから見て自然な名前 ("hello.txt.gz" → "hello.txt") に置き換える。
    if (isSingleFile) {
      path = singleFileEntryName(archivePath);
    }
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
    // サイズはヘッダに入っているときだけ信用する。raw (単一ファイル圧縮) の
    // ように伸長後サイズを持たない形式では archive_entry_size() が 0 を返すため、
    // そのまま使うと「0 バイト」と嘘の表示になる。未設定は -1 (不明) にする。
    if (e.isDir) {
      e.size = -1;
    } else if (archive_entry_size_is_set(entry)) {
      e.size = static_cast<qint64>(archive_entry_size(entry));
    } else {
      e.size = -1;
      // 単一ファイル圧縮は中身が 1 つだけなので、小さいものはその場で伸長して
      // 実サイズを数える (書庫を開いた時点で読み切れる範囲に限る)。大きいものは
      // 数えるだけのために全体を伸長するのは割に合わないので不明のままにする。
      if (isSingleFile && QFileInfo(readFrom).size() <= kMeasureMaxCompressedBytes) {
        e.size = measureEntrySize(a, cancelFlag);
      }
    }
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

  // 入れ子アーカイブでは archivePath は論理パスなので、実体は readPath()。
  const QString readFrom = readPath();

  // アーカイブプラグインが所有する拡張子なら、1 エントリ展開もそこへ委譲する。
  // entryPath の安全性 (`..`/NUL 拒否) は上で検査済み。
  if (IArchivePlugin* plugin =
        ArchiveDispatcher::instance().pluginForPath(archivePath)) {
    return plugin->extractEntry(readFrom, entryPath, destPath, password);
  }

  // 一覧側 (load) と同じ文字コードで名前を復号しないと、entryPath と照合
  // できない。
  const QString nameEncoding =
    filenameEncodingFor(QFileInfo(archivePath).fileName());

  struct archive* a = archive_read_new();
  addFormatSupport(a, archivePath);
  // 暗号化エントリ用パスワードを設定 (空文字列は無視される)。
  if (!password.isEmpty()) {
    archive_read_add_passphrase(a, password.toUtf8().constData());
  }

#ifdef Q_OS_WIN
  const int openResult = archive_read_open_filename_w(a, asWChar(readFrom), 64 * 1024);
#else
  const int openResult = archive_read_open_filename(
    a, readFrom.toUtf8().constData(), 64 * 1024);
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

    // raw (単一ファイル圧縮) は libarchive が "data" を返すので、一覧側で
    // 付け替えた名前 (singleFileEntryName) と一致しない。エントリは 1 つしか
    // 無いので名前の照合はせず、そのまま書き出す。
    if (!ArchiveFormatCatalog::isSingleFileCompression(archivePath)) {
      const QString name = readEntryPath(entry, nameEncoding);
      if (name != entryPath) continue;
    }

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
  // アーカイブプラグインが所有する拡張子なら、パスワード検証もそこへ委譲する。
  if (IArchivePlugin* plugin =
        ArchiveDispatcher::instance().pluginForPath(archivePath)) {
    return plugin->verifyPassword(archivePath, password);
  }

  // 暗号化エントリを 1 件だけ試し読みする。少しでも復号できれば true。
  // ARCHIVE_FATAL (-30) や負値が返れば失敗 (incorrect passphrase 等)。
  // 暗号化エントリが 1 件も無いアーカイブでは「password が要らない」ので
  // 検証成功扱い (true)。
  struct archive* a = archive_read_new();
  addFormatSupport(a, archivePath);
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
