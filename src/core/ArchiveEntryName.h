#pragma once

#include <QList>
#include <QString>
#include <QStringList>

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
//
// encoding が空でなければ自動判別せず、その文字コード ("Shift_JIS" など
// QTextCodec 名 / "UTF-8") で復号する。設定 → アーカイブの形式ごとの
// 「ファイル名の文字コード」がここに来る。未知の名前だったときは自動判別に
// 戻す (設定ファイルを手で書き換えて壊れた名前を入れても、化けたままではなく
// 従来どおり読める側に倒す)。
QString decodeArchiveEntryName(struct archive_entry* entry,
                               const QString& encoding = QString());

// 形式ごとの「エントリ名の文字コード」ルール 1 件。
// patterns はアーカイブファイル名に対する glob ("*.zip" 等)。
struct FilenameEncodingRule {
  QStringList patterns;
  QString     encoding;
};

// 文字コードのルールを流し込む。ArchiveFormatCatalog が起動時と設定変更時に
// 呼ぶ (utils / core 下位層から Settings を引かないための注入口。ArchivePath の
// setArchivePatterns() と同じ形)。
//
// **メインスレッドからのみ呼ぶこと**。読み出し側 (filenameEncodingFor) は
// ワーカースレッドからも呼ばれるが、書き込みは起動時と設定保存時だけなので
// ロックを持たない。
void setFilenameEncodingRules(const QList<FilenameEncodingRule>& rules);

// アーカイブのファイル名から、そのエントリ名に使う文字コードを引く。
// 該当ルールが無い / 自動判別のときは空文字。
//
// アーカイブ 1 つにつき 1 回だけ引いて、エントリのループには引数で渡すこと
// (エントリごとに glob を回すと大きな書庫で効いてくる)。
QString filenameEncodingFor(const QString& archiveFileName);

} // namespace Farman
