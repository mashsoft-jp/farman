#include "DirectorySizeWorker.h"
#include "DirectorySizeCache.h"

#include <QDir>
#include <QFileInfo>

namespace Farman {

DirectorySizeWorker::DirectorySizeWorker(std::atomic<quint64>* currentGen,
                                         DirectorySizeCache* cache,
                                         QObject* parent)
  : QObject(parent), m_currentGen(currentGen), m_cache(cache) {}

bool DirectorySizeWorker::aborted(quint64 requestGen,
                                  const QString& topPath) const {
  // 終了時の一括中断 (generation 不一致) か、対象ディレクトリがどのペインでも
  // 表示されなくなった (移動でキャンセル) とき中断する。
  if (m_currentGen && m_currentGen->load() != requestGen) {
    return true;
  }
  if (m_cache && !m_cache->isWanted(topPath)) {
    return true;
  }
  return false;
}

void DirectorySizeWorker::process(QString dirPath, quint64 requestGen) {
  // 開始前に確認 (キュー滞留中に別ディレクトリへ移った場合の早期スキップ)。
  // ここで isWanted=false なら、走査に一切入らずに即スキップして次のジョブへ。
  if (aborted(requestGen, dirPath)) {
    emit done(dirPath, requestGen, 0, 0, false);
    return;
  }

  const QFileInfo dirInfo(dirPath);
  const qint64 dirMtimeMs =
    dirInfo.lastModified().isValid()
      ? dirInfo.lastModified().toMSecsSinceEpoch()
      : 0;

  qint64 total = 0;
  const bool ok = scan(dirPath, total, requestGen, dirPath);
  emit done(dirPath, requestGen, ok ? total : 0, dirMtimeMs, ok);
}

bool DirectorySizeWorker::scan(const QString& dirPath, qint64& totalBytes,
                               quint64 requestGen, const QString& topPath) {
  if (aborted(requestGen, topPath)) {
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
    // 走査は重くなり得るので、256 件ごとに中断判定 (generation / isWanted)。
    if ((++m_checkCounter & 0xFF) == 0 && aborted(requestGen, topPath)) {
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
    if (aborted(requestGen, topPath)) {
      return false;
    }
    if (fi.isSymLink()) {
      continue;  // ループ回避
    }
    if (!scan(fi.absoluteFilePath(), totalBytes, requestGen, topPath)) {
      return false;
    }
  }
  return true;
}

} // namespace Farman
