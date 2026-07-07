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
  QPushButton*  m_cancelButton;
  WorkerBase*   m_worker;
};

} // namespace Farman
