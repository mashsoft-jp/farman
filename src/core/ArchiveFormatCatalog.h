#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Farman {

// アーカイブ「形式」1 つ分の静的な素性 (カタログ定義)。
//
// これまで対応形式は `ArchivePath::archiveExtensions()` のコード固定リストと、
// プラグインが実行時に名乗る拡張子の 2 系統に分かれており、UI から一覧も設定も
// できなかった。ここで組み込み (libarchive) 形式とプラグイン形式を同じ構造体に
// 揃え、「設定 → アーカイブ」タブの一覧・詳細と、拡張子ルーティングの両方が
// 同じカタログを参照するようにする。
//
// 「今それを farman が実際に読めるか」ではなく「形式としてどういう素性か」を
// 書く場所であることに注意 (実際に有効かどうかは defaultEnabled と Settings 側の
// 有効 / 無効で決まる)。
struct ArchiveFormatInfo {
  // 形式の出どころ。Plugin は IArchivePlugin が名乗ったもの。
  enum class Source { Builtin, Plugin };

  // 暗号化の対応状況。作成側で暗号化できるかは ReadWrite のみ。
  enum class Encryption { None, ReadOnly, ReadWrite };

  // 形式 ID。組み込みは "zip" / "tar.gz" のような安定した文字列、プラグインは
  // IArchivePlugin::pluginId()。Settings の保存キーになるので変更しないこと。
  QString id;

  // 一覧に出す表示名。組み込みは translate 済みの文字列を displayName() で作る。
  QString displayName;

  // 既定の対応拡張子。ファイル名全体に対する glob ("*.tar.gz")。
  // ユーザーが設定で上書きできる。
  QStringList defaultPatterns;

  Source source = Source::Builtin;

  // 既定で認識対象にするか。
  // v0.9.9 までに実際に読めていた zip / tar 系のみ true。libarchive が読めるはず
  // だが farman としての動作確認と各 OS のコーデック同梱保証がまだ済んでいない
  // 形式 (7z / rar / iso ...) は false で置き、ユーザーが明示的に有効化するか、
  // 「libarchive 形式の一括対応」で検証が済んだ時点で true に変える。
  bool defaultEnabled = false;

  // farman から新規作成できるか。false の形式は詳細ダイアログで作成系の設定
  // (圧縮レベル / 暗号化) を出さない。
  bool canCreate = false;

  // 作成時に圧縮レベルを指定できるか (無圧縮の tar や読取専用形式は false)。
  bool supportsCompressionLevel = false;
  int  minCompressionLevel = 0;
  int  maxCompressionLevel = 9;

  Encryption encryption = Encryption::None;

  // 作成時に使う暗号化方式の既定 ("aes256" / "zipcrypt")。
  // Encryption::ReadWrite の形式でのみ意味を持つ。パスワードが空なら暗号化
  // 自体を行わないので、「暗号化しない」はこの値ではなくパスワード欄で決まる。
  QString defaultEncryption;

  // エントリ名の文字コードを指定できるか (zip / lzh のようにレガシーコード
  // ページで格納されうる形式)。
  bool supportsFilenameEncoding = false;

  // 中身が 1 エントリだけの単一ファイル圧縮 (.gz / .xz など、tar を伴わないもの)。
  // コンテナ形式と UX を分けるための印。
  bool singleFileCompression = false;

  // Source::Plugin のときのプラグイン ID (= id と同じ値)。Builtin では空。
  QString pluginId;
};

// カタログ定義に Settings の上書きを重ねた「実効設定」。
// UI (設定 → アーカイブ) と拡張子ルーティングは常にこちらを見る。
struct ResolvedArchiveFormat {
  ArchiveFormatInfo info;

  // 認識対象か。プラグイン形式では Settings::isArchivePluginDisabled() の
  // 反転で、組み込み形式では上書きが無ければ info.defaultEnabled。
  bool        enabled = false;
  // 実効の対応拡張子 (上書きが無ければ info.defaultPatterns)。
  QStringList patterns;
  // 作成時の既定圧縮レベル。-1 = 形式の既定。
  int         compressionLevel = -1;
  // 作成時の既定暗号化方式。"" = なし / "aes256" / "zipcrypt"。
  QString     encryption;
  // エントリ名の文字コード。"" = 自動判別。
  QString     filenameEncoding;
};

// 組み込み + プラグインのアーカイブ形式カタログ。
//
// 組み込み形式は静的テーブル、プラグイン形式は ArchiveDispatcher の
// pluginRecords() から毎回組み立てる (プラグインは起動時に確定するので、
// 実質的に起動後は不変)。
namespace ArchiveFormatCatalog {

// 組み込み (libarchive) 形式の定義一覧。
const QList<ArchiveFormatInfo>& builtinFormats();

// 組み込み + 現在ロードされているプラグイン形式を結合した一覧。
// 並びは組み込み → プラグインの順で、組み込み内はカタログ定義順。
QList<ArchiveFormatInfo> allFormats();

// id から形式を引く。見つからなければ nullptr。
// 戻り値は呼び出しごとに作り直すリストを指すため、保持せずその場で使うこと。
const ArchiveFormatInfo* findBuiltin(const QString& id);

// allFormats() に Settings の上書きを重ねた一覧。並びは allFormats() と同じ。
QList<ResolvedArchiveFormat> resolvedFormats();

// id 1 件ぶんの実効設定。見つからなければ enabled=false の空を返す
// (found に非 nullptr を渡せば見つかったかを受け取れる)。
ResolvedArchiveFormat resolvedFormat(const QString& id, bool* found = nullptr);

// 有効な形式のパターンを 1 本のリストにまとめたもの。
QStringList activePatterns();

// activePatterns() を ArchivePath へ流し込む。起動時 (プラグインロード後) と、
// 設定が変わったときに呼ぶ。utils 層をコアに依存させないための注入口。
void applyToArchivePath();

} // namespace ArchiveFormatCatalog

} // namespace Farman
