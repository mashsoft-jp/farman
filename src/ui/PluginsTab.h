#pragma once

#include "core/PluginCatalog.h"
#include "core/PluginInstaller.h"

#include <QList>
#include <QString>
#include <QWidget>

class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace Farman {

class EnterClickFilter;
class PluginDownloader;

// 設定 → Plugins ページ。外部プラグインの「導入と管理」を、ビュアー / アーカイブの
// 種別を問わず 1 箇所で扱う (仕様は SPEC.md「プラグインのインストール」):
//   - 外部プラグインを読み込むか (全体に関わるスイッチなので一番上)
//   - 外部プラグインの一覧 (全種別)。導入済みのものに加えて、未導入の公式プラグインも
//     並べる。行ごとに「詳細...」/「更新する」(公式のみ。未導入なら「インストール」) /
//     「アンインストール...」
//   - 公式プラグインの更新確認 (GitHub Releases。PluginCatalog)
//   - ファイルのドラッグ＆ドロップ / 「ファイルからインストール...」による導入
//   - 再起動待ちの変更があるときの「再起動」ボタン
//   - どこから読み込むか (プラグインディレクトリ。一番下)
//
// 導入後の設定 (有効 / 無効・ファイルパターン・プラグイン固有の設定) は、従来どおり
// 「ビュアー」「アーカイブ」の各ページが持つ。同梱プラグインもそちらにだけ出る。
//
// 検証と退避は PluginInstaller、公式プラグインの一覧とダウンロードは PluginCatalog /
// PluginDownloader が行い、ここは確認と結果表示を担う。
class PluginsTab : public QWidget {
  Q_OBJECT

public:
  explicit PluginsTab(QWidget* parent = nullptr);
  ~PluginsTab() override;

  void save();

  // 直前の save() でプラグインの読込み設定 (外部プラグインの許可 /
  // プラグインディレクトリ) が変更されたか。どちらも起動時に一括ロードする
  // 都合で次回起動から反映されるため、SettingsDialog が再起動を確認する。
  bool pluginLoadSettingsChangedOnSave() const {
    return m_pluginLoadSettingsChangedOnSave;
  }

signals:
  // 「再起動」ボタンが押された。SettingsDialog が設定を保存してから再起動する。
  void restartRequested();

protected:
  // ページのどこにファイルを落としても導入できる (一覧などの子ウィジェットは
  // ドロップを受けないので、ドラッグイベントはこのページまで上がってくる)。
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  // ページが初めて表示されたときに、公式プラグインの更新を確認する。
  void showEvent(QShowEvent* event) override;
  // 一覧と行のボタンのキー操作 (Enter / Space で詳細、Tab は選択行のボタンへ)。
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  // 公式プラグインの行で「更新する」/「インストール」が押せるかどうか。
  enum class UpdateState {
    NotOfficial,      // 非公式 (ボタンを出さない)
    Unknown,          // まだ最新リリースを確認していない / 確認中
    Unavailable,      // 確認できなかった / この環境向けの配布物が無い / farman が古い
    NotInstalled,     // 未導入 → 「インストール」
    UpdateAvailable,  // 導入済みより新しい版がある → 「更新する」
    VersionUnknown,   // 導入済みだがロードされておらず版数不明 → 「更新する」(入れ直し)
    UpToDate,         // 最新
  };

  // 一覧の 1 行。導入済みの外部プラグイン、導入待ちの新規ファイル、未導入の公式プラグイン。
  struct Row {
    PluginInstaller::Kind kind = PluginInstaller::Kind::Unknown;
    QString filePath;      // 導入済みのパス。それ以外は空
    QString relPath;       // プラグインディレクトリからの相対パス (管理外 / 未導入は空)
    // この行を置き換える導入待ちファイルの相対パス。同名の上書きなら relPath と同じ、
    // 別名の新しい版なら別のパス、導入待ちの新規ファイルの行ではそのファイル自身。
    QString pendingInstallRelPath;
    QString pluginId;
    QString name;
    QString version;
    QString author;
    QString authorUrl;
    QString errorReason;
    bool    loaded = false;
    bool    disabledByUser = false;
    bool    blockedExternalDisabled = false;
    bool    pendingInstall = false;   // 新規導入 or 更新が退避中
    bool    pendingRemoval = false;   // 削除が退避中
    bool    installed = false;        // 既にどこかに導入されている (ロードを試みた)

