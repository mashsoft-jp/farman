#pragma once

#include <QString>
#include <QStringList>

namespace Farman {

// バックグラウンドで実行中のコピー / 移動が使っているディレクトリ (コピー元・
// コピー先) を「ロック」として保持し、これから行うファイル操作がそれらに
// 触れるかどうかを判定する。
//
// 判定規則:
//   - 操作対象のパス (削除・リネーム・コピー元などのアイテムや、新しく作る /
//     書き込むパス) は、ロック中ディレクトリと同じ・その配下・その上位の
//     いずれかなら衝突とする。上位を消したり移動したりすると、処理中の
//     ディレクトリごと消えてしまうため。
//   - 書き込み先ディレクトリは、ロック中ディレクトリと同じかその配下なら
//     衝突とする (上位ディレクトリへ新しいものを置くだけなら影響しない。
//     上位に同名で上書きするケースは、書き込むパスを操作対象として渡して
//     判定する)。
class PathLock {
public:
  void setDirectories(const QStringList& dirs);
  void clear() { m_dirs.clear(); }
  bool isEmpty() const { return m_dirs.isEmpty(); }
  const QStringList& directories() const { return m_dirs; }

  // items / destDirs のいずれかがロックに触れるなら true。
  bool conflicts(const QStringList& items,
                 const QStringList& destDirs = QStringList()) const;

  // 比較用に正規化した絶対パス (区切りは '/', 末尾の '/' なし。ルートは除く)。
  static QString normalize(const QString& path);
  // path が dir と同じか、その配下なら true (どちらも normalize 済みを想定)。
  static bool isSameOrUnder(const QString& path, const QString& dir);

private:
  QStringList m_dirs;  // normalize 済み
};

} // namespace Farman
