#include "ProgressDialog.h"
#include "settings/Settings.h"
#include "utils/Dialogs.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>

namespace Farman {

ProgressDialog::ProgressDialog(
  const QString& operationName,
  QWidget*       parent)
  : QDialog(parent)
  , m_worker(nullptr) {

  setupUI(operationName);
  setModal(true);
  setMinimumWidth(400);
}

void ProgressDialog::setupUI(const QString& operationName) {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Operation label
  m_operationLabel = new QLabel(operationName, this);
  QFont boldFont = m_operationLabel->font();
  boldFont.setBold(true);
  m_operationLabel->setFont(boldFont);
  mainLayout->addWidget(m_operationLabel);

  // Current file label
  m_currentFileLabel = new QLabel("", this);
  m_currentFileLabel->setWordWrap(true);
  mainLayout->addWidget(m_currentFileLabel);

  // 上段: 現在処理中のファイル 1 個の中でのバイト進捗。
  m_fileProgressBar = new QProgressBar(this);
  m_fileProgressBar->setRange(0, 100);
  m_fileProgressBar->setValue(0);
  mainLayout->addWidget(m_fileProgressBar);

  // バイト量ラベル ("1.2 / 5.0 MB")。macOS はバー上の文字を出さないので別ラベル。
  m_fileByteLabel = new QLabel(QString(), this);
  m_fileByteLabel->setAlignment(Qt::AlignRight);
  mainLayout->addWidget(m_fileByteLabel);

  // 上段バーはバイト進捗 (total>0) を報告する操作 (コピー / 移動) でのみ表示。
  // 削除など 1 ファイルを一括処理する操作はバイト進捗を持たないので隠す。
  m_fileProgressBar->hide();
  m_fileByteLabel->hide();

  // 下段: コピー / 移動対象ファイル数のうち完了した数の進捗。
  m_progressBar = new QProgressBar(this);
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);
  mainLayout->addWidget(m_progressBar);

  // 件数ラベル: macOS の QProgressBar はバー上の文字を表示しないので、
  // "10 / 235 files" を別の QLabel に出して常に見える状態にする。
  m_countLabel = new QLabel(QStringLiteral("0 / 0 files"), this);
  m_countLabel->setAlignment(Qt::AlignRight);
  mainLayout->addWidget(m_countLabel);

  // Button layout: 左側に「自動で閉じる」チェック、右側に Cancel
  QHBoxLayout* buttonLayout = new QHBoxLayout();

  m_autoCloseCheck = new QCheckBox(tr("Close automatically when done"), this);
  // 既定値は Settings から取るが、ここでの変更は **その場限り** で永続化しない。
  // 永続的な既定は Settings → 全般 → File Operations から変更する。
  m_autoCloseCheck->setChecked(Settings::instance().progressAutoClose());
  buttonLayout->addWidget(m_autoCloseCheck);
  buttonLayout->addStretch();

  m_cancelButton = new QPushButton(tr("Cancel"), this);
  applyAltShortcut(m_cancelButton, Qt::Key_X);
  connect(m_cancelButton, &QPushButton::clicked, this, &ProgressDialog::onCancel);
  buttonLayout->addWidget(m_cancelButton);

  mainLayout->addLayout(buttonLayout);

  setLayout(mainLayout);

  // macOS の System Settings → 「キーボードナビゲーション」設定に依存させずに
  // Tab で全コントロールを辿れるようにする。
  m_autoCloseCheck->setFocusPolicy(Qt::StrongFocus);
  m_cancelButton->setFocusPolicy(Qt::StrongFocus);
}

void ProgressDialog::setWorker(WorkerBase* worker) {
  m_worker = worker;

  if (m_worker) {
    connect(m_worker, &WorkerBase::progressUpdated,
            this, &ProgressDialog::onProgressUpdated);
    connect(m_worker, &WorkerBase::finished,
            this, &ProgressDialog::onFinished);
  }
}

