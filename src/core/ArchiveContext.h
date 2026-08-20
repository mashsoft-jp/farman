#pragma once

#include "ArchiveEntry.h"
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <atomic>
#include <memory>

namespace Farman {

// 1 アーカイブ分のメタデータキャッシュ。
//
// `ArchiveContext::load(archivePath)` で同期的にエントリを列挙し、
// 中身を `entries` に詰めて返す。
// `shared_ptr` で `FileListModel` (および将来は両ペイン間) に保持される。
//
// **読み取り専用**: 構築後は変更しない (FileItem が weak/shared で握っても安全)。
class ArchiveContext {
public:
  // ── 公開フィールド ────────────────────────────
  // アーカイブ自体の絶対パス。入れ子アーカイブでは
  // "a.zip!/d/inner.zip" のような**論理パス**が入る (実 FS 上には無い)。
  // 表示・ナビゲーションはこちらを使う。
  QString                       archivePath;
  // libarchive に渡す実体のパス。入れ子アーカイブのときだけ、一時展開した
  // 実ファイルの場所が入る。空なら archivePath そのものが実体。
  // **読み書きするときは必ず readPath() を経由すること。**
  QString                       localPath;
  QHash<QString, ArchiveEntry>  entries;        // pathInArchive (先頭 '/' なし) → entry
  QDateTime                     archiveMtime;   // 元アーカイブの mtime (再ロード判定用)

  // ── 暗号化対応 ────────────────────────────────
  // アーカイブに暗号化エントリが含まれているか (ZIP の中央ディレクトリで検出 or
  // エントリ単位の archive_entry_is_encrypted で検出)。
  // ZIP は暗号化されていてもエントリ "一覧" 自体は password 無しで取れるので、
  // load() はこのフラグを立てつつ list 構築までは成功する。実データの展開
  // (extractEntryTo / ArchiveExtractEntriesWorker) には password が要る。
  bool                          hasEncryptedEntries = false;
  // ユーザーが入力したパスワード。プロセスメモリ内のみ。永続化しない。
  QString                       password;

  // libarchive に渡す実体のパス。入れ子でなければ archivePath と同じ。
  QString readPath() const {
    return localPath.isEmpty() ? archivePath : localPath;
  }

  // ── ファクトリ ────────────────────────────────
  // libarchive で archivePath を開いて全エントリのメタデータを列挙する。
  // 失敗時は nullptr。
  // **本メソッドは同期 I/O**。UI スレッドから呼ぶ場合は呼び出し側で
  // QtConcurrent + イベントループで包むこと。
  //
  // errorOut (任意): nullptr 以外を渡すと、失敗時に翻訳済みエラーメッセージ
  //   ("Encrypted archive (not supported)" 等) が書き込まれる。
  // cancelFlag (任意): nullptr 以外を渡すと、エントリ列挙の各反復で値を確認
  //   し、true ならその場で打ち切って nullptr を返す (errorOut は cancel メッセージ)。
  // entriesRead (任意): nullptr 以外を渡すと、エントリ 1 件処理するごとに ++ する。
  //   UI スレッド側で QTimer + QProgressDialog 経由で「N entries read」表示に使える。
  //
  // localPath (任意): archivePath が入れ子アーカイブの論理パスのときに、
  //   実体として読む一時ファイルを渡す。空なら archivePath 自身を読む。
  static std::shared_ptr<ArchiveContext> load(
    const QString&         archivePath,
    QString*               errorOut    = nullptr,
    std::atomic<bool>*     cancelFlag  = nullptr,
    std::atomic<int>*      entriesRead = nullptr,
    const QString&         localPath   = QString());

  // ── クエリ ────────────────────────────────────
  // innerDir は "/" (ルート) または "/foo" / "/foo/bar" の形式。
  // その直下にいるエントリ (entries map のうち、parent が innerDir に一致するもの)
  // を返す。順序は entries 走査順 (不定)。
  QList<const ArchiveEntry*> childrenOf(const QString& innerDir) const;

  // innerDir が有効なディレクトリ（あるいは "/" = ルート）かどうか。
  // "/" は常に true。それ以外はエントリ自体が isDir=true で存在するか、
  // または entries の中にそれを prefix とするエントリがあるか。
  bool isValidDirectory(const QString& innerDir) const;

  // ── 1 エントリ展開 (Phase B) ──────────────────
  // archivePath を libarchive で開き直し、entryPath (= ArchiveEntry の
  // pathInArchive、先頭 '/' なし形式) に該当する 1 エントリの内容を destPath
  // に書き出す。destPath の親ディレクトリは必要に応じて作成。
  // 戻り値: 成功 true。libarchive はランダムアクセスできないので、エントリ
  // 発見まで先頭から逐次読みする (大きなアーカイブでは線形時間)。
  // 暗号化エントリの場合、password が空文字以外なら archive_read_add_passphrase
  // で渡して復号する。
  bool extractEntryTo(const QString& entryPath, const QString& destPath) const;

  // ── パスワード検証 ────────────────────────────
  // archivePath に対し、与えられた password で 1 つ目の暗号化エントリを試し
  // 読みする。少しでも復号できれば true。失敗 (libarchive が "incorrect
  // passphrase" を返す等) なら false。暗号化エントリが無いアーカイブでは
  // true を返す (password 不要なので合致扱い)。
  static bool verifyPassword(const QString& archivePath, const QString& password);

private:
  // pathInArchive をディレクトリ正規形に整える内部ヘルパ:
  //   "/" → ""
  //   "/foo" → "foo"
  //   "/foo/" → "foo"
  static QString normalizeDirKey(const QString& innerDir);
};

} // namespace Farman
