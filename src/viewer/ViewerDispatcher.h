#pragma once

#include "IViewerPlugin.h"
#include <QObject>
#include <QDir>
#include <memory>

class QPluginLoader;

namespace Farman {

// 1 つのプラグイン (同梱公式 or 外部) のロード結果。
// Help → Plugins... ダイアログで一覧表示するために ViewerDispatcher が保持する。
struct PluginRecord {
  enum class Origin { Bundled, External };
  Origin  origin;
  QString filePath;     // 動的ロード元 path。Bundled / External とも実 path。
  QString pluginId;     // 失敗時は (取得できれば) id、無理なら空
  QString pluginName;   // 同上
  QString version;      // プラグイン自身の配布バージョン (取得できた場合のみ)
  QString author;       // 制作者名 / 組織名 (取得できた場合のみ)
  QString authorUrl;    // 制作者の URL (任意。取得できた場合のみ)
  QStringList supportedExtensions;  // 取得できた場合のみ。無効化行の表示にも使う。
  int     priority = -1;            // 取得できた場合のみ (-1 = 不明)。0 が最優先。
  bool    loaded   = false;
  bool    disabledByUser = false;
  // 外部プラグインだが設定「外部プラグインの読込みを許可する」が OFF のため
  // ロードしなかった (dlopen もしていない) 場合に true。一覧表示用。
  bool    blockedExternalDisabled = false;
  QString errorReason;  // loaded == false のときだけ非空
};

class ViewerDispatcher : public QObject {
  Q_OBJECT

public:
  static ViewerDispatcher& instance();

  // プラグインに渡す PluginContext のテンプレートを設定する。
  // registerBundledPlugins / loadPlugins より前に呼ぶこと。
  // 設定された ctx は initialize() / createViewer() に渡される。
  void setContext(const PluginContext& ctx) { m_context = ctx; }
  const PluginContext& pluginContext() const { return m_context; }

  // 現在の Settings / QApplication palette からプラグイン向け Appearance を
  // 作る。起動時コンテキストと変更通知の両方で同じ変換を使う。
  static PluginAppearance currentAppearance();

  // Settings 変更や OS ライト / ダーク切替に合わせて、登録済みプラグインへ
  // Appearance の再適用を通知する。
  void notifyAppearanceChanged(const PluginAppearance& appearance);

  // 同梱公式ビュアープラグインの登録（起動時に呼ぶ）
  void registerBundledPlugins();

  // 登録済みプラグインの shutdown() を呼んでからアンロードする。
  // IViewerPlugin のライフサイクル契約 (initialize 成功後、アンロード前に
  // shutdown を 1 回呼ぶ) を守るため、QApplication が生きているうちに
  // main() の app.exec() 後に呼ぶ。
  void shutdownPlugins();

  // プラグインディレクトリからロード
  void loadPlugins(const QDir& pluginDir);

  // このファイルを開けるプラグインを返す (無ければ nullptr)。
  //
  // filePath:    実際に読み込むディスク上のパス。存在チェックと、MIME
  //              フォールバックの内容判定に使う。
  // routingPath: 名前で振り分けるときに使うパス。空なら filePath を使う。
  //              アーカイブ内エントリは一時展開したファイルを開くので、実体の
  //              名前ではなく "x.zip!/inner.txt" の側で振り分ける必要がある。
  //              このパスはディスク上に存在しなくてよい。
  IViewerPlugin* resolvePlugin(const QString& filePath,
                               const QString& routingPath = QString()) const;

  // pluginId からロード済みプラグイン本体を返す (無ければ nullptr)。
  // PluginsTab の詳細ダイアログが設定 UI を呼び出すのに使う。
  IViewerPlugin* pluginById(const QString& pluginId) const;

  // ビュアーウィジェットを生成して返す
  // 対応ビュアーがない場合は BinaryViewer にフォールバック
  QWidget* createViewer(
    const QString& filePath,
    QWidget*       parent = nullptr
  ) const;

  // 全登録プラグイン一覧
  QList<IViewerPlugin*> allPlugins() const;

  // 全ビュアーの「設定可能なショートカット項目」を取得用共通 API
  // (IViewerPlugin::shortcutCommands) 経由で集約する。本体のキーバインド設定
  // (KeybindingTab) がビュアー別セクションを構築するのに使う。commandId 重複は
  // 除去し、カタログの標準順 (viewerCommandViewerIds) で返す。
  QList<ViewerCommandDef> aggregatedShortcutCommands() const;

  // 登録済み pluginId が外部プラグイン由来かどうか。
  bool isExternalPlugin(const QString& pluginId) const;

  // 全プラグイン (同梱公式 + 外部) のロード結果ログ。
  // Help → Plugins... ダイアログがこれを使って状態を表示する。
  // 成功した同梱公式 + 外部ロード試行 (成功 / 失敗) が全部入る。
  QList<PluginRecord> pluginRecords() const { return m_records; }

private:
  explicit ViewerDispatcher(QObject* parent = nullptr);
  void loadPluginsFromDirectory(const QDir& pluginDir, PluginRecord::Origin origin);
  QStringList bundledPluginDirectories() const;
  // filePath: 動的ロード元 path。レコード生成と明示選択の解決に使う。
  void registerPlugin(std::shared_ptr<IViewerPlugin> plugin,
                      const QString& filePath,
                      PluginRecord::Origin origin);

  QList<std::shared_ptr<IViewerPlugin>> m_plugins;
  // 外部プラグインの QObject 実体は QPluginLoader が所有する。ロード済み
  // プラグインがアプリ終了まで有効であり続けるよう loader 自体も保持する。
  QList<std::shared_ptr<QPluginLoader>> m_pluginLoaders;
  // 全プラグインに渡す共通コンテキスト。registerPlugin 内で initialize() に
  // 渡し、createViewer() でも渡す。
  PluginContext m_context;

  // 全プラグイン (同梱公式 + 外部) のロード結果。Help ダイアログで表示用。
  QList<PluginRecord> m_records;
};

} // namespace Farman
