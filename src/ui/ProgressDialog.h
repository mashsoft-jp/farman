#pragma once

#include "core/workers/WorkerBase.h"
#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

namespace Farman {

class ProgressDialog : public QDialog {
  Q_OBJECT

public:
  explicit ProgressDialog(
    const QString& operationName,
    QWidget*       parent = nullptr
  );

  void setWorker(WorkerBase* worker);

  // 「バックグラウンドで実行」ボタンの制御 (コピー / 移動の進捗ダイアログのみ)。
  // enableBackgroundOption(true) でボタンを出し、setBackgroundAvailable で押せる
  // かどうかを切り替える (バックグラウンド実行は同時に 1 つまでなので、他が
  // 実行中の間は押せない)。
  void enableBackgroundOption(bool enable);
  void setBackgroundAvailable(bool available);
  // バックグラウンドに回されたかどうか。回された後は、ステータスバーから
  // 再表示したときのボタンが「隠す」動作になり、完了時にも自動では閉じない
  // (閉じるかどうかは呼び出し側が結果を見て決める)。
  void setRunningInBackground(bool background);
  bool isRunningInBackground() const { return m_inBackground; }
  bool isWorkerFinished() const { return m_workerFinished; }
  bool isAutoCloseChecked() const { return m_autoCloseCheck->isChecked(); }

  // exec() がバックグラウンドへの切り替えで戻ったときの戻り値。
  static constexpr int BackgroundResult = 2;

signals:
  // 「バックグラウンドで実行」が押された。
  void backgroundRequested();

public slots:
  // 実行中に Esc / ウィンドウの閉じるボタンで閉じられると、処理だけが見えない
  // まま進んでしまうので、実行中は閉じない。バックグラウンド実行中に再表示した
  // ダイアログだけは、隠す (処理は続く) 動作にする。
  void reject() override;

private slots:
  void onProgressUpdated(const WorkerProgress& progress);
  void onFinished(bool success);
  void onCancel();

private:
  void setupUI(const QString& operationName);

  QLabel*       m_operationLabel;
  QLabel*       m_currentFileLabel;
  QProgressBar* m_fileProgressBar;   // 現在ファイル内のバイト進捗 (上段)
  QLabel*       m_fileByteLabel;     // "1.2 / 5.0 MB" を常に表示
  QLabel*       m_countLabel;        // "10 / 235 files" を常に表示
  QProgressBar* m_progressBar;       // ファイル数の進捗 (下段)
  QCheckBox*    m_autoCloseCheck;    // 完了時に自動で閉じるか
  QPushButton*  m_backgroundButton;  // バックグラウンドで実行 (既定は非表示)
  QPushButton*  m_cancelButton;
  WorkerBase*   m_worker;
  bool          m_inBackground   = false;
  bool          m_workerFinished = false;
};

} // namespace Farman
