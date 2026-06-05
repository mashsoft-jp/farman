#include "DirectoryCompareWorker.h"
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QHash>
#include <QSet>

namespace Farman {

namespace {

struct Snapshot {
  qint64    size  = -1;
  QDateTime mtime;
  bool      isDir = false;
};

// 1 ディレクトリ直下のエントリを列挙し、name → Snapshot のマップを作る。
// 隠しファイル含む。"." / ".." は除外。
// シンボリックリンクは「ディレクトリ扱いしない」(再帰時の無限ループを避ける):
// link そのものをファイル相当のエントリとして比較する。
QHash<QString, Snapshot> snapshotDir(const QString& dir) {
  QHash<QString, Snapshot> out;
  QDir d(dir);
  if (!d.exists() || !d.isReadable()) return out;
  const QFileInfoList list = d.entryInfoList(
    QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
    QDir::NoSort);
  for (const QFileInfo& info : list) {
    Snapshot s;
    // symlink to dir はサイズ・mtime 比較のために isDir=false として扱う。
    const bool realDir = info.isDir() && !info.isSymLink();
    s.isDir = realDir;
    s.size  = realDir ? -1 : info.size();
    s.mtime = info.lastModified();
    out.insert(info.fileName(), s);
  }
  return out;
}

// 2 つのファイル (両方とも非ディレクトリ) を粒度で比較し DiffStatus を返す。
DiffStatus compareFile(const Snapshot& l, const Snapshot& r,
                       CompareGranularity g) {
  switch (g) {
    case CompareGranularity::NameOnly:
      return DiffStatus::Same;
    case CompareGranularity::SizeMtime: {
      if (l.size != r.size) return DiffStatus::Differ;
      const qint64 lt = l.mtime.toSecsSinceEpoch();
      const qint64 rt = r.mtime.toSecsSinceEpoch();
      return (lt == rt) ? DiffStatus::Same : DiffStatus::Differ;
    }
  }
  return DiffStatus::Same;
}

// 2 つのディレクトリの内容を再帰的に比較し、サブツリー全体としての DiffStatus
// を返す。集約ルール:
//   - 全エントリが Same → Same
//   - 片側にしかないエントリがある / 中身が違うエントリがある → Differ
// recursive=false のときは最上位の Differ 判定だけで止まる (= 直下に差分があれば
// Differ)、ただし通常の run() は子ディレクトリ自体は Same 扱いする (Phase A 動作)。
// この関数は recursive=true 専用で、何段でも深く潜る。
//
// 早期 return: 1 つでも差分が見つかればすぐ Differ を返してそれ以上見ない。
DiffStatus aggregateSubtree(const QString& leftDir, const QString& rightDir,
                            CompareGranularity g) {
  const QHash<QString, Snapshot> ls = snapshotDir(leftDir);
  const QHash<QString, Snapshot> rs = snapshotDir(rightDir);

  // 左にしかない / 右にしかない
  for (auto it = ls.cbegin(); it != ls.cend(); ++it) {
    if (!rs.contains(it.key())) return DiffStatus::Differ;
  }
  for (auto it = rs.cbegin(); it != rs.cend(); ++it) {
    if (!ls.contains(it.key())) return DiffStatus::Differ;
  }

  // 共通エントリを比較
  for (auto it = ls.cbegin(); it != ls.cend(); ++it) {
    const Snapshot& l = it.value();
    const Snapshot& r = rs.value(it.key());
    if (l.isDir != r.isDir) return DiffStatus::Differ;
    if (l.isDir) {
      const DiffStatus sub = aggregateSubtree(
        leftDir  + QLatin1Char('/') + it.key(),
        rightDir + QLatin1Char('/') + it.key(),
        g);
      if (sub != DiffStatus::Same) return DiffStatus::Differ;
    } else {
      if (compareFile(l, r, g) != DiffStatus::Same) return DiffStatus::Differ;
    }
  }
  return DiffStatus::Same;
}

} // namespace

DirectoryCompareWorker::DirectoryCompareWorker(
  const QString&        leftDir,
  const QString&        rightDir,
  const CompareOptions& opts,
  QObject*              parent)
  : WorkerBase(parent)
  , m_leftDir(leftDir)
  , m_rightDir(rightDir)
  , m_opts(opts) {
}

void DirectoryCompareWorker::run() {
  // 左右のディレクトリを直下だけ列挙してスナップショット化。
  // recursive=true の場合は、両側にある同名ディレクトリの中身を更に下層まで
  // 突き合わせて Same/Differ を集約する (aggregateSubtree)。
  const QHash<QString, Snapshot> leftSnap  = snapshotDir(m_leftDir);
  const QHash<QString, Snapshot> rightSnap = snapshotDir(m_rightDir);

  // 同じ name が両側にあるかをまず分類する。
  for (auto it = leftSnap.cbegin(); it != leftSnap.cend(); ++it) {
    if (isCancelled()) break;
    const QString& name = it.key();
    auto r = rightSnap.constFind(name);
    if (r == rightSnap.cend()) {
      m_result.left.insert(name, DiffStatus::OnlyHere);
      continue;
    }
    const Snapshot& l = it.value();
    const Snapshot& rv = r.value();

    DiffStatus s = DiffStatus::Same;
    if (l.isDir != rv.isDir) {
      // 片方がファイルでもう片方がディレクトリ → Differ
      s = DiffStatus::Differ;
    } else if (l.isDir && rv.isDir) {
      // 両側がディレクトリ。recursive オプションで中身まで見るか決まる。
      if (m_opts.recursive) {
        s = aggregateSubtree(
          m_leftDir  + QLatin1Char('/') + name,
          m_rightDir + QLatin1Char('/') + name,
          m_opts.granularity);
      } else {
        // 非再帰は「同名ディレクトリがあれば Same」(Phase A 動作と同じ)
        s = DiffStatus::Same;
      }
    } else {
      // ファイル同士は粒度で比較
      s = compareFile(l, rv, m_opts.granularity);
    }
    m_result.left.insert(name, s);
    m_result.right.insert(name, s);
  }

  // 右にしかない: 右側 overlay に OnlyHere、左側 overlay には現れない (= 左に
  // そもそも表示されないので)
  for (auto it = rightSnap.cbegin(); it != rightSnap.cend(); ++it) {
    if (!leftSnap.contains(it.key())) {
      m_result.right.insert(it.key(), DiffStatus::OnlyHere);
    }
  }

  emit finished(!isCancelled());
}

} // namespace Farman
