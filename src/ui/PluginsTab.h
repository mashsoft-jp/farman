#pragma once

#include <QMap>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QTableWidget;
class QToolButton;

namespace Farman {

class IViewerPlugin;

// 設定 → Plugins ページ。プラグインに関する設定と診断情報を 1 箇所に集約する:
//   - プラグインディレクトリ (旧 General → Viewer Plugins)
//   - プラグイン一覧: ロード状況 + 外部プラグインの有効 / 無効
//     (旧 Help → Plugins... ダイアログ。Help メニューからは本ページが開く)
//   - ビュアーの拡張子紐付け (旧 Viewers → Viewer Associations)
//
// 将来ビュアー以外のプラグイン種別 (Content / FS / Archive) が増えた場合は、
// 一覧の「Type」列の値を増やし、種別ごとの紐付け UI をグループとして
// 追加する方針 (SPEC.md プラグインシステム「拡張余地」参照)。
class PluginsTab : public QWidget {
  Q_OBJECT

public:
  explicit PluginsTab(QWidget* parent = nullptr);
  ~PluginsTab() override = default;

  void save();
  // 直前の save() で「次回起動から反映」の変更 (有効/無効・ディレクトリ) が
  // あったか。SettingsDialog が Apply/OK 後の通知に使う。
  bool restartRequiredOnSave() const { return m_restartRequiredOnSave; }

protected:
  // 2 つのテーブルを「1 つのフォーカス位置」として扱うための Tab 制御。
  // macOS では Tab フォーカスがテキスト系コントロール優先になる
  // (TabFocusTextControls) ため、既定の focusNextPrevChild に任せず
  // プラグイン一覧 → ビュアー関連付けの遷移を明示的に行う。
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void setupUi();
  void loadSettings();
  void loadPluginList();
  void loadAssociations();

  // 拡張子紐付けのヘルパー (旧 ViewersTab から移設)
  void addViewerRow(const QString& pluginId,
                    const QString& pluginName,
                    const QStringList& extensions,
                    const QStringList& defaultExtensions);
  QString normalizedExtension(const QString& extension) const;
  QStringList normalizedExtensions(const QString& text) const;
  QString extensionsTextForPlugin(const QMap<QString, QString>& associations,
                                  const QString& pluginId) const;
  QStringList defaultExtensionsForPlugin(IViewerPlugin* plugin) const;
  QStringList defaultExtensionsFromList(const QStringList& extensions) const;
  bool hasViewerRow(const QString& pluginId) const;

  // プラグインディレクトリ
  QLineEdit*   m_pluginsDirectoryEdit    = nullptr;
  QToolButton* m_pluginsDirectoryBrowse  = nullptr;
  QToolButton* m_pluginsDirectoryDefault = nullptr;

  // プラグイン一覧 (ロード状況 + 有効/無効)
  QTableWidget* m_pluginTable = nullptr;

  // ビュアーの拡張子紐付け
  QTableWidget* m_assocTable = nullptr;

  bool m_restartRequiredOnSave = false;
};

} // namespace Farman
