#include "MoveWorker.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>

namespace Farman {

namespace {

// 移動先が移動元自身 or 配下かを判定する (CopyWorker の同名関数と同じ仕様)。
// rename(2) で同 FS なら一発で動くが、別 FS への fallback で copyDirectory が
// 走るときに再帰展開する危険があるため事前に弾く。
bool isDestinationInsideSource(const QString& srcPath, const QString& dstDir) {
  auto canonOrAbs = [](const QString& p) {
    QFileInfo fi(p);
    QString c = fi.canonicalFilePath();
    if (c.isEmpty()) c = QDir::cleanPath(fi.absoluteFilePath());
    return c;
  };
  const QString src = canonOrAbs(srcPath);
  const QString dst = canonOrAbs(dstDir);
  if (src.isEmpty() || dst.isEmpty()) return false;
  if (dst == src) return true;
  return dst.startsWith(src + QLatin1Char('/'));
}

// エントリ (ファイル or ディレクトリ) の合計バイト数を返す。
// rename 高速パスでは copyFile を通らずバイト進捗が出ないため、上段バーを
// 100% 完了として表示する際の分母に使う。
qint64 entryByteSize(const QString& path) {
  QFileInfo info(path);
  if (!info.isDir()) {
    return info.size();
  }
  qint64 total = 0;
  QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    total += it.fileInfo().size();
  }
  return total;
}

} // namespace

MoveWorker::MoveWorker(
  const QStringList& srcPaths,
  const QString&     dstDir,
  QObject*           parent)
  : WorkerBase(parent)
  , m_srcPaths(srcPaths)
  , m_dstDir(dstDir) {
}

void MoveWorker::run() {
  bool success = true;

  // 事前スキャンで全ファイル数を把握 (rename で一気に動く分も含む)
  m_progress = WorkerProgress{};
  m_progress.processed  = 0;
  m_progress.total      = -1;
  m_progress.filesDone  = 0;
  m_progress.filesTotal = countAllFiles(m_srcPaths);
  emit progressUpdated(m_progress);

  // Ensure destination directory exists
  QDir dstDir(m_dstDir);
  if (!dstDir.exists()) {
    emit errorOccurred(m_dstDir, "Destination directory does not exist");
    emit finished(false);
    return;
  }

  // Move each source path
  for (const QString& srcPath : m_srcPaths) {
    if (isCancelled()) {
      emit finished(false);
      return;
    }

    QFileInfo srcInfo(srcPath);
    if (!srcInfo.exists()) {
      emit errorOccurred(srcPath, "Source does not exist");
      success = false;
      continue;
    }

    // ディレクトリを自分自身 / 配下に移動しようとしている場合は拒否する。
    // 同 FS では rename(2) が失敗するが、別 FS なら copyDirectory フォール
    // バックが走ってしまうので、先に弾いておくのが安全。
    if (srcInfo.isDir() && isDestinationInsideSource(srcPath, m_dstDir)) {
      emit errorOccurred(srcPath,
        "Destination is inside the source directory");
      success = false;
      continue;
    }

    QString dstPath = m_dstDir + "/" + srcInfo.fileName();

    if (QFileInfo::exists(dstPath)) {
      OverwriteResolution resolution = resolveOverwrite(srcPath, dstPath);
      if (resolution.action == OverwriteResolution::Action::Cancel) {
        requestCancel();
        emit finished(false);
        return;
      }
      if (resolution.action == OverwriteResolution::Action::Rename) {
        dstPath = resolution.targetPath;
      } else {
        // Overwrite: 既存を削除
        QFileInfo dstInfo(dstPath);
        if (dstInfo.isDir()) {
          if (!removeDirectory(dstPath)) {
            emit errorOccurred(dstPath, "Failed to remove existing directory");
            success = false;
            continue;
          }
        } else {
          if (!QFile::remove(dstPath)) {
            emit errorOccurred(dstPath, "Failed to remove existing file");
            success = false;
            continue;
          }
        }
      }
    }

    m_progress.currentFile = srcPath;
    // 新しいエントリ開始。上段 (ファイル内バイト) バーは 0% から始める。
    m_progress.processed = 0;
    m_progress.total     = -1;
    emit progressUpdated(m_progress);

    // 事前にこの src 配下のファイル数を覚えておく。
    // rename で一気に移動できた場合 copyFile() が走らないので、
    // moveEntry 後にここまでの累積期待値まで filesDone を引き上げる。
    const int beforeDone   = m_progress.filesDone;
    const int expectedDone = beforeDone + countAllFiles({srcPath});

    if (!moveEntry(srcPath, dstPath)) {
      success = false;
      continue;
    }

    if (m_progress.filesDone < expectedDone) {
      m_progress.filesDone = expectedDone;
      emit progressUpdated(m_progress);
    }
  }

  emit finished(success);
}

