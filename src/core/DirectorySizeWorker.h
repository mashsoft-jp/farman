#pragma once

#include <QObject>
#include <QString>
#include <atomic>

namespace Farman {

class DirectorySizeCache;

// 1 ディレクトリの配下を再帰集計して合計バイト数を求めるワーカー。
// DirectorySizeCache が単一 worker thread 上で保持し、process() を queued
// 起動する (ThumbnailWorker と同じ構成)。シンボリックリンクは追跡しない
// (無限ループ / 二重計上回避)。集計ロジックは PropertiesWorker を踏襲。
//
// 走査は重くなり得るので、一定件数ごとに現在の generation を確認し、ディレクトリ
// 移動などで generation が進んでいたら早期に打ち切って done(ok=false) を返す。
class DirectorySizeWorker : public QObject {
  Q_OBJECT

public:
  // currentGen: DirectorySizeCache が保持する世代カウンタへのポインタ (アプリ
  //   終了時の一括中断に使う)。process() の requestGen と一致しなくなったら中断。
  // cache: 走査対象 path が今もいずれかのペインで表示されているか (isWanted) を
  //   問い合わせるため。どのペインも表示していない path のジョブは中断する。
  DirectorySizeWorker(std::atomic<quint64>* currentGen,
                      DirectorySizeCache* cache,
                      QObject* parent = nullptr);

public slots:
  // dirPath 配下を再帰集計する。worker thread のイベントループで実行される。
  void process(QString dirPath, quint64 requestGen);

signals:
  // 集計完了 (または中断) を通知。ok=false は中断 / エラーで、受信側はキャッシュ
  // しない。dirMtimeMs は走査開始時点の dirPath 自身の最終更新時刻 (キャッシュ
  // 無効化の照合に使う)。
  void done(QString dirPath, quint64 requestGen, qint64 totalBytes,
            qint64 dirMtimeMs, bool ok);

private:
  // dirPath 配下を再帰加算する。中断されたら false を返す。topPath は今回の
  // 算出対象 (ペインに表示されているディレクトリ) で、中断判定の isWanted に使う。
  bool scan(const QString& dirPath, qint64& totalBytes, quint64 requestGen,
            const QString& topPath);
  // 打ち切り判定: generation 不一致 (終了時) か、topPath がどのペインでも
  // 表示されなくなった (移動でキャンセル) とき true。
  bool aborted(quint64 requestGen, const QString& topPath) const;

  std::atomic<quint64>* m_currentGen = nullptr;
  DirectorySizeCache*   m_cache      = nullptr;
  // generation / isWanted 確認を間引くためのカウンタ。
  int m_checkCounter = 0;
};

} // namespace Farman
