#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <atomic>
#include <memory>

class QTemporaryDir;

namespace Farman {

class PreviewPane;
class ArchiveContext;
class PropertiesWorker;

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
  // QTemporaryDir を unique_ptr で保持しているため、デストラクタは .cpp 側で
  // 完全型を見せた状態で生成する必要がある (前方宣言だけだと未定義型エラー)。
  ~PreviewController() override;

  // 左ペインのカーソル変化時に呼ばれる入口 (通常ファイル / ディレクトリ用)。
  //   filePath   : ディスク上の絶対パス
  //   displayPath: ステータス表示用 (空なら filePath 流用)
  //   isDirectory: ディレクトリにカーソルがある場合 true
  //   isDotDot   : ".." 擬似行
  //   fileSize   : ディスク上のサイズ (バイト)。ディレクトリのときは 0 でよい
  void requestPreview(const QString& filePath,
                      const QString& displayPath,
                      bool           isDirectory,
                      bool           isDotDot,
                      qint64         fileSize);

  // アーカイブ内エントリ向けの requestPreview。デバウンス窓内に一時展開して
  // 通常の prepareLoad 経路に流す。
  //   ctx        : 展開元のアーカイブコンテキスト (FileListModel が保有)
  //   entryPath  : アーカイブ内の相対パス (例: "inner/foo.txt")
  //   displayPath: ステータス表示用 (例: "/path/x.zip!/inner/foo.txt")
  //   uncompressedSize: エントリの元サイズ。previewMaxFileSizeBytes 判定に使う
  void requestArchivePreview(const ArchiveContext* ctx,
                             const QString&        entryPath,
                             const QString&        displayPath,
                             qint64                uncompressedSize);

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

  // アーカイブ内エントリの場合に追加でセットされる。
  // m_pendingArchiveCtx が non-null なら archive モード、null なら通常ファイル。
  const ArchiveContext* m_pendingArchiveCtx = nullptr;
  QString               m_pendingArchiveEntryPath;

  // 直近で実際に PreviewPane に流したパス。同じものを再要求された場合は
  // ロードをスキップしてチラつきを抑える。
  QString m_lastShownPath;

  // 世代カウンタ。requestPreview / clearPreview のたびにインクリメントする。
  // ワーカーは開始時の世代をキャプチャし、結果到着時に m_generation と一致
  // すれば適用、不一致なら破棄。スレッド間で読まれるので atomic。
  std::atomic<quint64> m_generation{0};

  // 直近 in-flight ジョブのキャンセルトークン。新しい requestPreview /
  // clearPreview が来た瞬間に *m_currentCancelToken = true をセットして、
  // prepareLoad の中の cancelToken チェックポイントで早期 return させる。
  // shared_ptr にする理由: ワーカースレッドがアクセス中に PreviewController が
  // 解放されても寿命が保証されるようにするため。
  std::shared_ptr<std::atomic<bool>> m_currentCancelToken;

  // アーカイブ内エントリを一時展開しておく作業ディレクトリ。
  // 初回アーカイブプレビュー要求時に lazy 生成し、PreviewController の
  // デストラクタで丸ごと削除される (= QTemporaryDir のセマンティクス)。
  // ファイル名は <archiveHash>/<entryHashPath> 形式で決定論的にし、
  // 同じエントリへの再要求では再展開を行わない。
  std::unique_ptr<QTemporaryDir> m_archiveTempDir;

  // ディレクトリプレビューで「再帰サイズ」を非同期集計するためのワーカ。
  // 同時に動かすのは常に 1 件のみ。次のディレクトリ / 通常ファイル要求が
  // 来たら requestCancel() + deleteLater でドロップする。
  // ファイルプレビュー時は m_dirSizeWorker は null。
  QPointer<PropertiesWorker> m_dirSizeWorker;
  // 進行中のディレクトリ集計ジョブの世代。古い結果が走り遅れて到着しても
  // 無視するため、PreviewController 全体の m_generation と同じパターン。
  std::atomic<quint64>       m_dirSizeGeneration{0};

  // ディレクトリ集計ワーカを cancel + deleteLater でドロップする。
  void cancelDirSizeWorker();
};

} // namespace Farman
