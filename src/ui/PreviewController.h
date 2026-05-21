#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace Farman {

class PreviewPane;

// プレビューモード時の「カーソル変化 → 右ペインのビュアー切替」を
// オーケストレートするコントローラ。
//
// Phase 1 (本ファイル):
//   - デバウンスタイマで連打を吸収する (Settings::previewDebounceMs)
//   - タイマ発火時にメインスレッドで prepareLoad を呼んで applyPreparedLoad
//     を即時実行する (= 簡易同期版)
//   - ディレクトリ / 大ファイル / 非対応ファイルは早期に Unsupported 表示へ
//
// Phase 2 で QtConcurrent::run + 世代カウンタによる非同期化に置換予定。
class PreviewController : public QObject {
  Q_OBJECT
public:
  PreviewController(PreviewPane* pane, QObject* parent = nullptr);
  ~PreviewController() override = default;

  // 左ペインのカーソル変化時に呼ばれる入口。
  //   filePath   : ディスク上の絶対パス (アーカイブ内ファイルは Phase 1 では未対応)
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
  void clearPreview();

private slots:
  // デバウンスタイマ発火時に呼ばれる。m_pending* を最新リクエストとして
  // 実行する。
  void onDebounceTimeout();

private:
  // 「実際に prepareLoad を呼んで PreviewPane に表示する」本体。
  void loadAndShow(const QString& filePath, const QString& displayPath);

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
};

} // namespace Farman
