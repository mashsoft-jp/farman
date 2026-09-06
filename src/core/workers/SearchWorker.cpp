#include "SearchWorker.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace Farman {

SearchWorker::SearchWorker(const QString&      rootPath,
                           const QStringList&  namePatterns,
                           const QStringList&  excludeDirPatterns,
                           const QStringList&  excludeFilePatterns,
                           bool                includeSubdirs,
                           const SearchFilter& filter,
                           SearchTarget        target,
                           QObject*            parent)
  : WorkerBase(parent)
  , m_rootPath(rootPath)
  , m_namePatterns(namePatterns)
  , m_excludeDirPatterns(excludeDirPatterns)
  , m_excludeFilePatterns(excludeFilePatterns)
  , m_includeSubdirs(includeSubdirs)
  , m_filter(filter)
  , m_target(target) {
  m_namePatterns.removeAll(QString());
  m_excludeDirPatterns.removeAll(QString());
  m_excludeFilePatterns.removeAll(QString());
}

void SearchWorker::run() {
  searchIn(m_rootPath);
  emit finished(!isCancelled());
}

void SearchWorker::searchIn(const QString& dirPath) {
  if (isCancelled()) return;

  QDir dir(dirPath);
  if (!dir.exists()) return;

  // ファイル（パターン適用）
  if (m_target != SearchTarget::Directories) {
    const QStringList filters = m_namePatterns.isEmpty()
      ? QStringList{QStringLiteral("*")}
      : m_namePatterns;
    const QFileInfoList files = dir.entryInfoList(
      filters,
      QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : files) {
      if (isCancelled()) return;
      if (isExcludedFile(fi.fileName())) continue;
      if (!matchesFilter(fi)) continue;
      emit resultFound(fi.absoluteFilePath());
    }
  }

  // サブディレクトリ。「結果に出す」と「再帰で潜る」を 1 回の列挙でまとめて
  // 判断する。除外パターン (Exclude dirs) はどちらにも同じように効かせる
  // = 除外したディレクトリは結果にも出さないし、中にも入らない。
  const bool wantDirs = (m_target != SearchTarget::Files);
  if (!m_includeSubdirs && !wantDirs) return;

  const QFileInfoList subs = dir.entryInfoList(
    QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
  for (const QFileInfo& fi : subs) {
    if (isCancelled()) return;
    if (isExcludedDir(fi.fileName())) continue;

    if (wantDirs
        && matchesNamePattern(fi.fileName())
        && matchesFilter(fi)) {
      emit resultFound(fi.absoluteFilePath());
    }

    if (!m_includeSubdirs) continue;
    if (fi.isSymLink()) continue;  // ループ回避
    searchIn(fi.absoluteFilePath());
  }
}

bool SearchWorker::matchesNamePattern(const QString& name) const {
  if (m_namePatterns.isEmpty()) return true;
  for (const QString& pattern : m_namePatterns) {
    const QRegularExpression re = QRegularExpression::fromWildcard(
      pattern.trimmed(),
      Qt::CaseInsensitive);
    if (re.match(name).hasMatch()) return true;
  }
  return false;
}

bool SearchWorker::isExcludedDir(const QString& dirName) const {
  for (const QString& pattern : m_excludeDirPatterns) {
    const QRegularExpression re = QRegularExpression::fromWildcard(
      pattern.trimmed(),
      Qt::CaseInsensitive);
    if (re.match(dirName).hasMatch()) return true;
  }
  return false;
}

bool SearchWorker::isExcludedFile(const QString& fileName) const {
  for (const QString& pattern : m_excludeFilePatterns) {
    const QRegularExpression re = QRegularExpression::fromWildcard(
      pattern.trimmed(),
      Qt::CaseInsensitive);
    if (re.match(fileName).hasMatch()) return true;
  }
  return false;
}

bool SearchWorker::matchesFilter(const QFileInfo& fi) const {
  // サイズと内容はディレクトリに意味が無い。条件が有効なら「満たさない」と
  // 扱って結果から落とす (サイズ指定をしたのにディレクトリが混ざる、という
  // 見え方を避ける)。「ディレクトリのみ」ではダイアログ側が両フィルタを
  // 無効化するので、ここに来るのは「両方」を選んだときだけ。
  if (fi.isDir()) {
    if (m_filter.sizeEnabled)    return false;
    if (m_filter.contentEnabled) return false;
  }

  // サイズ
  if (m_filter.sizeEnabled) {
    const qint64 sz = fi.size();
    if (m_filter.minSize > 0 && sz < m_filter.minSize) return false;
    if (m_filter.maxSize > 0 && sz > m_filter.maxSize) return false;
  }

  // 更新日時
  if (m_filter.modifiedEnabled) {
    const QDateTime mt = fi.lastModified();
    if (m_filter.modifiedFrom.isValid() && mt < m_filter.modifiedFrom) return false;
    if (m_filter.modifiedTo.isValid()   && mt > m_filter.modifiedTo)   return false;
  }

  // 内容テキスト (大きいファイルはスキップ)
  if (m_filter.contentEnabled && !m_filter.contentBytes.isEmpty()) {
    if (fi.size() > m_filter.contentMaxScanBytes) return false;
    if (!fileContainsContent(fi.absoluteFilePath())) return false;
  }

  return true;
}

bool SearchWorker::fileContainsContent(const QString& filePath) const {
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) return false;

  // チャンクで読みつつ検索。前のチャンクの末尾と次のチャンクの先頭を
  // 跨ぐマッチも拾えるよう、検索文字列長 - 1 バイトの残しを次に渡す。
  // 大文字小文字を無視するときはバイト列を予め lower にして比較。
  const bool cs = m_filter.contentCaseSensitive;
  const QByteArray needle = cs ? m_filter.contentBytes
                                : m_filter.contentBytes.toLower();
  const qint64 chunkSize = 64 * 1024;
  const int    overlap   = qMax(0, needle.size() - 1);
  QByteArray buf;
  buf.reserve(static_cast<int>(chunkSize) + overlap);

  while (!f.atEnd()) {
    if (isCancelled()) return false;
    QByteArray chunk = f.read(chunkSize);
    if (!cs) chunk = chunk.toLower();
    buf.append(chunk);
    if (buf.indexOf(needle) >= 0) return true;
    // 次回の overlap 用に末尾だけ残す
    if (buf.size() > overlap) {
      buf.remove(0, buf.size() - overlap);
    }
  }
  return false;
}

} // namespace Farman
