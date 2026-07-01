#include "ArchiveExtractWorker.h"
#include "core/ArchiveEntryName.h"
#include "utils/ArchivePath.h"
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <archive.h>
#include <archive_entry.h>

namespace Farman {

namespace {

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

#ifdef Q_OS_WIN
// QString → const wchar_t* の薄いヘルパ。Windows では QString の utf16()
// をそのまま wchar_t* として扱える (Windows の wchar_t は 16-bit)。
inline const wchar_t* asWChar(const QString& s) {
  return reinterpret_cast<const wchar_t*>(s.utf16());
}
#endif

} // anonymous namespace

ArchiveExtractWorker::ArchiveExtractWorker(const QString& archivePath,
                                           const QString& outputDir,
                                           const QString& password,
                                           QObject*       parent)
  : WorkerBase(parent)
  , m_archivePath(archivePath)
  , m_outputDir(outputDir)
  , m_password(password) {
}

void ArchiveExtractWorker::run() {
  // 展開先ディレクトリを確保
  QDir().mkpath(m_outputDir);

  // 出力ディレクトリの実体パスに解決する。macOS の `/tmp` (→ /private/tmp)
  // のように上位コンポーネントが symlink だと、ARCHIVE_EXTRACT_SECURE_SYMLINKS
  // が「出力パスの途中に symlink がある」として書き込みを全て拒否してしまう。
  // canonicalFilePath は実在パスを実体に解決するので、`/private/tmp/...` まで
  // 解決した上で libarchive に渡せばこの誤検知を避けられる。
  QString outputDir = QFileInfo(m_outputDir).canonicalFilePath();
  if (outputDir.isEmpty()) outputDir = m_outputDir;

  struct archive* src = archive_read_new();
  archive_read_support_format_all(src);
  archive_read_support_filter_all(src);
  // 暗号化エントリ用パスワード (空文字は無視される)。
  if (!m_password.isEmpty()) {
    archive_read_add_passphrase(src, m_password.toUtf8().constData());
  }

  // Windows では archive_read_open_filename (char*) が ANSI 解釈なので、
  // 日本語パス (例: OneDrive\ドキュメント) や OneDrive 配下が開けない。
  // wchar_t* 版を使えば QString の UTF-16 表現をそのまま渡せる。
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

  struct archive* dst = archive_write_disk_new();
  // SECURE_SYMLINKS: 出力パス上にユーザーが書き込めない symlink があったら
  // 展開を拒否する。これがないと「先に link/ → /tmp/x を仕込み、後続で
  // link/foo を展開して /tmp/x/foo を書く」式の Zip Slip が成立する。
  archive_write_disk_set_options(dst,
    ARCHIVE_EXTRACT_TIME    | ARCHIVE_EXTRACT_PERM |
    ARCHIVE_EXTRACT_ACL     | ARCHIVE_EXTRACT_FFLAGS |
    ARCHIVE_EXTRACT_SECURE_SYMLINKS);
  archive_write_disk_set_standard_lookup(dst);

  WorkerProgress progress;
  progress.filesDone  = 0;
  progress.filesTotal = 0;  // 事前には不明
  progress.processed  = 0;
  progress.total      = -1;

  bool success = true;
  struct archive_entry* entry = nullptr;
  while (true) {
    if (isCancelled()) { success = false; break; }

    const int r = archive_read_next_header(src, &entry);
    if (r == ARCHIVE_EOF) break;
    if (r < ARCHIVE_WARN) {
      // 致命エラー: libarchive のエラーメッセージを伝えて中断する
      // (壊れたアーカイブを「途中まで成功」扱いにしない)。
      emit errorOccurred(m_archivePath,
                         QString::fromUtf8(archive_error_string(src)));
      success = false; break;
    }
    if (r < ARCHIVE_OK) continue;  // 警告は黙って次へ

    // 出力パスを outputDir 配下へ書き換える。
    // エントリ名は UTF-8 フラグ無しの CP932 (Shift-JIS) zip でも文字化けしない
    // よう共通ヘルパで判定して QString 化する。宛先 path も下でこの UTF-8 /
    // wchar_t からセットするので、write-disk 段で日本語ファイル名が化けない。
    const QString origName = decodeArchiveEntryName(entry);
    // Zip Slip 対策: `..` / 絶対パス / outputDir を抜け出すエントリは拒否。
    // 危険エントリを 1 件でも含むアーカイブは「正常終了」にせず、操作全体を
    // 失敗扱いにする (UI 側で errorOccurred を表示しなくても、最終的な
    // finished(success=false) でユーザーに失敗が伝わる)。
    const QString destPath = ArchivePath::safeJoinExtractPath(outputDir, origName);
    if (destPath.isEmpty()) {
      emit errorOccurred(origName,
        QStringLiteral("Refused unsafe archive entry path: %1").arg(origName));
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
