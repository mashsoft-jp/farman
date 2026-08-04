#include "DirectorySizeWorker.h"

#include <QDir>
#include <QFileInfo>

namespace Farman {

DirectorySizeWorker::DirectorySizeWorker(std::atomic<quint64>* currentGen,
                                         QObject* parent)
  : QObject(parent), m_currentGen(currentGen) {}

bool DirectorySizeWorker::aborted(quint64 requestGen) const {
  return m_currentGen && m_currentGen->load() != requestGen;
}

void DirectorySizeWorker::process(QString dirPath, quint64 requestGen) {
  // 開始前に世代確認 (キュー滞留中に別ディレクトリへ移った場合の早期スキップ)。
  if (aborted(requestGen)) {
    emit done(dirPath, requestGen, 0, 0, false);
    return;
  }

  const QFileInfo dirInfo(dirPath);
  const qint64 dirMtimeMs =
    dirInfo.lastModified().isValid()
      ? dirInfo.lastModified().toMSecsSinceEpoch()
      : 0;

  qint64 total = 0;
  const bool ok = scan(dirPath, total, requestGen);
  emit done(dirPath, requestGen, ok ? total : 0, dirMtimeMs, ok);
}

bool DirectorySizeWorker::scan(const QString& dirPath, qint64& totalBytes,
                               quint64 requestGen) {
  if (aborted(requestGen)) {
    return false;
  }

  QDir dir(dirPath);
  if (!dir.exists()) {
    return true;  // 消えていても 0 加算で正常終了扱い
  }

  // ファイル: サイズを加算 (シンボリックリンクは加算しない)。
  const QFileInfoList files = dir.entryInfoList(
    QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
  for (const QFileInfo& fi : files) {
    // 走査は重くなり得るので、256 件ごとに generation を確認して打ち切る。
    if ((++m_checkCounter & 0xFF) == 0 && aborted(requestGen)) {
      return false;
    }
    if (fi.isSymLink()) {
      continue;
    }
    totalBytes += fi.size();
  }

  // サブディレクトリを再帰。
  const QFileInfoList subs = dir.entryInfoList(
    QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
  for (const QFileInfo& fi : subs) {
    if (aborted(requestGen)) {
      return false;
    }
    if (fi.isSymLink()) {
      continue;  // ループ回避
    }
    if (!scan(fi.absoluteFilePath(), totalBytes, requestGen)) {
      return false;
    }
  }
  return true;
}

} // namespace Farman
