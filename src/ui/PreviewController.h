#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>

namespace Farman {

class PreviewPane;

// プレビューモード時の「カーソル変化 → 右ペインのビュアー切替」を
// オーケストレートするコントローラ。
//
//   - デバウンスタイマで連打を吸収する (Settings::previewDebounceMs)
//   - タイマ発火時に QtConcurrent::run で prepareLoad をワーカースレッドへ
//     投げる。完了時は QFutureWatcher::finished で結果を受け、メインスレッドで
//     applyPreparedLoad を呼ぶ。
//   - 「読込中にカーソルが移動した場合は読込みを中断して次へ」要件は、
//     std::atomic<quint64> m_generation で実現する: requestPreview のたびに
//     世代をインクリメントし、結果到着時に開始時の世代と現在の世代が一致する
//     ときだけ画面に反映する (古い結果は捨てる)。
//     ※ prepareLoad 自体の途中キャンセルは Phase 3 で対応。Phase 2 では
//        「結果無視」だけだが、デバウンスと合わせて体感は十分軽い。
//   - 150ms 経過しても結果が返ってこない場合だけ Loading page を出す
//     (短時間で完了する小ファイルでチラつかせない)。
//   - ディレクトリ / 大ファイル / 非対応ファイルは早期に Unsupported 表示へ。
class PreviewController : public QObject {
  Q_OBJECT
public:
  PreviewController(PreviewPane* pane, QObject* parent = nullptr);
  ~PreviewController() override = default;

  // 左ペインのカーソル変化時に呼ばれる入口。
  //   filePath   : ディスク上の絶対パス (アーカイブ内ファイルは Phase 4 で対応)
  //   displayPath: ステータス表示用 (アーカイブ仮想パスなど、空なら filePath 流用)
  //   isDirectory: ディレクトリにカーソルがある場合 true
  //   isDotDot   : ".." 擬似行
  //   fileSize   : ディスク上のサイズ (バイト)。ディレクトリのときは 0 でよい
  void requestPreview(const QString& filePath,
                      const QString& displayPath,
                      bool           isDirectory,
                      bool           isDotDot,
                      qint64         fileSize);

  // 何も表示しない状態へ戻す (Preview レイアウトを抜けたとき等)。
  // 進行中のジョブは generation を 1 進めて結果を捨てさせる。
  void clearPreview();

private slots:
  // デバウンスタイマ発火時に呼ばれる。m_pending* を最新リクエストとして
  // 実行する。
  void onDebounceTimeout();

private:
  // 「実際に prepareLoad をワーカースレッドへ投げる」本体。
  // 受け取ったときに世代が一致していたら PreviewPane に流す。
  void startLoadJob(const QString& filePath, const QString& displayPath);

  PreviewPane* m_pane = nullptr;
  QTimer       m_debounceTimer;

  // 次の発火時に表示すべき要求 (デバウンス窓の最後に上書きされていく)。
  QString m_pendingFilePath;
  QString m_pendingDisplayPath;
  bool    m_pendingIsDirectory = false;
  bool    m_pendingIsDotDot    = false;
  qint64  m_pendingFileSize    = 0;
  bool    m_hasPending         = false;

  // 直近で実際に PreviewPane に流したパス。同じものを再要求された場合は
  // ロードをスキップしてチラつきを抑える。
  QString m_lastShownPath;

  // 世代カウンタ。requestPreview / clearPreview のたびにインクリメントする。
  // ワーカーは開始時の世代をキャプチャし、結果到着時に m_generation と一致
  // すれば適用、不一致なら破棄。スレッド間で読まれるので atomic。
  std::atomic<quint64> m_generation{0};
};

} // namespace Farman
