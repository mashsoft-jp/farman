#pragma once

#include "WorkerBase.h"
#include <QStringList>

namespace Farman {

// 与えられた複数パス (選択項目) を集計して、合計サイズ / 合計ファイル数 /
// 合計ディレクトリ数を計算するワーカー。プロパティダイアログから呼び出される。
// ファイルはサイズ加算、ディレクトリは配下を再帰集計する。
//
// 走査中、一定間隔で statsUpdated を emit して途中経過を UI に流す。
// シンボリックリンクは追跡しない (無限ループ回避)。
//
// finished(bool) は WorkerBase の既定シグナル。キャンセルされたら false。
class PropertiesWorker : public WorkerBase {
  Q_OBJECT

public:
  PropertiesWorker(const QStringList& paths, QObject* parent = nullptr);

signals:
  // 途中経過。totalBytes はそれまで集計したバイト数、fileCount は通常
  // ファイル数、dirCount はディレクトリ数 (root 自身を含まない)。
  void statsUpdated(qint64 totalBytes, int fileCount, int dirCount);

protected:
  void run() override;

private:
  void scan(const QString& dirPath);

  QStringList m_paths;
  qint64  m_totalBytes = 0;
  int     m_fileCount  = 0;
  int     m_dirCount   = 0;
  // emit を間引くためのカウンタ
  int     m_emitCounter = 0;
};

} // namespace Farman