    int         catalogIndex = -1;    // 公式なら m_catalog の添字
    UpdateState updateState = UpdateState::NotOfficial;
    QString     updateNote;           // Unavailable の理由など (ツールチップ / 詳細に出す)
  };

  void setupUi();
  void loadSettings();
  // 退避状況と公式プラグインの情報を読み直して、一覧と再起動バナーを作り直す。
  void reloadList();
  QList<Row> collectRows() const;
  void applyCatalog(QList<Row>* rows) const;
  QString statusEmoji(const Row& row) const;
  QString statusText(const Row& row) const;
  QString updateStateText(const Row& row) const;
  static QString uiLanguage();

  void setDropZoneActive(bool active);
  void chooseFiles();
  // 確認 → 検証 → 退避 → 結果表示までを行う (ファイルからの導入)。
  void installFiles(const QStringList& filePaths);
  // 外部プラグインの読込みが OFF なら、ON にするか尋ねる (導入しても動かないため)。
  void offerEnableExternalPlugins();

  // 行のボタン。
  void showRowDetails(int row);
  void runRowUpdate(int row);       // 公式プラグインの更新 / インストール
  void runRowUninstall(int row);    // アンインストール、または退避の取り消し
  // 一覧を作り直した後、同じ位置のボタン (無ければ一覧) にフォーカスを戻す。
  void reloadListKeepingFocus(int row, int column);
  QPushButton* rowButton(int row, int column) const;

  // 公式プラグインの更新確認とダウンロード。
  void checkForUpdates(bool force);
  void onCatalogUpdated();
  void onDownloadFinished(bool ok, const QString& filePath, const QString& errorReason);
  void setBusy(bool busy, const QString& message = QString());

  // ── 読込み設定 (旧: General タブの Plugins グループ) ──
  QCheckBox*   m_allowExternalPluginsCheck = nullptr;
  QLineEdit*   m_pluginsDirectoryEdit      = nullptr;
  QToolButton* m_pluginsDirectoryBrowse    = nullptr;
  QToolButton* m_pluginsDirectoryOpen      = nullptr;
  QToolButton* m_pluginsDirectoryDefault   = nullptr;
  bool         m_pluginLoadSettingsChangedOnSave = false;

  // ── 外部プラグインの一覧と導入 ──
  QTableWidget* m_table         = nullptr;
  QPushButton*  m_installButton = nullptr;
  QPushButton*  m_checkButton   = nullptr;   // 「更新を確認」
  QLabel*       m_checkLabel    = nullptr;   // 確認の状況 (同梱一覧へのフォールバックなど)
  QLabel*       m_busyLabel     = nullptr;
  QProgressBar* m_progressBar   = nullptr;
  // 点線枠のドロップ領域 (DropZoneFrame)。ドラッグ中は強調表示にする。
  QFrame*       m_dropZone      = nullptr;
  QList<Row>    m_rows;
  QList<PluginCatalogEntry> m_catalog;
  // ボタンにフォーカスがあるときの Enter を「そのボタンを押す」にする。何もしないと
  // Enter はダイアログの既定ボタン (OK) に届いて設定ダイアログが閉じてしまう。
  EnterClickFilter* m_enterClickFilter = nullptr;

  PluginDownloader*  m_downloader = nullptr;
  PluginCatalogEntry m_downloadingEntry;
  bool m_busy = false;
  bool m_checkedOnShow = false;

  // ── 再起動待ちの変更があるときだけ出すバナー ──
  QFrame*      m_restartBanner = nullptr;
  QPushButton* m_restartButton = nullptr;
};

} // namespace Farman
