#pragma once

#include <QCoreApplication>
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

  // fileName が導入済み (または導入待ち) か。上書き確認に使う。
  static bool isInstalledOrPending(const QString& root, Kind kind,
                                   const QString& fileName);

  // 検証済みファイルを pending/<種別>/ へコピーする (同名は置き換え)。
  // 同じファイルの削除待ちがあれば取り消す。
  static bool stageInstall(const QString& root, const QString& filePath,
                           Kind kind, QString* error);
  // 導入済みプラグイン (root 配下 viewers/ / archives/ 直下のファイル) の削除を
  // 退避する。同じファイルの導入待ちがあれば取り消す。
  static bool stageRemoval(const QString& root, const QString& installedFilePath,
                           QString* error);
  // 退避の取り消し。relPath は PendingState の要素。
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
  static QStringList readRemovals(const QString& root);
  static bool writeRemovals(const QString& root, const QStringList& relPaths);
  // 退避が無くなったら空の pending/ を片付ける (空でなければ何もしない)。
  static void removeEmptyPendingDirs(const QString& root);
};

} // namespace Farman
