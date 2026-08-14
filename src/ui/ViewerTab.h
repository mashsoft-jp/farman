#pragma once

#include "viewer/ViewerDispatcher.h"

#include <QList>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace Farman {

class IViewerPlugin;

// 設定 → Viewer ページ。ビュアープラグインに関する診断情報と設定を集約する:
//   - インストール済みビュアープラグインの一覧 (ロード状況 + 有効 / 無効)
//   - 各行の「詳細...」ダイアログで区分・プラグイン ID・パス・エラー全文を確認し、
//     拡張子の紐付けと、プラグインが持つ設定ページもそこで編集する
//
// プラグインの置き場所 (外部プラグインの読込み許可・プラグインディレクトリ) は
// ビュアー / アーカイブに共通なので「全般」タブが持つ。アーカイブプラグインは
// アーカイブ形式の一種として「アーカイブ」タブが管理する。
class ViewerTab : public QWidget {
  Q_OBJECT

public:
  explicit ViewerTab(QWidget* parent = nullptr);
  ~ViewerTab() override = default;

  void save();
  // 直前の save() で「次回起動から反映」の変更 (有効/無効) があったか。
  // SettingsDialog が Apply/OK 後の通知に使う。
  bool restartRequiredOnSave() const { return m_restartRequiredOnSave; }

protected:
  // プラグイン一覧のキー操作の制御:
  //   - Enter で選択行の詳細ダイアログ、Space で有効 / 無効のトグル
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
  // 有効 / 無効の切り替えと拡張子紐付けの確認・変更もここで行う (一覧は表示
  // のみ)。設定 UI を持つプラグインは設定ページをこの中に埋め込む。
  void showPluginDetails(int row);
  // 有効 / 無効の変更を編集状態と一覧表示の両方へ反映する。一覧のチェック
  // ボックスと詳細ダイアログの両方から呼ぶ。
  void setPluginEnabled(int row, bool enabled);
  QString pluginStatusText(const PluginRecord& record) const;
  QString pluginStatusEmoji(const PluginRecord& record) const;
  QString extensionsDisplayText(const PluginRecord& record) const;
  void updatePluginTablePalette(bool focused);
  // m_disabledPluginIds は小文字正規化済み。ロード側
  // (Settings::isViewerPluginDisabled) の case-insensitive 判定に合わせる。
  bool isPluginDisabled(const QString& pluginId) const {
    return m_disabledPluginIds.contains(pluginId.trimmed().toLower());
  }

  // 拡張子紐付けのヘルパー (旧 ViewersTab から移設)
  QString normalizedExtension(const QString& extension) const;
  QStringList normalizedExtensions(const QString& text) const;
  QStringList defaultExtensionsForPlugin(IViewerPlugin* plugin) const;
  QStringList defaultExtensionsFromList(const QStringList& extensions) const;

  QTableWidget* m_pluginTable = nullptr;
  // 一覧の行番号 → レコード。詳細ダイアログの表示に使う。
  QList<PluginRecord> m_pluginRecords;

  // プラグインの有効 / 無効の編集状態 (無効化する pluginId の集合、
  // 小文字正規化済み)。詳細ダイアログで編集し、save() で Settings に書き戻す。
  QSet<QString> m_disabledPluginIds;

  // ビュアーの拡張子紐付けの編集状態 (詳細ダイアログで編集し save() で保存)。
  // m_extensionOrder は save() の競合解決 (同じ拡張子は先勝ち) の優先順。
  QStringList m_extensionOrder;
  QMap<QString, QStringList> m_extensions;          // pluginId → 現在値
  QMap<QString, QStringList> m_extensionDefaults;   // pluginId → 既定値
  // 一覧に存在しないプラグインのパターン (pluginId → パターン一覧)。編集
  // 対象外なので save() でそのまま書き戻す。
  QMap<QString, QStringList> m_preservedPatterns;

  // 一覧を作り直している間は itemChanged をユーザー操作として扱わない。
  bool m_populating = false;

  bool m_restartRequiredOnSave = false;
};

} // namespace Farman