bool MoveWorker::moveEntry(const QString& src, const QString& dst) {
  if (isCancelled()) {
    return false;
  }

  QFileInfo srcInfo(src);
  // run() 側でトップレベル競合は解決済み。ここでは dst は存在しない前提。

  // rename は瞬時に成功して src が消えるため、サイズは事前に控えておく。
  const qint64 entrySize = entryByteSize(src);

  // Try to rename first (fast for same filesystem)
  if (QFile::rename(src, dst)) {
    // 同 FS の rename では copyFile を通らずバイト進捗が出ないので、上段バーを
    // このエントリのバイト数で 100% 完了として一度報告する (空でも 100% にする)。
    const qint64 denom = entrySize > 0 ? entrySize : 1;
    m_progress.processed = denom;
    m_progress.total     = denom;
    emit progressUpdated(m_progress);
    return true;
  }

  // If rename fails, copy and then delete
  if (srcInfo.isDir()) {
    if (!copyDirectory(src, dst)) {
      return false;
    }
    if (!removeDirectory(src)) {
      emit errorOccurred(src, "Failed to remove source directory after copy");
      return false;
    }
  } else {
    if (!copyFile(src, dst)) {
      return false;
    }
    if (!QFile::remove(src)) {
      emit errorOccurred(src, "Failed to remove source file after copy");
      return false;
    }
  }

  return true;
}

bool MoveWorker::copyFile(const QString& src, const QString& dst) {
  if (isCancelled()) {
    return false;
  }

  // Open source file
  QFile srcFile(src);
  if (!srcFile.open(QIODevice::ReadOnly)) {
    emit errorOccurred(src, "Failed to open source file");
    return false;
  }

  // Open destination file
  QFile dstFile(dst);
  if (!dstFile.open(QIODevice::WriteOnly)) {
    emit errorOccurred(dst, "Failed to create destination file");
    srcFile.close();
    return false;
  }

  // Copy in chunks for cancelability and progress reporting
  const qint64 CHUNK_SIZE = 1024 * 1024; // 1MB chunks
  qint64 totalSize = srcFile.size();
  qint64 bytesCopied = 0;

  while (!srcFile.atEnd()) {
    if (isCancelled()) {
      srcFile.close();
      dstFile.close();
      QFile::remove(dst); // Remove incomplete file
      return false;
    }

    QByteArray chunk = srcFile.read(CHUNK_SIZE);
    if (chunk.isEmpty()) {
      break;
    }

    qint64 bytesWritten = dstFile.write(chunk);
    if (bytesWritten != chunk.size()) {
      emit errorOccurred(dst, "Failed to write to destination file");
      srcFile.close();
      dstFile.close();
      QFile::remove(dst);
      return false;
    }

    bytesCopied += bytesWritten;

    // バイト単位は補助情報として共有 m_progress に乗せる
    m_progress.currentFile = src;
    m_progress.processed   = bytesCopied;
    m_progress.total       = totalSize;
    emit progressUpdated(m_progress);
  }

  srcFile.close();
  dstFile.close();

  // Preserve file permissions
  QFile::setPermissions(dst, QFile::permissions(src));

  // 1 ファイル完了
  ++m_progress.filesDone;
  m_progress.processed = 0;
  m_progress.total     = -1;
  emit progressUpdated(m_progress);

  return true;
}

bool MoveWorker::copyDirectory(const QString& src, const QString& dst) {
  if (isCancelled()) {
    return false;
  }

  QDir srcDir(src);
  QDir dstDir(dst);

  // Create destination directory
  if (!dstDir.exists()) {
    if (!QDir().mkpath(dst)) {
      emit errorOccurred(dst, "Failed to create directory");
      return false;
    }
  }

  // Iterate through all entries in the directory
  QDirIterator it(src, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
  bool success = true;

  while (it.hasNext()) {
    if (isCancelled()) {
      return false;
    }

    QString srcPath = it.next();
    QFileInfo srcInfo(srcPath);
    QString relativePath = srcDir.relativeFilePath(srcPath);
    QString dstPath = dst + "/" + relativePath;

    m_progress.currentFile = srcPath;
    emit progressUpdated(m_progress);

    if (srcInfo.isDir()) {
      if (!copyDirectory(srcPath, dstPath)) {
        success = false;
      }
    } else {
      if (!copyFile(srcPath, dstPath)) {
        success = false;
      }
    }
  }

  return success;
}

bool MoveWorker::removeDirectory(const QString& path) {
  if (isCancelled()) {
    return false;
  }

  QDir dir(path);
  if (!dir.exists()) {
    return true;
  }

  // Remove all entries in the directory
  QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
  bool success = true;

  while (it.hasNext()) {
    if (isCancelled()) {
      return false;
    }

    QString entryPath = it.next();
    QFileInfo info(entryPath);

    if (info.isDir()) {
      if (!removeDirectory(entryPath)) {
        success = false;
      }
    } else {
      if (!QFile::remove(entryPath)) {
        emit errorOccurred(entryPath, "Failed to remove file");
        success = false;
      }
    }
  }

  // Remove the directory itself
  if (success && !dir.rmdir(path)) {
    emit errorOccurred(path, "Failed to remove directory");
    return false;
  }

  return success;
}

} // namespace Farman
