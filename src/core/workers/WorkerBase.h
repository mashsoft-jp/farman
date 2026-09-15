#pragma once

#include "types.h"
#include <QThread>
#include <QString>
#include <atomic>
#include <optional>

namespace Farman {

// Worker → UI への進捗通知
struct WorkerProgress {
  QString currentFile;  // 現在処理中のファイル名
  qint64  processed;    // 処理済みバイト数
  qint64  total;        // 合計バイト数（不明な場合は -1）
  int     filesDone;    // 完了ファイル数
  int     filesTotal;   // 合計ファイル数
};

// resolveOverwrite の結果
struct OverwriteResolution {
  enum class Action {
    Overwrite,  // dst を削除してから書き込み
    Rename,     // targetPath の新パスへ書き込み（ユニーク名）
    Skip,       // このエントリは書き込まず、次へ進む
    Cancel      // 操作を中止
  };
  Action  action      = Action::Cancel;
  QString targetPath;  // 最終的に使用する出力先フルパス
};

class WorkerBase : public QThread {
  Q_OBJECT

public:
  explicit WorkerBase(QObject* parent = nullptr);
  ~WorkerBase() override;

  // キャンセル要求（スレッドセーフ）
  void requestCancel();
  bool isCancelled() const;

  // 上書き動作モード（開始前に設定）
  void setOverwriteMode(OverwriteMode mode);

  // AutoRename 時に付加するサフィックステンプレート。"{n}" がカウンタ。
  // 空 or {n} を含まない場合は末尾に " ({n})" を補う。
  void setAutoRenameTemplate(const QString& tmpl);

signals:
  void progressUpdated(const WorkerProgress& progress);
  // Ask モードでのみ emit。UI スレッドで dialog を開き、decision を埋める。
  // Qt::BlockingQueuedConnection で接続することを想定。
  void overwriteRequired(
    const QString& srcPath,
    const QString& dstPath,
    OverwriteDecision* decision
  );
  void errorOccurred(const QString& path, const QString& message);
  void finished(bool success);

protected:
  void run() override = 0;

  // 上書き競合の解決。dst が既に存在する場合に呼び出す。
  // - AutoOverwrite: { Overwrite, dst }
  // - AutoRename:    { Rename, generateUniqueName(dst) }
  // - Ask:           ダイアログ結果に従う。ダイアログで「以降にも適用」が
  //                  チェックされていたら、その選択を m_stickyAction に記憶し
  //                  以降の競合ではダイアログを出さずに同じ選択を使う
  //                  (Rename は generateUniqueName で自動リネーム)。
  OverwriteResolution resolveOverwrite(
    const QString& srcPath,
    const QString& dstPath
  );

  // "foo.txt" → "foo (1).txt", "foo (2).txt" … のように既存ファイルと衝突しない
  // ファイル名を生成して返す（m_autoRenameTemplate を使用）。
  QString generateUniqueName(const QString& path) const;

  // 与えたエントリ群の中の通常ファイル数を再帰的に数える。
  // 進捗ダイアログで「N / M files」を正しく表示するために、
  // 実際の copy/move/remove を始める前に呼んでおく。
  // ディレクトリ自体はカウントせず、その中の通常ファイルだけを数える。
  static int countAllFiles(const QStringList& paths);

  std::atomic<bool> m_cancelRequested{false};
  OverwriteMode     m_overwriteMode = OverwriteMode::Ask;
  QString           m_autoRenameTemplate = QStringLiteral(" ({n})");
  // Ask モードで「以降の重複にも適用」された選択。操作 (ワーカー) 単位で
  // 保持し、Settings には保存しない。
  std::optional<OverwriteDecision::Action> m_stickyAction;
};

} // namespace Farman
