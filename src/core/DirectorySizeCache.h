#pragma once

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <atomic>

class QThread;

namespace Farman {

class DirectorySizeWorker;

// ディレクトリの再帰合計サイズを、単一 worker thread でバックグラウンド算出する
// シングルトン (ThumbnailCache と同じ構成)。結果は絶対パスをキーにメモリキャッシュ
// する。ファイル一覧の Size 列表示に使う。
//
// 使い方 (FileListModel):
//   qint64 bytes;
//   if (cache.peek(path, maxAgeMs, dirMtimeMs, &bytes)) {
//     // hit → そのまま表示に使う
//   } else {
//     // miss → プレースホルダ表示 + 非同期算出要求
//     cache.request(path);
//   }
//   // 後で sizeReady(path, bytes) が届いたら該当行を dataChanged で更新。
//
// 古いリクエストの破棄:
//   一覧のディレクトリ移動時に bumpGeneration() を呼ぶと、worker が走査に入る前
//   / 走査中のジョブが早期中断される。既存キャッシュは保持 (戻ったとき再利用)。
class DirectorySizeCache : public QObject {
  Q_OBJECT

public:
  static DirectorySizeCache& instance();

  // キャッシュ照合。以下を満たせば hit:
  //   - path のエントリが存在する
  //   - maxAgeMs < 0 なら経過時間を無視、そうでなければ (now - computedAt) <= maxAgeMs
  //   - dirMtimeMs < 0 なら mtime を無視、そうでなければ保存 mtime と一致
  // hit のとき *outBytes に合計バイト数を格納して true。
  bool peek(const QString& path, qint64 maxAgeMs, qint64 dirMtimeMs,
            qint64* outBytes) const;

  // 非同期算出を依頼する。キャッシュ済み / 投入済みならスキップ。
  // メインスレッドから呼ぶ前提。
  void request(const QString& path);

  // 世代カウンタを進めて、キュー上 / 走査中の古いジョブを破棄させる。
  // 既存キャッシュエントリは保持する。
  void bumpGeneration();

  // 現在いずれかのペインが表示しているディレクトリ path の集合 (union) を、参照
  // カウントで更新する。remove の各 path を -1 (0 で削除)、add の各 path を +1。
  // worker は走査を開始する前 / 走査中にこの集合を見て、どのペインも表示して
  // いない (= 参照カウント 0) path のジョブを中断する。これにより、大きな
  // ディレクトリの算出中に別ディレクトリへ移動しても、不要になったジョブを
  // 素早く打ち切って新しいディレクトリの算出に進める。左右ペインの union なので
  // 片方のペインの移動でもう一方のペインのジョブは巻き込まない。
  void updateWanted(const QSet<QString>& remove, const QSet<QString>& add);
  // path が現在いずれかのペインで表示されているか (参照カウント > 0)。worker から
  // 走査可否の判定に呼ばれる。
  bool isWanted(const QString& path) const;

  // 指定パスのキャッシュを無効化する (強制再算出用)。
  void invalidate(const QString& path);
  // 全キャッシュを破棄する。
  void invalidateAll();

signals:
  // メインスレッドで発火。受信側 (FileListModel) は path から該当行を特定して
  // Size 列を再描画する。
  void sizeReady(const QString& path, qint64 totalBytes);

private slots:
  // DirectorySizeWorker からの結果受信 (queued connection 経由)。
  void onWorkerDone(QString path, quint64 generation, qint64 totalBytes,
                    qint64 dirMtimeMs, bool ok);

private:
  DirectorySizeCache();
  ~DirectorySizeCache();
  DirectorySizeCache(const DirectorySizeCache&) = delete;
  DirectorySizeCache& operator=(const DirectorySizeCache&) = delete;

  // 1 エントリ = 算出結果。
  struct Entry {
    qint64 bytes        = 0;
    qint64 computedAtMs = 0;   // 算出完了時刻 (epoch ms)
    qint64 dirMtimeMs   = 0;   // 算出時のディレクトリ自身の mtime (ms)
  };

  // 内部: キャッシュ追加 + 件数上限の eviction (m_mutex 取得済み前提)。
  void insertLocked(const QString& path, const Entry& entry);

  QThread*             m_thread = nullptr;
  DirectorySizeWorker* m_worker = nullptr;

  // メインスレッド / worker 双方から触る共有状態は m_mutex で保護。
  mutable QMutex          m_mutex;
  QHash<QString, Entry>   m_cache;
  QList<QString>          m_lru;               // tail = 最新。件数上限の eviction 用。
  int                     m_maxEntries = 8192;

  // メインスレッドのみで触る。
  QSet<QString> m_inflight;

  // 現在いずれかのペインが表示しているディレクトリ path の参照カウント (union)。
  // メインスレッド (updateWanted) と worker スレッド (isWanted) の双方から触るため
  // m_mutex で保護する。
  QHash<QString, int> m_wanted;

  // worker と共有する世代カウンタ。走査中に値が変わったら中断される。
  std::atomic<quint64> m_generation{0};
};

} // namespace Farman
