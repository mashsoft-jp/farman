#pragma once

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <atomic>

class QThread;

namespace Farman {

class ThumbnailWorker;

// サムネイル 1 枚を特定するキー。
// (path, mtimeMs, sizePx) の 3 つ揃いで一意に決まり、ファイル更新や size 切替で
// 自動的に別エントリ扱いになる。
struct ThumbnailKey {
  QString path;
  qint64  mtimeMs = 0;
  int     sizePx  = 0;

  bool operator==(const ThumbnailKey& other) const {
    return sizePx == other.sizePx && mtimeMs == other.mtimeMs && path == other.path;
  }
};

inline size_t qHash(const ThumbnailKey& k, size_t seed = 0) noexcept {
  return qHashMulti(seed, k.path, k.mtimeMs, k.sizePx);
}

// サムネイル画像の LRU メモリキャッシュ + 単一 worker thread の管理を持つ
// シングルトン。
//
// 使い方:
//   if (auto pm = ThumbnailCache::instance().peek(key)) {
//     // cache hit、そのまま描画に使う
//   } else {
//     // miss、placeholder で描画 + 非同期 decode 要求
//     ThumbnailCache::instance().request(key);
//   }
//   // 後で thumbnailReady シグナルが届いたら dataChanged で再描画。
//
// 古いリクエストの破棄:
//   ディレクトリ移動 / モード切替時に bumpGeneration() を呼ぶと、worker が
//   実 decode に入る前のジョブはスキップされる。decode 中のジョブは結果を
//   そのまま cache に入れる (捨てると CPU が無駄)。受信側 (FileListModel) も
//   path が現ディレクトリ外なら無視するため、UI には影響しない。
//
// LRU eviction:
//   pixmap の width*height*4 byte 推定で上限 (default 64MB) を超えると最古
//   エントリから捨てる。
class ThumbnailCache : public QObject {
  Q_OBJECT

public:
  static ThumbnailCache& instance();

  // cache hit なら *outPixmap に格納して true。miss / inflight なら false。
  // outPixmap が nullptr の場合は存在チェックだけして bool を返す。
  bool peek(const ThumbnailKey& key, QPixmap* outPixmap) const;

  // 非同期 decode を依頼する。cache hit / inflight ならスキップ。
  // メインスレッドから呼ぶ前提 (worker への queued invoke は内部で行う)。
  void request(const ThumbnailKey& key);

  // 世代カウンタを上げて、worker が拾うキュー上の古いリクエストを破棄する。
  // 既存 cache エントリは保持 (戻り直したときに再利用できる)。
  void bumpGeneration();

  // メモリ上限 (bytes) を設定。デフォルトは 64MB。
  void setCacheLimitBytes(qint64 bytes);
  qint64 cacheLimitBytes() const { return m_cacheBytesMax; }

signals:
  // メインスレッドで発火。サブスクライバ (FileListModel) は key.path から
  // 該当 row を特定して dataChanged を emit する。
  void thumbnailReady(const ThumbnailKey& key, const QPixmap& pixmap);

private slots:
  // ThumbnailWorker からの結果受信 (queued connection 経由でメインスレッド側へ)。
  void onWorkerDone(ThumbnailKey key, quint64 generation, QPixmap pixmap);

private:
  ThumbnailCache();
  ~ThumbnailCache();
  ThumbnailCache(const ThumbnailCache&) = delete;
  ThumbnailCache& operator=(const ThumbnailCache&) = delete;

  // 内部: cache 追加 + LRU eviction (m_mutex 取得済み前提)。
  void insertLocked(const ThumbnailKey& key, const QPixmap& pixmap);
  // 内部: LRU の更新 (touch、最新位置に移動)。
  void touchLocked(const ThumbnailKey& key);
  // 内部: pixmap のメモリ消費見積もり (width*height*4)。
  static qint64 estimateBytes(const QPixmap& pixmap);

  QThread*          m_thread = nullptr;
  ThumbnailWorker*  m_worker = nullptr;

  // メインスレッドからも worker からも触る共有状態は m_mutex で保護。
  mutable QMutex                m_mutex;
  QHash<ThumbnailKey, QPixmap>  m_cache;
  QList<ThumbnailKey>           m_lru;        // tail = 最新
  qint64                         m_cacheBytes    = 0;
  qint64                         m_cacheBytesMax = 64LL * 1024 * 1024;

  // メインスレッドのみで触る (request / 受信側が触る)。
  QSet<ThumbnailKey> m_inflight;

  // generation は worker と request 側で共有。worker は process() で
  // 現在の generation と比較して、合わなければ早期 return する。
  std::atomic<quint64> m_generation{0};
};

} // namespace Farman

Q_DECLARE_METATYPE(Farman::ThumbnailKey)
