#pragma once

#include "viewer/ViewerDispatcher.h"

#include <QList>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QToolButton;

namespace Farman {

class IViewerPlugin;

// 設定 → Plugins ページ。プラグインに関する設定と診断情報を 1 箇所に集約する:
//   - プラグインディレクトリ (旧 General → Viewer Plugins)
//   - インストール済みプラグイン: 種別ごとのタブに分けた一覧。ロード状況 +
//     外部プラグインの有効 / 無効。各行の「詳細...」ダイアログで区分・
//     プラグイン ID・パス・エラー全文を確認でき、ビュアーは拡張子の紐付け
//     もそこで編集する (旧 Help → Plugins... ダイアログと
//     旧 Viewers → Viewer Associations を統合)
//
// 現状はビュアープラグインのみなのでタブは「Viewer」だけ。将来ビュアー以外
// のプラグイン種別 (Content / FS / Archive) が増えたら種別ごとにタブを追加
// して出し分ける方針 (SPEC.md プラグインシステム「拡張余地」参照)。
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
  // プラグイン一覧のキー操作の制御:
  //   - Enter / Space で選択行の詳細ダイアログを開く
  //   - 行移動は ↑/↓ のみ (←/→ は無効化)
  //   - Tab は「詳細...」ボタンに止まらず設定ダイアログの OK ボタンへ抜ける
  //     (macOS は TabFocusTextControls で Tab 先がテキスト系優先になるため、
  //      既定の focusNextPrevChild に任せず明示的に遷移する)
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void setupUi();
  void loadSettings();
  void loadPluginList();
  void loadExtensionState();

  // プラグイン一覧の「詳細...」ダイアログ。一覧には最低限の列しか出さない
  // ので、区分・プラグイン ID・パス・エラー全文はこちらで見せる。
  // 外部プラグインの有効 / 無効の切り替えと、ビュアーの拡張子紐付けの
  // 確認・変更もここで行う (一覧は表示のみ)。
  void showPluginDetails(int row);
  QString pluginStatusText(const PluginRecord& record) const;
  QString pluginStatusEmoji(const PluginRecord& record) const;
  QString extensionsDisplayText(const PluginRecord& record) const;
  void updatePluginTablePalette(bool focused);

  // 拡張子紐付けのヘルパー (旧 ViewersTab から移設)
  QString normalizedExtension(const QString& extension) const;
  QStringList normalizedExtensions(const QString& text) const;
  QString extensionsTextForPlugin(const QMap<QString, QString>& associations,
                                  const QString& pluginId) const;
  QStringList defaultExtensionsForPlugin(IViewerPlugin* plugin) const;
  QStringList defaultExtensionsFromList(const QStringList& extensions) const;

  // プラグインディレクトリ
  QLineEdit*   m_pluginsDirectoryEdit    = nullptr;
  QToolButton* m_pluginsDirectoryBrowse  = nullptr;
  QToolButton* m_pluginsDirectoryDefault = nullptr;

  // インストール済みプラグイン (種別ごとのタブ)
  QTabWidget*   m_pluginTabs  = nullptr;
  QTableWidget* m_pluginTable = nullptr;  // Viewer タブの一覧
  // 一覧の行番号 → レコード。詳細ダイアログの表示に使う。
  QList<PluginRecord> m_pluginRecords;

  // 外部プラグインの有効 / 無効の編集状態 (無効化する pluginId の集合)。
  // 詳細ダイアログで編集し、save() で Settings に書き戻す。
  QSet<QString> m_disabledPluginIds;

  // ビュアーの拡張子紐付けの編集状態 (詳細ダイアログで編集し save() で保存)。
  // m_extensionOrder は save() の競合解決 (同じ拡張子は先勝ち) の優先順。
  QStringList m_extensionOrder;
  QMap<QString, QStringList> m_extensions;          // pluginId → 現在値
  QMap<QString, QStringList> m_extensionDefaults;   // pluginId → 既定値
  // 一覧に存在しないプラグインへの紐付け (拡張子 → pluginId)。編集対象外
  // なので save() でそのまま書き戻す。
  QMap<QString, QString> m_preservedAssociations;

  bool m_restartRequiredOnSave = false;
};

} // namespace Farman
