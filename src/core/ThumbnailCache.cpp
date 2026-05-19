#include "ThumbnailCache.h"
#include "ThumbnailWorker.h"

#include <QMetaType>
#include <QMutexLocker>
#include <QThread>

namespace Farman {

ThumbnailCache& ThumbnailCache::instance() {
  static ThumbnailCache cache;
  return cache;
}

ThumbnailCache::ThumbnailCache() {
  // ThumbnailKey を QMetaType に登録 (queued signal/slot 経由で値渡しするため)。
  qRegisterMetaType<ThumbnailKey>("Farman::ThumbnailKey");

  m_thread = new QThread();
  m_thread->setObjectName(QStringLiteral("ThumbnailWorker"));
  m_worker = new ThumbnailWorker(&m_generation);
  m_worker->moveToThread(m_thread);

  // worker → cache (メインスレッド) の結果受信 (queued)。
  connect(m_worker, &ThumbnailWorker::done,
          this,     &ThumbnailCache::onWorkerDone,
          Qt::QueuedConnection);
  // app 終了時のクリーンアップ
  connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

  m_thread->start();
}

ThumbnailCache::~ThumbnailCache() {
  if (m_thread) {
    m_thread->quit();
    m_thread->wait();
    delete m_thread;
  }
}

bool ThumbnailCache::peek(const ThumbnailKey& key, QPixmap* outPixmap) const {
  QMutexLocker lock(&m_mutex);
  auto it = m_cache.constFind(key);
  if (it == m_cache.cend()) return false;
  if (outPixmap) *outPixmap = it.value();
  // const なので touch しないが、本質的にはここで LRU を最新化したい。
  // const_cast は避けて、リクエスト側で必要なら touch する API を別途用意する。
  return true;
}

void ThumbnailCache::request(const ThumbnailKey& key) {
  {
    QMutexLocker lock(&m_mutex);
    if (m_cache.contains(key)) {
      // hit のついでに LRU を最新化
      touchLocked(key);
      return;
    }
  }
  if (m_inflight.contains(key)) return;  // 既に投げ済み
  m_inflight.insert(key);

  const quint64 gen = m_generation.load();
  // worker のスロットを queued で起動。worker thread のイベントループで処理。
  QMetaObject::invokeMethod(m_worker, "process", Qt::QueuedConnection,
                            Q_ARG(Farman::ThumbnailKey, key),
                            Q_ARG(quint64, gen));
}

void ThumbnailCache::bumpGeneration() {
  m_generation.fetch_add(1);
  // inflight は受信で消えるのでここでは触らない (古い結果が届いたら受信時に
  // generation を見て無視する経路)。
}

void ThumbnailCache::setCacheLimitBytes(qint64 bytes) {
  QMutexLocker lock(&m_mutex);
  m_cacheBytesMax = bytes;
  // 即 eviction
  while (m_cacheBytes > m_cacheBytesMax && !m_lru.isEmpty()) {
    const ThumbnailKey old = m_lru.takeFirst();
    auto it = m_cache.find(old);
    if (it != m_cache.end()) {
      m_cacheBytes -= estimateBytes(it.value());
      m_cache.erase(it);
    }
  }
}

void ThumbnailCache::onWorkerDone(ThumbnailKey key, quint64 generation,
                                   QPixmap pixmap) {
  m_inflight.remove(key);

  // 結果は generation を問わず cache に入れる (decode コストを捨てない)。
  // ただし pixmap が null (decode 失敗 / 古い世代スキップ) はキャッシュしない。
  if (pixmap.isNull()) return;

  {
    QMutexLocker lock(&m_mutex);
    insertLocked(key, pixmap);
  }

  // 受信側 (FileListModel) に通知。古い世代の結果でも path が現ディレクトリに
  // あれば dataChanged で正常に描画される (世代を見て無視する判断は emit せず
  // 受信側にも任せず、ここで素直に流して良い)。
  emit thumbnailReady(key, pixmap);
  // generation は将来チェック用に残しておくが今は未使用
  Q_UNUSED(generation);
}

void ThumbnailCache::insertLocked(const ThumbnailKey& key, const QPixmap& pixmap) {
  auto existing = m_cache.find(key);
  if (existing != m_cache.end()) {
    m_cacheBytes -= estimateBytes(existing.value());
    existing.value() = pixmap;
    m_cacheBytes += estimateBytes(pixmap);
    touchLocked(key);
  } else {
    m_cache.insert(key, pixmap);
    m_lru.append(key);
    m_cacheBytes += estimateBytes(pixmap);
  }
  // eviction
  while (m_cacheBytes > m_cacheBytesMax && m_lru.size() > 1) {
    const ThumbnailKey old = m_lru.takeFirst();
    if (old == key) {
      // 自分が tail に来ているはずだが、念のため。head が自分なら end に戻す。
      m_lru.append(old);
      break;
    }
    auto it = m_cache.find(old);
    if (it != m_cache.end()) {
      m_cacheBytes -= estimateBytes(it.value());
      m_cache.erase(it);
    }
  }
}

void ThumbnailCache::touchLocked(const ThumbnailKey& key) {
  m_lru.removeAll(key);
  m_lru.append(key);
}

qint64 ThumbnailCache::estimateBytes(const QPixmap& pixmap) {
  return static_cast<qint64>(pixmap.width()) * pixmap.height() * 4;
}

} // namespace Farman
