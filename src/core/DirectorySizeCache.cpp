#include "DirectorySizeCache.h"
#include "DirectorySizeWorker.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QThread>

namespace Farman {

DirectorySizeCache& DirectorySizeCache::instance() {
  static DirectorySizeCache cache;
  return cache;
}

DirectorySizeCache::DirectorySizeCache() {
  m_thread = new QThread();
  m_thread->setObjectName(QStringLiteral("DirSizeWorker"));
  m_worker = new DirectorySizeWorker(&m_generation);
  m_worker->moveToThread(m_thread);

  // worker → cache (メインスレッド) の結果受信 (queued)。
  connect(m_worker, &DirectorySizeWorker::done,
          this,     &DirectorySizeCache::onWorkerDone,
          Qt::QueuedConnection);
  // app 終了時のクリーンアップ。
  connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

  m_thread->start();
}

DirectorySizeCache::~DirectorySizeCache() {
  if (m_thread) {
    // 走査中ジョブを速やかに中断させてから終了する。
    m_generation.fetch_add(1);
    m_thread->quit();
    m_thread->wait();
    delete m_thread;
  }
}

bool DirectorySizeCache::peek(const QString& path, qint64 maxAgeMs,
                              qint64 dirMtimeMs, qint64* outBytes) const {
  QMutexLocker lock(&m_mutex);
  auto it = m_cache.constFind(path);
  if (it == m_cache.cend()) {
    return false;
  }
  const Entry& e = it.value();
  if (maxAgeMs >= 0) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - e.computedAtMs > maxAgeMs) {
      return false;  // 期限切れ
    }
  }
  if (dirMtimeMs >= 0 && e.dirMtimeMs != dirMtimeMs) {
    return false;  // ディレクトリが更新された
  }
  if (outBytes) {
    *outBytes = e.bytes;
  }
  return true;
}

void DirectorySizeCache::request(const QString& path) {
  if (m_inflight.contains(path)) {
    return;  // 既に投入済み
  }
  m_inflight.insert(path);

  const quint64 gen = m_generation.load();
  QMetaObject::invokeMethod(m_worker, "process", Qt::QueuedConnection,
                            Q_ARG(QString, path),
                            Q_ARG(quint64, gen));
}

void DirectorySizeCache::bumpGeneration() {
  m_generation.fetch_add(1);
  // inflight は結果受信で消える。古い世代の結果は onWorkerDone 側で ok=false /
  // path 不在として扱われる。
}

void DirectorySizeCache::invalidate(const QString& path) {
  QMutexLocker lock(&m_mutex);
  if (m_cache.remove(path) > 0) {
    m_lru.removeAll(path);
  }
}

void DirectorySizeCache::invalidateAll() {
  QMutexLocker lock(&m_mutex);
  m_cache.clear();
  m_lru.clear();
}

void DirectorySizeCache::onWorkerDone(QString path, quint64 generation,
                                      qint64 totalBytes, qint64 dirMtimeMs,
                                      bool ok) {
  Q_UNUSED(generation);
  m_inflight.remove(path);

  if (!ok) {
    // 中断 / エラーはキャッシュしない。受信側にも通知しない (プレースホルダの
    // まま。次回一覧表示で再要求される)。
    return;
  }

  {
    QMutexLocker lock(&m_mutex);
    Entry e;
    e.bytes        = totalBytes;
    e.computedAtMs = QDateTime::currentMSecsSinceEpoch();
    e.dirMtimeMs   = dirMtimeMs;
    insertLocked(path, e);
  }

  // 受信側 (FileListModel) に通知。現ディレクトリ外の path なら受信側で無視される。
  emit sizeReady(path, totalBytes);
}

void DirectorySizeCache::insertLocked(const QString& path, const Entry& entry) {
  auto existing = m_cache.find(path);
  if (existing != m_cache.end()) {
    existing.value() = entry;
    m_lru.removeAll(path);
    m_lru.append(path);
    return;
  }
  m_cache.insert(path, entry);
  m_lru.append(path);
  // 件数上限を超えたら最古から捨てる (エントリは小さいので件数で管理)。
  while (m_lru.size() > m_maxEntries) {
    const QString old = m_lru.takeFirst();
    m_cache.remove(old);
  }
}

} // namespace Farman
