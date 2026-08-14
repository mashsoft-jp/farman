#pragma once

// プラグインの設定ページ (IPluginSettingsPage) で対象ファイルパターンを編集する
// ための小さなヘルパ。ホスト側 ViewerTab の normalizedExtensions と同じ正規化
// (小文字化・先頭ドット除去) を行い、glob パターン (例: "c*", "!class",
// "jp*g", "*.tar.gz") はそのまま保持する。各公式ビュアーの設定ページが共有する。
// 判定側の規則は MediaMatchers::fileNameMatches() を参照。

#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace Farman {

// "PNG, .jpg; gif" のような入力を {"png", "jpg", "gif"} に正規化する。
// カンマ / セミコロン / 空白区切りを受け付け、空要素は捨てる。
inline QStringList parseExtensionsText(const QString& text) {
  QStringList result;
  const QStringList tokens =
    text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
  for (const QString& token : tokens) {
    QString ext = token.trimmed().toLower();
    while (ext.startsWith(QLatin1Char('.'))) ext.remove(0, 1);
    if (!ext.isEmpty()) result.append(ext);
  }
  return result;
}

// 拡張子一覧を編集欄に出す表示文字列 ("png, jpg, gif")。
inline QString joinExtensionsText(const QStringList& extensions) {
  return extensions.join(QStringLiteral(", "));
}

} // namespace Farman
