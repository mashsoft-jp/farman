#include "MediaMatchers.h"
#include "settings/Settings.h"

#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QRegularExpression>

namespace Farman {
namespace MediaMatchers {

bool extensionMatches(const QStringList& patterns, const QString& extension) {
  auto patternMatches = [&](const QString& p) {
    if (p.contains(QLatin1Char('*')) || p.contains(QLatin1Char('?'))) {
      QRegularExpression re(
        QRegularExpression::wildcardToRegularExpression(p),
        QRegularExpression::CaseInsensitiveOption);
      return re.match(extension).hasMatch();
    }
    return extension.compare(p, Qt::CaseInsensitive) == 0;
  };

  bool anyInclude = false;
  bool included   = false;
  for (const QString& raw : patterns) {
    QString p = raw.trimmed();
    if (p.isEmpty()) continue;
    const bool isExclude = p.startsWith(QLatin1Char('!'));
    if (isExclude) {
      p = p.mid(1).trimmed();
      if (p.isEmpty()) continue;
      if (patternMatches(p)) return false;  // 除外マッチで即不一致
    } else {
      anyInclude = true;
      if (patternMatches(p)) included = true;
    }
  }
  if (!anyInclude) return false;
  return included;
}

bool mimeMatches(const QStringList& patterns, const QMimeType& mime) {
  const QString name = mime.name();
  for (const QString& p : patterns) {
    const QString trimmed = p.trimmed();
    if (trimmed.isEmpty()) continue;
    if (trimmed.endsWith(QLatin1Char('*'))) {
      const QString prefix = trimmed.left(trimmed.size() - 1);
      if (name.startsWith(prefix, Qt::CaseInsensitive)) return true;
    } else {
      if (name.compare(trimmed, Qt::CaseInsensitive) == 0) return true;
      if (mime.inherits(trimmed)) return true;
    }
  }
  return false;
}

bool isImageFile(const QString& filePath) {
  const Settings& s = Settings::instance();
  const QFileInfo fi(filePath);
  const QString ext = fi.suffix().toLower();
  if (extensionMatches(s.imageViewerExtensions(), ext)) return true;
  // MIME 判定は extra cost が掛かる (QMimeDatabase が file の magic bytes
  // を読みに行く可能性) ので、拡張子で先にふるい落としてから初めて呼ぶ。
  QMimeDatabase mimeDb;
  const QMimeType mime = mimeDb.mimeTypeForFile(filePath);
  return mimeMatches(s.imageViewerMimePatterns(), mime);
}

} // namespace MediaMatchers
} // namespace Farman
