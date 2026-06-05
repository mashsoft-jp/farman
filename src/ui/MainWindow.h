#pragma once

#include <QMainWindow>
#include <QPointer>
#include "types.h"
#include "../settings/Settings.h"
#include "../core/UpdateChecker.h"
#include "../keybinding/CommandRegistry.h"
#include "../keybinding/KeyBindingManager.h"
#include "ViewerPanel.h"

class QAction;
class QStackedWidget;
class QLabel;
class QToolBar;

namespace Farman {

class FileManagerPanel;
class ShortcutListDialog;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

protected:
  void keyPressEvent(QKeyEvent* event) override;
  bool eventFilter(QObject* obj, QEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

private slots:
  void onFileActivated(const QString& filePath, const QString& displayPath);
  void onSettingsChanged();

private:
  void setupUi();
  void showFileManager();
  // displayPath: 空ならステータス・タイトルに filePath をそのまま使う。
  // アーカイブ内エントリ一時展開時のみ別物 (= "<archive>!/<inner>") を渡す。
  void showViewer(const QString& filePath, const QString& displayPath = {});
  // ビュアーを呼び出し側で固定して開く（任意ビュアー機能から使う）
  void showViewerWith(const QString& filePath, ViewerPanel::ViewerKind kind,
                      const QString& displayPath = {});
  void showSettingsDialog();
  void registerCommands();
  void createMenus();
  void showAboutDialog();
  void showPluginsDialog();
  // About ダイアログの「License Info...」から開く、同梱 OSS のライセンス全文
  // (Qt/LGPL, libarchive/BSD, uchardet/MPL) ビューア。
  void showThirdPartyLicenses();
  // Help メニュー → "Check for Updates..." の手動チェック。
  void checkForUpdatesManually();
  // 起動シーケンスから呼ばれる、24h スロットル付きの自動チェック。
  // Settings::autoUpdateCheckOnStartup が ON のときだけ実行する。
  void maybeCheckForUpdatesOnStartup();
  // UpdateAvailableDialog で "Update Now" が押されたあとのダウンロード +
  // インストール起動。完了で QApplication::quit() してインストール側に
  // 制御を移す。
  void startUpdateDownload(const Farman::ReleaseInfo& info);
  // ショートカット一覧ウィンドウのトグル表示。
  void toggleShortcutList();

  // どちらのパネルがアクティブかでステータスバーの表示元を切り替えるため、
  // それぞれの最新ステータスをキャッシュしておく
  void updateStatusBar();
  // ステータスバー右側のディスク使用量表示を更新する。
  // アクティブペインのカレントパスから QStorageInfo を引いて
  // 「N GB free / M GB (P% used)」を表示。5 秒タイマーでも自動再計算。
  void updateDiskStatus();
  QString m_fmStatusPath;
  QString m_fmStatusSummary;
  QString m_viewerStatusPath;
  QString m_viewerStatusSummary;

  QStackedWidget* m_stack;
  FileManagerPanel* m_fileManagerPanel;
  ViewerPanel* m_viewerPanel;
  // ステータスバー (左: フォーカス中ファイルの絶対パス / 中央: Sync Browse 状態 /
  // 右: 件数・選択要約)
  QLabel*      m_statusPathLabel        = nullptr;
  QLabel*      m_statusSummaryLabel     = nullptr;
  QLabel*      m_statusSyncBrowseLabel  = nullptr;
  // ディレクトリ比較モード中のインジケータ ("Compare: SizeMtime" 等)。
  // OFF 時は空文字列 (= 何も出ない)。
  QLabel*      m_statusCompareLabel     = nullptr;
  // アクティブペインのカレントが属するボリュームの使用量
  // (例: "245 GB free / 500 GB (51% used)")。ビュアー表示中は隠す。
  QLabel*      m_statusDiskLabel        = nullptr;
  // 5 秒ごとに disk 情報を再計算するためのポーリングタイマー
  // (コピー / 移動 / 削除後の空き容量変化に追従する)。
  QTimer*      m_diskUpdateTimer        = nullptr;

  // View メニューの Sync Browse トグル項目 (FileManagerPanel 状態と同期)。
  QAction*     m_syncBrowseAction = nullptr;
  // View メニューの Compare Directories... 項目 (チェックマークで状態同期)。
  QAction*     m_compareAction = nullptr;

  // ウィンドウタイトルのベース部分 (例: "farman 0.9.0")。Sync Browse が ON
  // のときにサフィックス "[Sync]" を追加するため、再構築用に保持しておく。
  QString      m_windowTitleBase;
  void         updateWindowTitle();

  // ショートカット一覧ウィンドウ (遅延生成)。
  ShortcutListDialog* m_shortcutListDialog = nullptr;
  // 更新チェッカ (Help → "Check for Updates..." 用)。初回 click で生成して
  // 以降は使い回す。起動時自動チェックでも同じインスタンスを使う。
  UpdateChecker* m_updateChecker = nullptr;
  // 直前の checkLatest() 起動が手動 (= "Check for Updates...") か自動かを覚えて
  // おく。失敗時のメッセージ表示 / Skip 抑止の挙動を切り替えるため。
  bool m_updateCheckIsManual = false;

  // UpdateChecker の生成 + signal 接続を共通化。
  void ensureUpdateChecker();
  // finished シグナルハンドラ。manual / 自動の両経路で共有。
  void onUpdateCheckFinished(bool ok, const Farman::ReleaseInfo& info,
                              bool isNewer, const QString& errorReason);

  // メインツールバー (View → Toolbar / Settings からトグル可能)。
  // CommandRegistry の既存コマンドを呼び出すボタンを並べる。表示/非表示は
  // Settings::showToolbar() に同期する。
  QToolBar* m_toolbar = nullptr;
  // ツールバー上の "Toolbar" メニュー項目をチェック付きで追跡する。
  // Settings 側からトグルが入ったときも aboutToShow / 直接呼び出し経由で
  // 同期させる。
  QAction*  m_toolbarMenuAction = nullptr;
  void      createMainToolBar();
  void      applyToolbarVisibility();

  // External モードのビュアーウィンドウ。同時に開けるのは 1 つだけ。
  // 別ファイルを開く要求が来たら、前のものを閉じてジオメトリを引き継いだ
  // 新規ウィンドウを作成する。WA_DeleteOnClose 付きなので X ボタンや Esc で
  // 閉じれば自動破棄され、QPointer は自動的に nullptr になる。
  QPointer<QMainWindow> m_externalViewerWindow;

  // Tools メニュー (外部アプリ連携)。UserCommandManager の userCommandsChanged で
  // rebuildToolsMenu() が呼ばれ、addCmd() ベースで再構築する。
  QMenu* m_toolsMenu = nullptr;
  void   rebuildToolsMenu();
  // rebuildToolsMenu() で生成した QAction を保持する。再構築時は古いものを
  // FileManagerPanel から removeAction してから deleteLater する。これをやらないと
  // 同一ショートカット (例: T) が複数の QAction に紐付いて
  // "Ambiguous shortcut overload" エラーで発火しなくなる。
  QList<QAction*> m_userCmdActions;
};

} // namespace Farman
