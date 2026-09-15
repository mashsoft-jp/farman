#include "WorkerBase.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace Farman {

WorkerBase::WorkerBase(QObject* parent) : QThread(parent) {
}

WorkerBase::~WorkerBase() = default;

void WorkerBase::requestCancel() {
  m_cancelRequested.store(true);
}

bool WorkerBase::isCancelled() const {
  return m_cancelRequested.load();
}

void WorkerBase::setOverwriteMode(OverwriteMode mode) {
  m_overwriteMode = mode;
}

void WorkerBase::setAutoRenameTemplate(const QString& tmpl) {
  m_autoRenameTemplate = tmpl;
}

OverwriteResolution WorkerBase::resolveOverwrite(
  const QString& srcPath,
  const QString& dstPath) {
  OverwriteResolution resolution;
  resolution.targetPath = dstPath;

  switch (m_overwriteMode) {
    case OverwriteMode::AutoOverwrite:
      resolution.action = OverwriteResolution::Action::Overwrite;
      return resolution;

    case OverwriteMode::AutoRename:
      resolution.action     = OverwriteResolution::Action::Rename;
      resolution.targetPath = generateUniqueName(dstPath);
      return resolution;

    case OverwriteMode::Ask: {
      // 前回のダイアログで「以降の重複にも適用」が選ばれていれば、
      // ダイアログを出さずにその選択を使い回す。
      if (m_stickyAction.has_value()) {
        switch (*m_stickyAction) {
          case OverwriteDecision::Action::Overwrite:
            resolution.action = OverwriteResolution::Action::Overwrite;
            break;
          case OverwriteDecision::Action::Rename:
            // ユーザーが入力した名前は 1 件目にしか使えないので、以降は
            // 自動リネームテンプレートで一意な名前を付ける。
            resolution.action     = OverwriteResolution::Action::Rename;
            resolution.targetPath = generateUniqueName(dstPath);
            break;
          case OverwriteDecision::Action::Skip:
            resolution.action = OverwriteResolution::Action::Skip;
            break;
          case OverwriteDecision::Action::Cancel:
            resolution.action = OverwriteResolution::Action::Cancel;
            break;
        }
        return resolution;
      }

      OverwriteDecision decision;
      emit overwriteRequired(srcPath, dstPath, &decision);
      switch (decision.action) {
        case OverwriteDecision::Action::Overwrite:
          resolution.action = OverwriteResolution::Action::Overwrite;
          break;
        case OverwriteDecision::Action::Rename: {
          resolution.action = OverwriteResolution::Action::Rename;
          const QString parent = QFileInfo(dstPath).absolutePath();
          resolution.targetPath = parent + "/" + decision.newName;
          break;
        }
        case OverwriteDecision::Action::Skip:
          resolution.action = OverwriteResolution::Action::Skip;
          break;
        case OverwriteDecision::Action::Cancel:
          resolution.action = OverwriteResolution::Action::Cancel;
          break;
      }
      if (decision.applyToAll &&
          decision.action != OverwriteDecision::Action::Cancel) {
        m_stickyAction = decision.action;
      }
      return resolution;
    }
  }
  return resolution;
}

QString WorkerBase::generateUniqueName(const QString& path) const {
  QFileInfo info(path);
  const QString dir      = info.absolutePath();
  const QString fileName = info.fileName();

  // dot-file 保護: 先頭 '.' を含まない最初の '.' を区切りとする
  int firstDot = -1;
  for (int i = 1; i < fileName.length(); ++i) {
    if (fileName[i] == QLatin1Char('.')) {
      firstDot = i;
      break;
    }
  }
  const QString base = (firstDot >= 0) ? fileName.left(firstDot) : fileName;
  const QString ext  = (firstDot >= 0) ? fileName.mid(firstDot)  : QString();

  // テンプレート正規化: 空 or {n} を含まなければ末尾に " ({n})" を補う
  QString tmpl = m_autoRenameTemplate;
  if (tmpl.isEmpty()) tmpl = QStringLiteral(" ({n})");
  if (!tmpl.contains(QStringLiteral("{n}"))) tmpl += QStringLiteral(" ({n})");

  for (int n = 1; n < 10000; ++n) {
    QString suffix = tmpl;
    suffix.replace(QStringLiteral("{n}"), QString::number(n));
    const QString candidate     = base + suffix + ext;
    const QString candidatePath = dir + "/" + candidate;
    if (!QFileInfo::exists(candidatePath)) {
      return candidatePath;
    }
  }
  // フォールバック（まずあり得ないが念のため）
  return path + " (copy)";
}

int WorkerBase::countAllFiles(const QStringList& paths) {
  int count = 0;
  for (const QString& path : paths) {
    QFileInfo info(path);
    if (!info.exists()) continue;
    if (info.isDir()) {
      QDirIterator it(path,
                      QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                      QDirIterator::Subdirectories);
      while (it.hasNext()) {
        it.next();
        ++count;
      }
    } else if (info.isFile()) {
      ++count;
    }
  }
  return count;
}

} // namespace Farman
