#pragma once

#include <QEventLoop>
#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QWidget>

#include <atomic>
#include <memory>

class QLabel;
class QProgressBar;
class QPushButton;

namespace Farman {

// 各 External ViewerWindow / Inline ViewerPanel が表示する「読み込み中…」
// プレースホルダ。ファイル名 / サイズ / プログレスバー + Cancel ボタン。
//
//   - setForFile(filePath) で表示用テキストを書き換え、resetToken() で
//     新しい cancel token を発行する。
//   - Cancel ボタンクリック (Enter で default ボタンとして発火) もしくは
//     keyPressEvent で Esc を受けたら `cancelled()` シグナルを emit。
//     呼び出し側は close() なり showFileManager() なりに繋ぐ。
//   - 表示直後に Cancel ボタンへフォーカスが当たるので、Enter で即座に
//     キャンセル → ファイラに戻れる。
class CancellableLoadPage : public QWidget {
  Q_OBJECT
public:
  explicit CancellableLoadPage(QWidget* parent = nullptr);

  // タイトル / 詳細行 (ファイル名 + サイズ + ディレクトリ) を再描画する。
  void setForFile(const QString& filePath);

  // 新しい cancel token を発行して保持する。prepareLoad に raw ポインタを
  // 渡せるよう shared_ptr で返す (呼び出し側で生存期間を保証)。
  // 副作用: Cancel ボタンを enable + フォーカスを当てる + 強制再描画。
  std::shared_ptr<std::atomic<bool>> resetToken();

  // 進行中のロードを取り消す。token が有効なら true をセットして、
  // worker (prepareLoad) を早期 return させる。cancelled() シグナルも emit。
  void cancel();

signals:
  // Cancel ボタンが押された / Esc が押された。
  void cancelled();

protected:
  void keyPressEvent(QKeyEvent* event) override;

private:
  QLabel*       m_title       = nullptr;
  QLabel*       m_detail      = nullptr;
  QProgressBar* m_bar         = nullptr;
  QPushButton*  m_cancelBtn   = nullptr;

  std::shared_ptr<std::atomic<bool>> m_token;
};

// QFuture が終わるまでイベントループを回しつつ待つ。Cancel ボタンや
// ユーザー入力イベントは生かしておく (= キャンセル操作だけが届く)。
// QApplication::sendPostedEvents / processEvents を回す前提なので、
// 必ず UI スレッドから呼ぶこと。
//
// 注意: ここでは worker のキャンセルは呼び出し側が行う (ViewerPanel /
// ViewerWindow が CancellableLoadPage::cancelled() に繋ぐ)。本関数は
// 単に Future の完了を待つだけ。
template <typename T>
T waitForFutureWithEventLoop(QFuture<T> future) {
  QFutureWatcher<T> watcher;
  QEventLoop        loop;
  QObject::connect(&watcher, &QFutureWatcher<T>::finished,
                   &loop,    &QEventLoop::quit);
  watcher.setFuture(future);
  if (!future.isFinished()) {
    // ExcludeUserInputEvents だと Cancel ボタンのクリック / Esc キーも
    // 食われてしまうので、AllEvents で回す。worker をキャンセルしたい
    // ユーザー操作だけは受け付けたいので、入力イベントも通す。
    loop.exec();
  }
  return watcher.result();
}

// ビュアーのロード結末を Logger に流す共通ヘルパ。Inline (ViewerPanel) も
// External (各 *ViewerWindow) も呼ぶ。
//   - ok=true   → "Viewer loaded (<kind>): <path>"           (Info)
//   - cancelled → "Viewer load cancelled (<kind>): <path>"   (Info)
//   - else      → "Viewer load failed (<kind>): <path>"      (Warn)
// kindLabel は "Text" / "Image" / "Binary" / "Markdown" / "PDF" / "CSV"
// (必要なら "Text (external)" のように外側ウィンドウ識別を付け足す)。
void logViewerLoadResult(const QString& kindLabel,
                          const QString& displayPath,
                          bool           ok,
                          bool           cancelled);

} // namespace Farman
