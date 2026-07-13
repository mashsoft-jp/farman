#pragma once

#include "IArchivePlugin.h"
#include <QDir>
#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

class QPluginLoader;

namespace Farman {

// 1 つのアーカイブプラグインのロード結果 (Help → Plugins ダイアログ表示用)。
// ViewerDispatcher の PluginRecord と同趣旨だが、アーカイブ用に独立して持つ。
struct ArchivePluginRecord {
  enum class Origin { Bundled, External };
  Origin      origin = Origin::External;
  QString     filePath;            // 動的ロード元 path
  QString     pluginId;            // 失敗時は取得できれば id、無理なら空
  QString     pluginName;
  QString     version;             // プラグイン自身の配布バージョン (空 = 不明)
  QString     author;
  QString     authorUrl;
  QStringList supportedExtensions;
  int         priority = -1;       // -1 = 不明。0 が最優先
  bool        loaded   = false;
  bool        disabledByUser = false;  // ユーザーが設定で無効化した
  QString     errorReason;         // loaded == false のときだけ非空
};

// アーカイブ (非ビュアー) プラグインのロードと解決を担う。
// ViewerDispatcher の兄弟。plugins/archives/ から IArchivePlugin を動的ロードし、
// 拡張子 → プラグインの解決を提供する。将来 IFsPlugin / IContentPlugin 等を
// 足す場合も同じパターンで別ディスパッチャを新設する。
class ArchiveDispatcher : public QObject {
  Q_OBJECT

public:
  static ArchiveDispatcher& instance();

  // プラグインに渡す ArchivePluginContext を設定する (loadPlugins より前に呼ぶ)。
  void setContext(const ArchivePluginContext& ctx) { m_context = ctx; }
  const ArchivePluginContext& pluginContext() const { return m_context; }

  // 同梱公式アーカイブプラグイン (バイナリ隣の plugins/archives / macOS の
  // .app/Contents/PlugIns/archives) を動的ロードする。起動時に呼ぶ。
  void registerBundledPlugins();

  // 指定ディレクトリ (通常はユーザーの外部プラグインディレクトリ配下 archives/)
  // からプラグインをロードする。
  void loadPlugins(const QDir& pluginDir);

  // 登録済みプラグインの shutdown() を呼んでからアンロードする。
  // app.exec() 後、QApplication が生きているうちに呼ぶ。
  void shutdownPlugins();

  // archivePath を扱えるプラグインを返す (無ければ nullptr)。
  // 複数が canHandle した場合は priority 最小 (最優先) を返す。
  IArchivePlugin* pluginForPath(const QString& archivePath) const;

  // 登録済み全プラグインが名乗る拡張子 (先頭ドット付き ".lzh" 形式) を返す。
  // main() が ArchivePath::registerArchiveExtension() へ流し込むのに使う。
  QStringList registeredDotExtensions() const;

  // 登録済みプラグインが 1 つでもあるか (高速パス判定用)。
  bool hasPlugins() const { return !m_plugins.isEmpty(); }

  // 全プラグインのロード結果ログ (Help ダイアログ表示用)。
  QList<ArchivePluginRecord> pluginRecords() const { return m_records; }

private:
  explicit ArchiveDispatcher(QObject* parent = nullptr);
  // 同梱プラグインの探索候補ディレクトリ (存在する最初のものを使う)。
  QStringList bundledArchiveDirectories() const;
  // ディレクトリからロードする実体。origin により検証ルールを変える
  // (外部プラグインのみ priority 0〜9999 と author 必須を強制、
  //  バンドル公式は priority 10000+ の予約域を許可)。
  void loadPluginsFromDirectory(const QDir& pluginDir,
                                ArchivePluginRecord::Origin origin);

  QList<std::shared_ptr<IArchivePlugin>> m_plugins;
  // 外部プラグインの QObject 実体は QPluginLoader が所有する。アプリ終了まで
  // 有効であり続けるよう loader 自体も保持する。
  QList<std::shared_ptr<QPluginLoader>>  m_pluginLoaders;
  ArchivePluginContext                   m_context;
  QList<ArchivePluginRecord>             m_records;
};

} // namespace Farman
