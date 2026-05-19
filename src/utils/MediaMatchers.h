#pragma once

#include <QString>
#include <QStringList>

class QMimeType;

namespace Farman {
namespace MediaMatchers {

// 拡張子パターンリストに対するマッチ判定。
// パターンは小文字の拡張子 ("png", "jp*g") とワイルドカード (`*`, `?`) を
// サポート。"!ext" 接頭辞は除外パターンで、マッチした時点で false を返す。
// 通常パターンが 1 つでも書かれているのにどれも一致しないと false を返す。
bool extensionMatches(const QStringList& patterns, const QString& extension);

// MIME タイプ名のマッチ判定。
// パターン末尾の `*` は prefix match、それ以外は完全一致 / inherits の両方を
// チェックする (例: "text/*" / "image/png" / "application/json")。
bool mimeMatches(const QStringList& patterns, const QMimeType& mime);

// ファイルパスを受け取り、Settings の Image Viewer 対象 (拡張子 or MIME)
// に該当するかを判定する。サムネイル生成・ImageViewer ルーティングの両方で
// 同じ条件で動かすために用意した薄いラッパ。Settings::instance() を内部参照。
bool isImageFile(const QString& filePath);

} // namespace MediaMatchers
} // namespace Farman
