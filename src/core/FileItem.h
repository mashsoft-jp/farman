#pragma once

#include "types.h"
#include "ArchiveEntry.h"
#include <QFileInfo>
#include <QString>
#include <QDateTime>
#include <memory>
#include <optional>

namespace Farman {

class ArchiveContext;

class FileItem {
public:
  // 通常 FS のファイル / ディレクトリ
  explicit FileItem(const QFileInfo& info);
  // アーカイブ内エントリ。archivePath は表示用 (absolutePath で
  // "<archive>!/<inner>" を組み立てるのに使う)。
  FileItem(const ArchiveEntry& entry,
           std::shared_ptr<ArchiveContext> ctx);

  // 基本情報
  QString   name()         const;
  QString   absolutePath() const;
  QString   suffix()       const;   // 拡張子（小文字）
  QString   mimeType()     const;   // 遅延取得
  qint64    size()         const;   // ディレクトリは -1
  QDateTime lastModified() const;

  // 種別
  bool isDir()     const;
  bool isFile()    const;
  bool isHidden()  const;
  bool isSymLink() const;
  bool isDotDot()  const;   // ".." エントリ

  // アーカイブ内エントリかどうか。通常 FS なら false。
  bool isInArchive() const { return m_archiveEntry.has_value(); }
  const ArchiveEntry* archiveEntry() const {
    return m_archiveEntry.has_value() ? &m_archiveEntry.value() : nullptr;
  }
  // このエントリが属するアーカイブ。通常 FS なら nullptr。
  // raw pointer は呼び出し中のみ有効 (実体は shared_ptr が保持)。
  const ArchiveContext* archiveContext() const { return m_archiveContext.get(); }

  // 選択状態
  bool isSelected() const;
  void setSelected(bool selected);

  // 生の QFileInfo が必要な場合 (アーカイブ内エントリでは空 QFileInfo を返す)
  const QFileInfo& fileInfo() const;

private:
  QFileInfo m_info;
  bool      m_selected = false;
  mutable QString m_mimeType;  // 遅延初期化

  // アーカイブ内エントリ用 (set されているときは m_info ではなく
  // こちらを真実とする)。
  std::optional<ArchiveEntry>        m_archiveEntry;
  std::shared_ptr<ArchiveContext>    m_archiveContext;
};

} // namespace Farman
