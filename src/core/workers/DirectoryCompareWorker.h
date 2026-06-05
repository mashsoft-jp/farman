#pragma once

#include "WorkerBase.h"
#include "core/DirectoryCompare.h"
#include <QString>

namespace Farman {

// 左右 2 つのディレクトリを突き合わせて、各エントリに DiffStatus を割り当てる
// ワーカー。NameOnly / SizeMtime 粒度に対応し、同名サブディレクトリの再帰比較は
// ディレクトリエントリの Same / Differ に集約する。
//
// 結果は worker 完了後に `result()` で取り出す。WorkerBase の
// `progressUpdated` シグナルは使わない。
class DirectoryCompareWorker : public WorkerBase {
  Q_OBJECT

public:
  DirectoryCompareWorker(const QString&        leftDir,
                         const QString&        rightDir,
                         const CompareOptions& opts,
                         QObject*              parent = nullptr);

  // 完了後にメインスレッドから読み出す比較結果。
  const CompareResult& result() const { return m_result; }

protected:
  void run() override;

private:
  QString        m_leftDir;
  QString        m_rightDir;
  CompareOptions m_opts;
  CompareResult  m_result;
};

} // namespace Farman
