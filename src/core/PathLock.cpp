#include "PathLock.h"
#include <QDir>
#include <QFileInfo>

namespace Farman {

namespace {

// Windows / macOS の既定のファイルシステムは大文字小文字を区別しないので、
// 表記ゆれで判定をすり抜けないよう区別せずに比べる。
constexpr Qt::CaseSensitivity kPathCase =
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  Qt::CaseInsensitive;
#else
  Qt::CaseSensitive;
#endif

} // namespace

QString PathLock::normalize(const QString& path) {
  if (path.isEmpty()) return QString();
  QString p = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  // cleanPath はルート ("/" や "C:/") 以外の末尾 '/' を落とすので、そのままでよい。
  return p;
}

bool PathLock::isSameOrUnder(const QString& path, const QString& dir) {
  if (path.isEmpty() || dir.isEmpty()) return false;
  if (path.compare(dir, kPathCase) == 0) return true;
  const QString prefix = dir.endsWith(QLatin1Char('/')) ? dir
                                                        : dir + QLatin1Char('/');
  return path.startsWith(prefix, kPathCase);
}

void PathLock::setDirectories(const QStringList& dirs) {
  m_dirs.clear();
  for (const QString& d : dirs) {
    const QString n = normalize(d);
    if (n.isEmpty()) continue;
    bool dup = false;
    for (const QString& existing : std::as_const(m_dirs)) {
      if (existing.compare(n, kPathCase) == 0) {
        dup = true;
        break;
      }
    }
    if (!dup) m_dirs.append(n);
  }
}

bool PathLock::conflicts(const QStringList& items,
                         const QStringList& destDirs) const {
  if (m_dirs.isEmpty()) return false;
  for (const QString& item : items) {
    const QString p = normalize(item);
    if (p.isEmpty()) continue;
    for (const QString& locked : m_dirs) {
      if (isSameOrUnder(p, locked) || isSameOrUnder(locked, p)) return true;
    }
  }
  for (const QString& dest : destDirs) {
    const QString p = normalize(dest);
    if (p.isEmpty()) continue;
    for (const QString& locked : m_dirs) {
      if (isSameOrUnder(p, locked)) return true;
    }
  }
  return false;
}

} // namespace Farman
