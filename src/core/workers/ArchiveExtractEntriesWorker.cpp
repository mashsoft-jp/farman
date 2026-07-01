#include "ArchiveExtractEntriesWorker.h"
#include "core/ArchiveEntryName.h"
#include "utils/ArchivePath.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <archive.h>
#include <archive_entry.h>

namespace Farman {

namespace {

#ifdef Q_OS_WIN
inline const wchar_t* asWChar(const QString& s) {
  return reinterpret_cast<const wchar_t*>(s.utf16());
}
#endif

QString readEntryPath(struct archive_entry* entry) {
  // CP932 (Shift-JIS) 等の非 UTF-8 zip でも文字化けしないよう共通ヘルパで判定。
  QString path = decodeArchiveEntryName(entry);
  while (path.size() > 0 && path.endsWith(QLatin1Char('/'))) path.chop(1);
  while (path.size() > 0 && path.startsWith(QLatin1Char('/'))) path.remove(0, 1);
  return path;
}

// archive の中から writer (disk) へ 1 エントリ分のデータをコピー
bool copyData(struct archive* src, struct archive* dst) {
  const void* buf = nullptr;
  size_t  size = 0;
  la_int64_t offset = 0;
  while (true) {
    const int r = archive_read_data_block(src, &buf, &size, &offset);
    if (r == ARCHIVE_EOF) return true;
    if (r < ARCHIVE_OK)   return false;
    if (archive_write_data_block(dst, buf, size, offset) < ARCHIVE_OK) {
      return false;
    }
  }
}

} // namespace

ArchiveExtractEntriesWorker::ArchiveExtractEntriesWorker(
  const QString&     archivePath,
  const QStringList& selectedFiles,
  const QStringList& selectedDirs,
  const QString&     currentInnerDir,
  const QString&     destDir,
  int                filesTotal,
  const QString&     password,
  QObject*           parent)
  : WorkerBase(parent)
  , m_archivePath(archivePath)
  , m_selectedFiles(selectedFiles)
  , m_selectedDirs(selectedDirs)
  , m_currentInnerDir(currentInnerDir)
  , m_destDir(destDir)
  , m_filesTotal(filesTotal)
  , m_password(password) {
  // 先頭/末尾 '/' を剥がして揃える (空 = ルート)
  while (m_currentInnerDir.startsWith(QLatin1Char('/'))) m_currentInnerDir.remove(0, 1);
  while (m_currentInnerDir.endsWith(QLatin1Char('/')))   m_currentInnerDir.chop(1);
}

void ArchiveExtractEntriesWorker::run() {
  QDir().mkpath(m_destDir);

  // 出力ディレクトリの実体パス解決 (macOS `/tmp` → `/private/tmp` 等の symlink
  // が ARCHIVE_EXTRACT_SECURE_SYMLINKS で誤拒否されるのを回避する)。
  QString destDir = QFileInfo(m_destDir).canonicalFilePath();
  if (destDir.isEmpty()) destDir = m_destDir;

  // 高速判定用に Set 化。selectedDirs は trailing '/' 付きの prefix にして
  // startsWith でも使えるようにしておく。
  const QSet<QString> fileSet(m_selectedFiles.cbegin(), m_selectedFiles.cend());
  QSet<QString>       dirSet(m_selectedDirs.cbegin(),  m_selectedDirs.cend());
  QStringList         dirPrefixes;
  for (const QString& d : m_selectedDirs) {
    dirPrefixes.append(d + QLatin1Char('/'));
  }

  // libarchive で読み込み準備
  struct archive* src = archive_read_new();
  archive_read_support_format_all(src);
  archive_read_support_filter_all(src);
  // 暗号化エントリ用パスワード (空文字なら効果なし)
  if (!m_password.isEmpty()) {
    archive_read_add_passphrase(src, m_password.toUtf8().constData());
  }

#ifdef Q_OS_WIN
  const int openResult = archive_read_open_filename_w(src, asWChar(m_archivePath), 64 * 1024);
#else
  const int openResult = archive_read_open_filename(src,
                          m_archivePath.toUtf8().constData(), 64 * 1024);
#endif
  if (openResult != ARCHIVE_OK) {
    emit errorOccurred(m_archivePath,
                       QString::fromUtf8(archive_error_string(src)));
    archive_read_free(src);
    emit finished(false);
    return;
  }

  // 書き出し先 (disk) を準備。Permissions / ACL / FFLAGS は反映、Time は反映する。
  // SECURE_SYMLINKS は Zip Slip 対策 (link → 外部パス + 後続エントリで脱出)。
  struct archive* dst = archive_write_disk_new();
  archive_write_disk_set_options(dst,
    ARCHIVE_EXTRACT_TIME    | ARCHIVE_EXTRACT_PERM |
    ARCHIVE_EXTRACT_ACL     | ARCHIVE_EXTRACT_FFLAGS |
    ARCHIVE_EXTRACT_SECURE_SYMLINKS);
  archive_write_disk_set_standard_lookup(dst);

  WorkerProgress progress;
  progress.filesDone  = 0;
  progress.filesTotal = m_filesTotal;  // 呼び出し側で事前計算済み (<=0 = indeterminate)
  progress.processed  = 0;
  progress.total      = -1;

  bool success = true;
  struct archive_entry* entry = nullptr;
  while (true) {
    if (isCancelled()) { success = false; break; }

    const int r = archive_read_next_header(src, &entry);
    if (r == ARCHIVE_EOF) break;
    if (r < ARCHIVE_WARN) {
      // 致命エラーは部分成功扱いせず中断する。
      emit errorOccurred(m_archivePath,
                         QString::fromUtf8(archive_error_string(src)));
      success = false; break;
    }
    if (r < ARCHIVE_OK)  continue;

    const QString entryPath = readEntryPath(entry);
    if (entryPath.isEmpty()) continue;

    // 抽出対象か判定:
    //   1. ファイルとして単独選択されている (fileSet)
    //   2. ディレクトリとして単独選択されている (dirSet)
    //   3. いずれかの選択済みディレクトリの配下にある (dirPrefixes)
    bool shouldExtract = fileSet.contains(entryPath) || dirSet.contains(entryPath);
    if (!shouldExtract) {
      for (const QString& prefix : dirPrefixes) {
        if (entryPath.startsWith(prefix)) { shouldExtract = true; break; }
      }
    }
    if (!shouldExtract) continue;

    // Zip Slip 対策: ここに来たエントリは選択範囲内 (shouldExtract=true)。
    // この段階で `..` / NUL を含むなら、選択ディレクトリを抜け出そうとする
    // 悪意あるエントリ (例: 選択 `foo/` に対する `foo/../evil`)。黙って
    // スキップせず errorOccurred + success=false で操作全体を失敗扱いにする。
    if (entryPath.contains(QStringLiteral("/../"))
        || entryPath.startsWith(QStringLiteral("../"))
        || entryPath == QStringLiteral("..")
        || entryPath.endsWith(QStringLiteral("/.."))
        || entryPath.contains(QChar(0))) {
      emit errorOccurred(entryPath,
        QStringLiteral("Refused unsafe archive entry path: %1").arg(entryPath));
      success = false;
      continue;
    }

    // destDir 配下の絶対パスを構築。
    // ユーザーがいる「カレントアーカイブ内ディレクトリ」(m_currentInnerDir) を
    // 基準に階層を相対化することで、通常 FS のコピー感覚と一致させる。
    //   例 (currentInnerDir="inner"):
    //     entry "inner/hello.txt"   → relative "hello.txt"     → destDir/hello.txt
    //     entry "inner/sub/world.txt"→ relative "sub/world.txt" → destDir/sub/world.txt
    //   例 (currentInnerDir=""):
    //     entry "inner/hello.txt"   → そのまま → destDir/inner/hello.txt
    QString relative = entryPath;
    if (!m_currentInnerDir.isEmpty()) {
      const QString prefix = m_currentInnerDir + QLatin1Char('/');
      if (relative.startsWith(prefix)) {
        relative.remove(0, prefix.size());
      } else if (relative == m_currentInnerDir) {
        // カレントディレクトリ自身のエントリは展開しても意味が無いのでスキップ
        continue;
      }
    }
    // Zip Slip 対策: 結合・正規化後に destDir 配下に収まることを検証。
    // 危険エントリ 1 件で操作全体を失敗扱いにする (success=false)。
    const QString destPath = ArchivePath::safeJoinExtractPath(destDir, relative);
    if (destPath.isEmpty()) {
      emit errorOccurred(entryPath,
        QStringLiteral("Refused unsafe archive entry path: %1").arg(entryPath));
      success = false;
      continue;
    }
#ifdef Q_OS_WIN
    archive_entry_copy_pathname_w(entry, asWChar(destPath));
#else
    archive_entry_set_pathname(entry, destPath.toUtf8().constData());
#endif

    progress.currentFile = destPath;
    emit progressUpdated(progress);

    const int hr = archive_write_header(dst, entry);
    if (hr < ARCHIVE_OK) {
      emit errorOccurred(destPath,
                         QString::fromUtf8(archive_error_string(dst)));
      if (hr < ARCHIVE_WARN) { success = false; break; }
    } else if (archive_entry_size(entry) > 0) {
      if (!copyData(src, dst)) {
        emit errorOccurred(destPath,
                           QString::fromUtf8(archive_error_string(dst)));
        success = false;
        break;
      }
    }
    if (archive_write_finish_entry(dst) < ARCHIVE_OK) {
      emit errorOccurred(destPath,
                         QString::fromUtf8(archive_error_string(dst)));
      success = false;
      break;
    }

    ++progress.filesDone;
    emit progressUpdated(progress);
  }

  archive_read_close(src);
  archive_read_free(src);
  archive_write_close(dst);
  archive_write_free(dst);

  emit finished(success && !isCancelled());
}

} // namespace Farman
