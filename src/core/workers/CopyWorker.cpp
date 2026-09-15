#include "CopyWorker.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>

namespace Farman {

namespace {

// コピー先がコピー元自身、もしくはコピー元配下に入っているかを判定する。
// 例: `~/foo` を `~/foo/bar` にコピー → bar が新規作成された瞬間に列挙対象に
// 加わって再帰展開してしまう。コピー先を src 自身に指定した場合も同様。
// canonicalFilePath はパスが実在しないと空になるので、その場合は clean な
// absolute パスで比較する (シンボリックリンクは追跡できなくなるがフォールバック)。
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

} // namespace

CopyWorker::CopyWorker(
  const QStringList& srcPaths,
  const QString&     dstDir,
  QObject*           parent)
  : WorkerBase(parent)
  , m_srcPaths(srcPaths)
  , m_dstDir(dstDir) {
}

void CopyWorker::run() {
  bool success = true;

  // 事前スキャンで「全ファイル数」を把握する。トップレベルだけだと
  // ディレクトリを 1 件選んだとき "0/1 files" のまま終盤まで進まない。
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

  // Copy each source path
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

    // ディレクトリを自分自身 / 配下にコピーしようとしている場合は拒否する。
    // (放置するとコピー先が列挙対象に加わって再帰展開してしまう)。
    if (srcInfo.isDir() && isDestinationInsideSource(srcPath, m_dstDir)) {
      emit errorOccurred(srcPath,
        "Destination is inside the source directory");
      success = false;
      continue;
    }

    QString dstPath = m_dstDir + "/" + srcInfo.fileName();

    // トップレベルの競合解決（ディレクトリ or ファイル）
    if (QFileInfo::exists(dstPath)) {
      OverwriteResolution resolution = resolveOverwrite(srcPath, dstPath);
      if (resolution.action == OverwriteResolution::Action::Cancel) {
        requestCancel();
        emit finished(false);
        return;
      }
      if (resolution.action == OverwriteResolution::Action::Skip) {
        // このエントリは飛ばす。配下のファイル数ぶん filesDone を進めて
        // 進捗が 100% で終わるようにする。
        m_progress.filesDone += countAllFiles({srcPath});
        emit progressUpdated(m_progress);
        continue;
      }
      if (resolution.action == OverwriteResolution::Action::Rename) {
        dstPath = resolution.targetPath;
      } else {
        // Overwrite: 既存を削除（dir は再帰的に）
        QFileInfo dstInfo(dstPath);
        if (dstInfo.isDir()) {
          QDir(dstPath).removeRecursively();
        } else {
          QFile::remove(dstPath);
        }
      }
    }

    m_progress.currentFile = srcPath;
    emit progressUpdated(m_progress);

    if (!copyEntry(srcPath, dstPath)) {
      success = false;
      continue;
    }
    // ファイル単位の filesDone++ は copyFile() の中で行う。
    // ディレクトリのトップレベルではここで増やす必要なし。
  }

  emit finished(success);
}

bool CopyWorker::copyEntry(const QString& src, const QString& dst) {
  QFileInfo srcInfo(src);

  if (srcInfo.isDir()) {
    return copyDirectory(src, dst);
  } else {
    return copyFile(src, dst);
  }
}

bool CopyWorker::copyFile(const QString& src, const QString& dstIn) {
  if (isCancelled()) {
    return false;
  }

  QString dst = dstIn;
  if (QFileInfo::exists(dst)) {
    OverwriteResolution resolution = resolveOverwrite(src, dst);
    if (resolution.action == OverwriteResolution::Action::Cancel) {
      requestCancel();
      return false;
    }
    if (resolution.action == OverwriteResolution::Action::Skip) {
      // 飛ばしたファイルも「処理済み」として数える (失敗扱いにはしない)
      ++m_progress.filesDone;
      emit progressUpdated(m_progress);
      return true;
    }
    if (resolution.action == OverwriteResolution::Action::Rename) {
      dst = resolution.targetPath;
    } else {
      if (!QFile::remove(dst)) {
        emit errorOccurred(dst, "Failed to remove existing file");
        return false;
      }
    }
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

    // バイト単位の途中進捗も流すが、filesDone/filesTotal は共有値を保つ。
    // (進捗ダイアログは filesTotal>0 で「N/M files」表示にしているので
    //  バイト進捗は currentFile ラベルの参考情報として扱われる。)
    m_progress.currentFile = src;
    m_progress.processed   = bytesCopied;
    m_progress.total       = totalSize;
    emit progressUpdated(m_progress);
  }

  srcFile.close();
  dstFile.close();

  // Preserve file permissions
  QFile::setPermissions(dst, QFile::permissions(src));

  // 1 ファイル完了したので filesDone を進める
  ++m_progress.filesDone;
  m_progress.processed = 0;
  m_progress.total     = -1;
  emit progressUpdated(m_progress);

  return true;
}

bool CopyWorker::copyDirectory(const QString& src, const QString& dst) {
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

    // 全体の filesDone/filesTotal はそのまま、currentFile だけ更新する
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

} // namespace Farman
