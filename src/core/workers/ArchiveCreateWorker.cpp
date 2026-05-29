#include "ArchiveCreateWorker.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <archive.h>
#include <archive_entry.h>
#ifndef Q_OS_WIN
  #include <sys/stat.h>
#endif

namespace Farman {

namespace {

static constexpr qint64 kReadBufSize = 64 * 1024;

#ifdef Q_OS_WIN
// QString → const wchar_t* の薄いヘルパ。Windows では QString は内部的に
// UTF-16 を保持しているので、utf16() の戻りをそのまま wchar_t* として
// 扱える (Windows の wchar_t は 16-bit)。
inline const wchar_t* asWChar(const QString& s) {
  return reinterpret_cast<const wchar_t*>(s.utf16());
}
#endif

} // anonymous namespace

ArchiveCreateWorker::ArchiveCreateWorker(const QString&     outputPath,
                                         Format             format,
                                         const QStringList& inputPaths,
                                         QObject*           parent)
  : WorkerBase(parent)
  , m_outputPath(outputPath)
  , m_format(format)
  , m_inputPaths(inputPaths) {
}

void ArchiveCreateWorker::run() {
  struct archive* a = archive_write_new();
  if (!a) {
    emit errorOccurred(m_outputPath, "archive_write_new failed");
    emit finished(false);
    return;
  }

  // フォーマット設定
  switch (m_format) {
    case Format::Zip:
      archive_write_set_format_zip(a);
      break;
    case Format::Tar:
      archive_write_set_format_pax_restricted(a);
      break;
    case Format::TarGz:
      archive_write_set_format_pax_restricted(a);
      archive_write_add_filter_gzip(a);
      break;
    case Format::TarBz2:
      archive_write_set_format_pax_restricted(a);
      archive_write_add_filter_bzip2(a);
      break;
    case Format::TarXz:
      archive_write_set_format_pax_restricted(a);
      archive_write_add_filter_xz(a);
      break;
  }

  // ── 作成オプション (open より前に設定する必要がある) ──
  // 圧縮レベル: 形式に対応する libarchive の module 名で指定。失敗しても
  // 致命的ではない (既定レベルで続行) ので戻り値は無視する。
  if (m_compressionLevel >= 0) {
    const char* mod = nullptr;
    int level = m_compressionLevel;
    switch (m_format) {
      case Format::Zip:    mod = "zip";   break;
      case Format::TarGz:  mod = "gzip";  break;
      case Format::TarBz2: mod = "bzip2"; if (level < 1) level = 1; break;  // bzip2 は 1〜9
      case Format::TarXz:  mod = "xz";    break;
      case Format::Tar:    mod = nullptr; break;  // 無圧縮なので無視
    }
    if (mod) {
      const QByteArray opt = (QString::fromLatin1(mod)
        + QStringLiteral(":compression-level=") + QString::number(level)).toUtf8();
      archive_write_set_options(a, opt.constData());
    }
  }

  // 暗号化 (zip のみ)。要求した暗号化が使えない libarchive ビルドで平文を
  // 黙って作ると「暗号化したつもり」のセキュリティ事故になるため、
  // set_passphrase / set_options が失敗したら明示的にエラー終了する。
  if (m_encryption != Encryption::None && !m_passphrase.isEmpty()) {
    if (m_format != Format::Zip) {
      emit errorOccurred(m_outputPath,
        QStringLiteral("Encryption is only supported for zip archives"));
      archive_write_free(a);
      emit finished(false);
      return;
    }
    const QByteArray pass = m_passphrase.toUtf8();
    if (archive_write_set_passphrase(a, pass.constData()) != ARCHIVE_OK) {
      emit errorOccurred(m_outputPath,
        QString::fromUtf8(archive_error_string(a)));
      archive_write_free(a);
      emit finished(false);
      return;
    }
    const char* enc = (m_encryption == Encryption::Aes256)
                        ? "zip:encryption=aes256"
                        : "zip:encryption=zipcrypt";
    if (archive_write_set_options(a, enc) != ARCHIVE_OK) {
      emit errorOccurred(m_outputPath,
        QStringLiteral("This build of libarchive cannot encrypt zip archives: %1")
          .arg(QString::fromUtf8(archive_error_string(a))));
      archive_write_free(a);
      emit finished(false);
      return;
    }
  }

  // Windows では archive_write_open_filename (char*) が ANSI 解釈なので、
  // 日本語パス (例: OneDrive\ドキュメント) や OneDrive 配下が開けない。
  // wchar_t* 版を使えば QString の UTF-16 表現をそのまま渡せる。
#ifdef Q_OS_WIN
  const int openResult = archive_write_open_filename_w(a, asWChar(m_outputPath));
#else
  const int openResult = archive_write_open_filename(a, m_outputPath.toUtf8().constData());
#endif
  if (openResult != ARCHIVE_OK) {
    emit errorOccurred(m_outputPath,
                       QString::fromUtf8(archive_error_string(a)));
    archive_write_free(a);
    emit finished(false);
    return;
  }

  WorkerProgress progress;
  progress.filesDone  = 0;
  progress.filesTotal = m_inputPaths.size();
  progress.processed  = 0;
  progress.total      = -1;

  bool success = true;
  for (const QString& inputPath : m_inputPaths) {
    if (isCancelled()) { success = false; break; }

    const QFileInfo info(inputPath);
    if (!info.exists()) {
      emit errorOccurred(inputPath, "Path does not exist");
      success = false;
      continue;
    }

    progress.currentFile = inputPath;
    emit progressUpdated(progress);

    const QString entryName = info.fileName();
    if (info.isDir()) {
      if (!addDirectoryRecursive(a, info.absoluteFilePath(), entryName)) {
        success = false;
      }
    } else {
      if (!addEntry(a, info.absoluteFilePath(), entryName)) {
        success = false;
      }
    }

    ++progress.filesDone;
    emit progressUpdated(progress);
  }

  archive_write_close(a);
  archive_write_free(a);

  // キャンセルされた場合は不完全なファイルを削除
  if (isCancelled()) {
    QFile::remove(m_outputPath);
    emit finished(false);
    return;
  }

  emit finished(success);
}

bool ArchiveCreateWorker::addEntry(::archive* a,
                                   const QString& absPath,
                                   const QString& entryName) {
  // 先にファイルを開いておく。open できない（= 権限無し）場合、ヘッダだけ
  // 書いてデータを書かない「壊れたエントリ」を残さないようここでエラー終了する。
  // QFile は Windows でも内部で UTF-16 path を使うので日本語パスが通る。
  QFile f(absPath);
  if (!f.open(QIODevice::ReadOnly)) {
    emit errorOccurred(absPath, "Failed to open for reading");
    return false;
  }

  struct archive_entry* entry = archive_entry_new();

#ifdef Q_OS_WIN
  // Windows: ::stat (ANSI) は UTF-8 path をうまく解釈できないので、
  // QFileInfo で取れる情報を archive_entry に直接セットする。POSIX permission
  // は意味が薄いため無難なデフォルト (0644) を入れておく。
  archive_entry_copy_pathname_w(entry, asWChar(entryName));
  QFileInfo fi(absPath);
  archive_entry_set_size(entry, fi.size());
  archive_entry_set_filetype(entry, AE_IFREG);
  archive_entry_set_mtime(entry, fi.lastModified().toSecsSinceEpoch(), 0);
  archive_entry_set_perm(entry, 0644);
#else
  // Unix は ::stat → archive_entry_copy_stat が一番確実 (mode/size/mtime
  // 一括で正しく入る)。Qt の QFile::Permissions を mode_t に手で詰め直すと
  // bits 不整合でヘッダの file size/type が壊れる罠があるため、stat 経由を
  // そのまま維持する。
  struct stat st {};
  if (::stat(absPath.toUtf8().constData(), &st) != 0) {
    emit errorOccurred(absPath, "stat failed");
    f.close();
    archive_entry_free(entry);
    return false;
  }
  archive_entry_set_pathname(entry, entryName.toUtf8().constData());
  archive_entry_copy_stat(entry, &st);
#endif

  const int hr = archive_write_header(a, entry);
  if (hr < ARCHIVE_OK) {
    emit errorOccurred(absPath,
                       QString::fromUtf8(archive_error_string(a)));
    if (hr < ARCHIVE_WARN) {
      f.close();
      archive_entry_free(entry);
      return false;
    }
  }

  QByteArray buf;
  buf.resize(kReadBufSize);
  while (!f.atEnd()) {
    if (isCancelled()) {
      f.close();
      archive_entry_free(entry);
      return false;
    }
    const qint64 got = f.read(buf.data(), buf.size());
    if (got <= 0) break;

    // archive_write_data は一度の呼び出しで指定バイト数全てを書き切らない
    // 可能性がある（short write）。残りがなくなるまでループして書く。
    const char* p = buf.constData();
    size_t remaining = static_cast<size_t>(got);
    while (remaining > 0) {
      const la_ssize_t wrote = archive_write_data(a, p, remaining);
      if (wrote < 0) {
        emit errorOccurred(absPath,
                           QString::fromUtf8(archive_error_string(a)));
        f.close();
        archive_entry_free(entry);
        return false;
      }
      if (wrote == 0) break;  // progress 無しの無限ループ防止
      p += wrote;
      remaining -= static_cast<size_t>(wrote);
    }
  }
  f.close();
  archive_entry_free(entry);
  return true;
}

bool ArchiveCreateWorker::addDirectoryRecursive(::archive* a,
                                                const QString& absPath,
                                                const QString& entryName) {
  // ディレクトリ自身のエントリ
  {
    const QString dirEntryName = entryName + QLatin1Char('/');
    struct archive_entry* entry = archive_entry_new();

#ifdef Q_OS_WIN
    // Windows: addEntry と同様に ::stat 回避 + wchar_t pathname。
    archive_entry_copy_pathname_w(entry, asWChar(dirEntryName));
    QFileInfo fi(absPath);
    archive_entry_set_filetype(entry, AE_IFDIR);
    archive_entry_set_mtime(entry, fi.lastModified().toSecsSinceEpoch(), 0);
    archive_entry_set_perm(entry, 0755);
#else
    struct stat st {};
    if (::stat(absPath.toUtf8().constData(), &st) != 0) {
      emit errorOccurred(absPath, "stat failed");
      archive_entry_free(entry);
      return false;
    }
    archive_entry_set_pathname(entry, dirEntryName.toUtf8().constData());
    archive_entry_copy_stat(entry, &st);
#endif

    const int r = archive_write_header(a, entry);
    archive_entry_free(entry);
    if (r < ARCHIVE_OK) {
      emit errorOccurred(absPath,
                         QString::fromUtf8(archive_error_string(a)));
      if (r < ARCHIVE_WARN) return false;
    }
  }

  // 配下のエントリを列挙
  QDirIterator it(absPath, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
  bool ok = true;
  while (it.hasNext()) {
    if (isCancelled()) return false;
    const QString childAbs = it.next();
    const QFileInfo childInfo(childAbs);
    if (childInfo.isSymLink()) continue;  // シンボリックリンクは追跡しない

    const QString childEntry = entryName + QLatin1Char('/') + childInfo.fileName();
    if (childInfo.isDir()) {
      if (!addDirectoryRecursive(a, childAbs, childEntry)) ok = false;
    } else {
      if (!addEntry(a, childAbs, childEntry)) ok = false;
    }
  }
  return ok;
}

} // namespace Farman