void ProgressDialog::onProgressUpdated(const WorkerProgress& progress) {
  // Update current file label
  if (!progress.currentFile.isEmpty()) {
    QFileInfo info(progress.currentFile);
    m_currentFileLabel->setText(tr("Processing: %1").arg(info.fileName()));
  }

  // 上段: 現在ファイル 1 個の中でのバイト進捗。バイト進捗を報告する操作
  // (コピー / 移動) でのみ表示し、削除など報告しない操作では隠したままにする。
  if (progress.total > 0) {
    if (m_fileProgressBar->isHidden()) {
      m_fileProgressBar->show();
      m_fileByteLabel->show();
    }
    const int filePct = static_cast<int>((progress.processed * 100) / progress.total);
    m_fileProgressBar->setRange(0, 100);
    m_fileProgressBar->setValue(filePct);
    m_fileProgressBar->setFormat(QStringLiteral("%p%"));
    const QLocale loc(QLocale::English);
    m_fileByteLabel->setText(tr("%1 / %2")
                               .arg(loc.formattedDataSize(progress.processed))
                               .arg(loc.formattedDataSize(progress.total)));
  } else if (!m_fileProgressBar->isHidden()) {
    // 一度表示済みでファイル間 (processed=0, total=-1) のとき。0% に戻す。
    m_fileProgressBar->setValue(0);
    m_fileProgressBar->setFormat(QString());
    m_fileByteLabel->clear();
  }

  // 下段: コピー / 移動対象ファイル数のうち完了した数の進捗。
  if (progress.filesTotal > 0) {
    const int percentage = (progress.filesDone * 100) / progress.filesTotal;
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(percentage);
    m_progressBar->setFormat(QStringLiteral("%p%"));
    m_countLabel->setText(tr("%1 / %2 files (%3%)")
                            .arg(progress.filesDone)
                            .arg(progress.filesTotal)
                            .arg(percentage));
  } else {
    // 総ファイル数が不明なとき (件数を数えない worker) は不確定表示。
    m_progressBar->setRange(0, 0);
    m_progressBar->setFormat(QString());
    m_countLabel->setText(tr("%1 files processed").arg(progress.filesDone));
  }
}

void ProgressDialog::onFinished(bool success) {
  // 自動で閉じる設定が ON なら従来通りすぐ閉じる
  if (m_autoCloseCheck && m_autoCloseCheck->isChecked()) {
    if (success) accept();
    else         reject();
    return;
  }

  // OFF: 完了状態を残してユーザーが Close で閉じるまで開いたままにする。
  // プログレスバーを完了状態で固定する (indeterminate range=0,0 のまま放置
  // するとぐるぐる回り続け、ユーザーには「まだ実行中」に見えてしまう)。
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(100);
  m_progressBar->setFormat(success ? tr("Done") : tr("Failed"));
  // 上段 (現在ファイル) は表示している操作 (コピー / 移動) のみ完了状態で固定。
  if (!m_fileProgressBar->isHidden()) {
    m_fileProgressBar->setRange(0, 100);
    m_fileProgressBar->setValue(success ? 100 : m_fileProgressBar->value());
    m_fileProgressBar->setFormat(success ? tr("Done") : tr("Failed"));
    m_fileByteLabel->clear();
  }
  // "Processing: foo.txt" は実行中の表示なので完了メッセージに差し替える
  m_currentFileLabel->setText(success ? tr("Completed.")
                                      : tr("Completed with errors."));

  // 「Cancel」ボタンを「Close」に切り替えて成功/失敗で accept/reject する。
  m_cancelButton->setEnabled(true);
  m_cancelButton->setText(tr("Close"));
  applyAltShortcut(m_cancelButton, Qt::Key_X);
  // 既存の onCancel 接続を切り、Close は単に accept/reject させる
  m_cancelButton->disconnect(this);
  connect(m_cancelButton, &QPushButton::clicked, this,
          [this, success]() {
            if (success) accept();
            else         reject();
          });
}

void ProgressDialog::onCancel() {
  if (m_worker) {
    m_worker->requestCancel();
    m_cancelButton->setEnabled(false);
    m_cancelButton->setText(tr("Cancelling..."));
  }
}

} // namespace Farman
