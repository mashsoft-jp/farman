#pragma once

#include <QString>

struct archive_entry;

namespace Farman {

// libarchive のエントリパス名を最適なエンコーディングで QString 化する。
//
// ZIP は UTF-8 フラグ (general purpose bit 11) が立っていればファイル名が
// UTF-8、そうでなければ作成環境のレガシーコードページ (日本語では CP932 /
// Shift-JIS) で格納される。libarchive は hdrcharset を指定しないと後者を
// 正しく変換できず文字化けするため、ここで自前でエンコーディングを決める:
//   1. バイト列が妥当な UTF-8 ならそのまま (UTF-8 フラグ付き zip / tar)。
//   2. そうでなければ日本語アプリの既定として Shift_JIS (CP932) で復号する。
// これにより macOS Finder 同様、UTF-8 フラグ無しの日本語 zip でも文字化けしない。
QString decodeArchiveEntryName(struct archive_entry* entry);

} // namespace Farman
