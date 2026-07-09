#pragma once

#include "ArchiveEntry.h"
#include <QList>
#include <QString>
#include <QStringList>
#include <QtPlugin>
#include <atomic>

namespace Farman {

// アーカイブプラグインに渡すアプリ本体側のコンテキスト。
// IViewerPlugin の PluginContext と同様、Settings/QApplication への参照は渡さず、
// 必要な値だけを固定形で渡す。フィールド追加は構造体メンバの追加で行う
// (署名変更を避ける)。
struct ArchivePluginContext {
  // 呼び出し時の farman アプリのバージョン文字列 (例 "1.0.0")。
  QString farmanVersion;
};

// listEntries() の結果。エントリの生の一覧とメタ情報を返す。
//
// **セキュリティ責務はホスト側**: プラグインが返した raw なエントリ一覧に対し、
// farman 本体 (ArchiveContext) が Zip Slip 防御 (`..`/NUL 除去) と合成ディレクトリ
// 生成を行う。プラグインは「アーカイブを読んでエントリを列挙する」ことだけに集中し、
// 防御線を自前で持たなくてよい (持っていても二重で害はない)。
struct ArchiveListResult {
  bool                ok = false;   // 列挙に成功したか
  QList<ArchiveEntry> entries;      // アーカイブ内エントリ (先頭'/'なし。順不同で可)
  bool                hasEncryptedEntries = false;  // 暗号化エントリを含むか
  QString             error;        // ok == false のとき、翻訳済みエラーメッセージ
};

// アーカイブプラグインのインターフェース。
// IID は `com.farman.IArchivePlugin/<major>.<minor>` 形式。ABI 互換が壊れる変更
// (仮想関数の追加・削除・並び替え等) をしたらメジャー番号を上げる。旧 IID の
// プラグインは qobject_cast に失敗し、ロードエラーとして安全に拒否される。
// プラグイン側の Q_PLUGIN_METADATA でも必ずこのマクロを使うこと。
#define FarmanIArchivePlugin_iid "com.farman.IArchivePlugin/1.0"

class IArchivePlugin {
public:
  virtual ~IArchivePlugin() = default;

  // ── プラグイン識別情報 ─────────────────────────
  virtual QString pluginId()   const = 0;  // "lzh_archive"
  virtual QString pluginName() const = 0;  // "LZH アーカイブ"
  // 制作者情報。外部プラグインは author() を必ず返すこと (空だとロード時に
  // エラーとなり使用不可)。authorUrl() は任意。
  virtual QString author()    const { return {}; }
  virtual QString authorUrl() const { return {}; }
  // プラグイン自身の配布バージョン (例 "1.0.0")。ABI (IID) とは別物で、同じ
  // インターフェースのままプラグインを更新配布したときに版を区別するためのもの。
  // farman が Plugins 一覧やログで表示する。空文字なら「不明」として扱う。
  virtual QString version()   const { return {}; }
  // 優先度: 0 が最優先で、数値が小さいほど優先される。同一拡張子を複数プラグイン
  // が名乗った場合の解決に使う。ユーザー作成の外部プラグインは 0〜9999、
  // 10000 以上は同梱公式用の予約域。
  virtual int     priority()   const { return 0; }

  // ── 対応拡張子の宣言 ───────────────────────────
  // 先頭ドット無しの小文字拡張子で返す。例: {"lzh", "lha"}。
  // farman は起動時にこれを ArchivePath へ登録し、`x.lzh!/inner` 形式の
  // アーカイブブラウジングを有効化する。
  virtual QStringList supportedExtensions() const = 0;

  // このアーカイブを扱えるか。既定は supportedExtensions() との拡張子一致。
  // マジックナンバー判定等が必要なら override する。
  virtual bool canHandle(const QString& archivePath) const;

  // ── ライフサイクル (任意 override) ──────────────
  // ロード直後に 1 回だけ呼ばれる。失敗時は false (Dispatcher が登録を取り消す)。
  virtual bool initialize(const ArchivePluginContext& /*ctx*/) { return true; }
  // initialize() が成功したプラグインに対し、アンロード前に 1 回だけ呼ばれる。
  virtual void shutdown() {}

  // ── 機能宣言 (任意 override) ────────────────────
  virtual QStringList capabilities() const { return {}; }

  // ── アーカイブ操作 ─────────────────────────────
  // アーカイブ全エントリを列挙する。**同期 I/O**。呼び出し側 (ホスト) が
  // ワーカースレッド + イベントループで包む。
  //   cancelFlag  (任意): nullptr 以外なら各反復で確認し、true なら中断して
  //                       ok=false / error=キャンセル文言 を返す。
  //   entriesRead (任意): nullptr 以外ならエントリ 1 件処理ごとに ++。
  virtual ArchiveListResult listEntries(
    const QString&     archivePath,
    std::atomic<bool>* cancelFlag  = nullptr,
    std::atomic<int>*  entriesRead = nullptr) = 0;

  // 1 エントリ (entryPath = pathInArchive 形式、先頭'/'なし) を destPath に
  // 書き出す。destPath の親ディレクトリはプラグイン側で必要に応じ作成してよいが、
  // ホストも事前に mkpath する。password が空でなければ復号に使う (暗号化非対応
  // プラグインは無視してよい)。成功 true。
  virtual bool extractEntry(
    const QString& archivePath,
    const QString& entryPath,
    const QString& destPath,
    const QString& password) = 0;

  // 与えた password が正しいか検証する。暗号化非対応 (既定) は常に true
  // (password 不要とみなす)。
  virtual bool verifyPassword(const QString& /*archivePath*/,
                              const QString& /*password*/) { return true; }
};

} // namespace Farman

// 1.0: 初版。
Q_DECLARE_INTERFACE(Farman::IArchivePlugin, FarmanIArchivePlugin_iid)
