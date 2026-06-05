#pragma once

#include "IViewerPlugin.h"
#include <QObject>
#include <QDir>
#include <memory>

class QPluginLoader;

namespace Farman {

// 1 つのプラグイン (組み込み or 外部) のロード結果。
// Help → Plugins... ダイアログで一覧表示するために ViewerDispatcher が保持する。
struct PluginRecord {
  enum class Origin { Builtin, External };
  Origin  origin;
  QString filePath;     // External のみ実 path。Builtin は空。
  QString pluginId;     // 失敗時は (取得できれば) id、無理なら空
  QString pluginName;   // 同上
  bool    loaded   = false;
  QString errorReason;  // loaded == false のときだけ非空
};

class ViewerDispatcher : public QObject {
  Q_OBJECT

public:
  static ViewerDispatcher& instance();

  // プラグインに渡す PluginContext のテンプレートを設定する。
  // registerBuiltins / loadPlugins より前に呼ぶこと。
  // 設定された ctx は initialize() / createViewer() に渡される。
  void setContext(const PluginContext& ctx) { m_context = ctx; }

  // 組み込みビュアーの登録（起動時に呼ぶ）
  void registerBuiltins();

  // プラグインディレクトリからロード
  void loadPlugins(const QDir& pluginDir);

  // ファイルに対応するビュアーを返す（nullptr = 対応なし）
  IViewerPlugin* resolvePlugin(const QString& filePath) const;

  // ビュアーウィジェットを生成して返す
  // 対応ビュアーがない場合は BinaryViewer にフォールバック
  QWidget* createViewer(
    const QString& filePath,
    QWidget*       parent = nullptr
  ) const;

  // 全登録プラグイン一覧
  QList<IViewerPlugin*> allPlugins() const;

  // 全プラグイン (組み込み + 外部) のロード結果ログ。
  // Help → Plugins... ダイアログがこれを使って状態を表示する。
  // 成功した組み込み 3 種 + 外部ロード試行 (成功 / 失敗) が全部入る。
  QList<PluginRecord> pluginRecords() const { return m_records; }

private:
  explicit ViewerDispatcher(QObject* parent = nullptr);
  // filePath: 外部プラグインの実 path。組み込みは空文字。レコード生成のために
  // 使う (失敗時 / 成功時とも m_records にエントリを 1 つ追加)。
  void registerPlugin(std::shared_ptr<IViewerPlugin> plugin,
                      const QString& filePath = QString());

  QList<std::shared_ptr<IViewerPlugin>> m_plugins;
  // 外部プラグインの QObject 実体は QPluginLoader が所有する。ロード済み
  // プラグインがアプリ終了まで有効であり続けるよう loader 自体も保持する。
  QList<std::shared_ptr<QPluginLoader>> m_pluginLoaders;
  // 全プラグインに渡す共通コンテキスト。registerPlugin 内で initialize() に
  // 渡し、createViewer() でも渡す。
  PluginContext m_context;

  // 全プラグイン (組み込み + 外部) のロード結果。Help ダイアログで表示用。
  QList<PluginRecord> m_records;
};

} // namespace Farman
