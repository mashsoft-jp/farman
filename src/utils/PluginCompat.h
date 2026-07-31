#pragma once

// 外部プラグインの互換性判定ヘルパ。
//
// プラグインが Q_PLUGIN_METADATA の FILE json に "MinHostVersion" を宣言している
// 場合、farman 本体はロード (dlopen) の「前」にメタデータだけを読んで、本体の
// バージョンが要件を満たすか判定する。満たさなければ instance() を呼ばずに
// スキップし、Settings > Plugins に実用的な理由 (要 farman X 以降) を表示する。
//
// なぜロード前か: プラグインが本体側の依存 (例: libQt6OpenGL) を必要とする場合、
// それが無い本体では dlopen 自体が失敗し、原因の分かりにくい dyld エラーになる。
// メタデータはコード解決を伴わずに QPluginLoader::metaData() で読めるため、
// バイナリがロードできないケースでも明快なメッセージを出せる。

#include <QList>
#include <QString>

namespace Farman {

// バージョン文字列の数値部 (major.minor.patch...) を取り出す。
// 先頭 "v"/"V" と "-" 以降の prerelease サフィックスは無視する。
// 数値以外が混ざる不正フォーマットは空リストを返す。
inline QList<int> pluginVersionNumericParts(QString s) {
  s = s.trimmed();
  if (s.startsWith(QLatin1Char('v')) || s.startsWith(QLatin1Char('V'))) {
    s = s.mid(1);
  }
  const int dash = s.indexOf(QLatin1Char('-'));
  if (dash >= 0) {
    s = s.left(dash);
  }
  QList<int> parts;
  const QStringList chunks = s.split(QLatin1Char('.'), Qt::SkipEmptyParts);
  for (const QString& c : chunks) {
    bool      ok = false;
    const int n  = c.toInt(&ok);
    if (!ok) {
      parts.clear();
      break;
    }
    parts.append(n);
  }
  return parts;
}

// hostVersion が minVersion 以上か。数値部のみで比較し、prerelease サフィックス
// (-test 等) は無視する (例: "0.9.9-test" は "0.9.9" 要件を満たす)。
//   - minVersion が空 (宣言なし) → 常に true (ゲートしない)
//   - minVersion が不正フォーマット → true (安全側: 不用意に弾かない)
//   - hostVersion が不明で minVersion 指定あり → false (要件不明なので拒否)
inline bool hostSatisfiesMinVersion(const QString& hostVersion, const QString& minVersion) {
  if (minVersion.trimmed().isEmpty()) {
    return true;
  }
  const QList<int> host = pluginVersionNumericParts(hostVersion);
  const QList<int> min  = pluginVersionNumericParts(minVersion);
  if (min.isEmpty()) {
    return true;
  }
  if (host.isEmpty()) {
    return false;
  }
  const int n = qMax(host.size(), min.size());
  for (int i = 0; i < n; ++i) {
    const int hv = (i < host.size()) ? host[i] : 0;
    const int mv = (i < min.size()) ? min[i] : 0;
    if (hv < mv) {
      return false;
    }
    if (hv > mv) {
      return true;
    }
  }
  return true;  // 数値部が完全一致
}

} // namespace Farman
