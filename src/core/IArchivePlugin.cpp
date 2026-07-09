#include "IArchivePlugin.h"

#include <QFileInfo>

namespace Farman {

bool IArchivePlugin::canHandle(const QString& archivePath) const {
  // 既定実装: 拡張子 (先頭ドット無し・小文字) の一致で判定する。
  // 複数ドット拡張子 (例 "tar.lzh") にも対応するため、完全なファイル名末尾を
  // ".<ext>" で照合する。
  const QString name = QFileInfo(archivePath).fileName();
  for (const QString& ext : supportedExtensions()) {
    if (ext.isEmpty()) continue;
    if (name.endsWith(QLatin1Char('.') + ext, Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

} // namespace Farman
