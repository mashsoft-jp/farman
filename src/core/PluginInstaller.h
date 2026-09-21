#pragma once

#include <QCoreApplication>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace Farman {

// 外部プラグイン (.dylib / .dll / .so) の導入・更新・削除を担う共通基盤。
// 仕様は SPEC.md「プラグインのインストール」。
//
// 方針:
//   - 導入時にプラグインを dlopen しない (第三者コードを実行しない)。
//     QPluginLoader::metaData() でバイナリ内の Qt メタデータだけを読んで検証する。
//   - ロード中のライブラリは差し替え・削除できない (Windows) / すべきでない
//     (macOS / Linux) ので、導入・更新・削除はすべて pending/ に退避し、次回起動時の
//     プラグイン読込前に applyPending() で反映する。
//
//   - 配布物のファイル名は版数つき (`<Name>-vX.Y.Z-<os>-<arch>.<ext>`) なので、
//     ファイル名そのものでは「同じプラグインの別の版」を見分けられない。版数以降を
//     落とした基底名 (pluginBaseName) で同一性を判定し、新しい版を導入するときは
//     古い版のファイルの削除を一緒に退避する (同じ pluginId が 2 つあると、
//     ファイル名順で先に読まれる古い版が勝ち、新しい版は重複として弾かれるため)。
//
//   <プラグインディレクトリ>/
//     viewers/   archives/          … 実際にロードされる場所
//     pending/viewers/  pending/archives/   … 導入・更新待ちのファイル
//     pending/removals.json                 … 削除待ちの相対パス一覧
class PluginInstaller {
  Q_DECLARE_TR_FUNCTIONS(Farman::PluginInstaller)

public:
  enum class Kind { Unknown, Viewer, Archive };

  // inspect() の結果。ok=false のとき error にユーザー向けの理由が入る。
  struct Inspection {
    bool    ok = false;
    Kind    kind = Kind::Unknown;
    QString iid;
    QString minHostVersion;
    QString error;
  };

  // 退避中の操作。いずれもプラグインディレクトリからの相対パス
  // ("viewers/Foo.dylib")。
  struct PendingState {
    QStringList installs;
    QStringList removals;
    // 更新に伴う削除: 削除される古いファイル → 置き換える導入待ちファイル。
    // キーは removals にも含まれる。ユーザーが指示したアンインストールと区別する。
    QHash<QString, QString> replacedBy;
  };

  // 設定のプラグインディレクトリ (空なら既定値)。
  static QString pluginsRoot();
  // ビルド時に埋め込まれた farman 本体のバージョン文字列 (例 "1.1.0")。
  static QString hostVersion();
  // 種別ごとのサブフォルダ名 ("viewers" / "archives")。Unknown は空。
  static QString kindSubdir(Kind kind);
  // 実行 OS の共有ライブラリの拡張子 ("dylib" / "dll" / "so")。
  static QString nativeLibrarySuffix();

  // ロードせずにメタデータだけで「この farman に導入できるプラグインか」を判定する。
  // hostVersion は本体バージョン (MinHostVersion の判定に使う)。
  static Inspection inspect(const QString& filePath, const QString& hostVersion);

  // 同じプラグインかどうかの判定に使う基底名。拡張子と、配布物の版数以降
  // (`-vX.Y.Z-<os>-<arch>`)、ブラウザが付ける重複番号 (` (1)`) を落として小文字化する。
  //   "ModelViewerPlugin-v0.3.0-macos-arm64.dylib" → "modelviewerplugin"
  //   "LzhArchivePlugin.dylib" / "LzhArchivePlugin (1).dylib" → "lzharchiveplugin"
  static QString pluginBaseName(const QString& fileName);

  // fileName を導入すると置き換わる、同じプラグインのファイル (導入済み / 導入待ち) の
  // 相対パス。同名のファイルも含む。導入の確認に使う。
  static QStringList samePluginFiles(const QString& root, Kind kind,
                                     const QString& fileName);

  // 検証済みファイルを pending/<種別>/ へコピーする。同じプラグインの導入待ちは
  // 置き換え、導入済みの別名ファイル (古い版) は削除を一緒に退避する。
  // 同じファイルの削除待ちがあれば取り消す。
  static bool stageInstall(const QString& root, const QString& filePath,
                           Kind kind, QString* error);
  // 導入済みプラグイン (root 配下 viewers/ / archives/ 直下のファイル) の削除を
  // 退避する。同じファイルの導入待ちがあれば取り消す。
  static bool stageRemoval(const QString& root, const QString& installedFilePath,
                           QString* error);
  // 退避の取り消し。relPath は PendingState の要素。導入を取り消すと、その導入に
  // 伴って退避された古い版の削除も取り消す。
  static bool cancelPendingInstall(const QString& root, const QString& relPath);
  static bool cancelPendingRemoval(const QString& root, const QString& relPath);

  static PendingState pendingState(const QString& root);

  // installedFilePath が root/viewers または root/archives 直下なら、root からの
  // 相対パス ("viewers/Foo.dylib") を返す。それ以外 (同梱プラグイン、ディレクトリ外)
  // は空。アンインストールできるのは相対パスが取れるファイルだけ。
  static QString managedRelativePath(const QString& root,
                                     const QString& installedFilePath);

  // 起動時、プラグイン読込前に呼ぶ。削除 → 導入の順に反映し、結果を Logger に残す。
  // 失敗したものは pending に残して次回起動で再試行する。
  static void applyPending(const QString& root);

private:
  static QString pendingDir(const QString& root);
  static QString removalsFilePath(const QString& root);
  // removals.json の 1 件。replacedBy が空ならユーザーが指示したアンインストール。
  struct Removal {
    QString relPath;
    QString replacedBy;
  };
  static QList<Removal> readRemovals(const QString& root);
  static bool writeRemovals(const QString& root, const QList<Removal>& removals);
  // 退避が無くなったら空の pending/ を片付ける (空でなければ何もしない)。
  static void removeEmptyPendingDirs(const QString& root);
};

} // namespace Farman
