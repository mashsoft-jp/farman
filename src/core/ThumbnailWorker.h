#pragma once

#include "ThumbnailCache.h"

#include <QObject>
#include <QPixmap>
#include <atomic>

namespace Farman {

// サムネイルの実 decode を別スレッドで実行するワーカー。ThumbnailCache が
// QThread に moveToThread して保持する。Queued connection 経由で process()
// が起動され、生成した pixmap を done シグナルで返す。
//
// 世代カウンタ (generation):
//   process() 開始時に「呼出時 generation」と「現在 generation (cache が保持)」
//   を比較し、不一致なら decode をスキップする。ディレクトリ移動などで古い
//   リクエストを高速に破棄するための仕組み。
class ThumbnailWorker : public QObject {
  Q_OBJECT

public:
  explicit ThumbnailWorker(std::atomic<quint64>* currentGen, QObject* parent = nullptr);

public slots:
  // メインスレッドから queued connection で呼ばれる。requestGen は呼出時の
  // 世代カウンタで、現在のカウンタ (m_currentGen) と一致するときだけ decode する。
  void process(Farman::ThumbnailKey key, quint64 requestGen);

signals:
  // decode 結果 (queued connection でメインスレッドへ届く)。
  // 失敗 (decode 不能) のときも null pixmap で発火する (受信側は無視する)。
  void done(Farman::ThumbnailKey key, quint64 generation, QPixmap pixmap);

private:
  std::atomic<quint64>* m_currentGen;
};

} // namespace Farman
