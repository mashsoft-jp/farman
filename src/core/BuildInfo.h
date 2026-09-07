#pragma once

#include <QString>
#include <QStringList>

namespace Farman {

// ビルドリビジョン。同じバージョン文字列 (例 "0.9.10-test") のビルドを
// 見分けるための識別子を 1 行に組み立てる。
//
//   CI ビルド     : "build 92 · a5d7fd6 · 2026-09-07 04:29 UTC"
//   ローカルビルド: "dev · a5d7fd6+ · 2026-09-07 12:00 UTC"
//
// 末尾の "+" は追跡中ファイルに未コミットの変更があることを示す。
// 取得できなかった要素は省く。全部空なら空文字を返す。
//
// 値の出どころは CMake (FARMAN_BUILD_NUMBER / _COMMIT / _TIMESTAMP)。
// バージョン比較には使わないこと (FARMAN_VERSION とは別物)。
inline QString buildRevision() {
#ifndef FARMAN_BUILD_NUMBER
#define FARMAN_BUILD_NUMBER ""
#endif
#ifndef FARMAN_BUILD_COMMIT
#define FARMAN_BUILD_COMMIT ""
#endif
#ifndef FARMAN_BUILD_TIMESTAMP
#define FARMAN_BUILD_TIMESTAMP ""
#endif
  const QString number    = QStringLiteral(FARMAN_BUILD_NUMBER);
  const QString commit    = QStringLiteral(FARMAN_BUILD_COMMIT);
  const QString timestamp = QStringLiteral(FARMAN_BUILD_TIMESTAMP);

  QStringList parts;
  // run number が無い = CI 以外で作ったビルド。"dev" と明示して、配布物と
  // 取り違えないようにする。
  parts << (number.isEmpty() ? QStringLiteral("dev")
                             : QStringLiteral("build %1").arg(number));
  if (!commit.isEmpty())    parts << commit;
  if (!timestamp.isEmpty()) parts << timestamp;

  // "dev" だけしか無いなら識別子として意味がないので空を返す。
  if (parts.size() == 1 && number.isEmpty()) return QString();
  return parts.join(QStringLiteral(" · "));
}

} // namespace Farman
