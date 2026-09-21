#pragma once

#include "core/PluginInstaller.h"

#include <QList>
#include <QString>
#include <QWidget>

class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace Farman {

class EnterClickFilter;

// 設定 → Plugins ページ。外部プラグインの「導入と管理」を、ビュアー / アーカイブの
// 種別を問わず 1 箇所で扱う (仕様は SPEC.md「プラグインのインストール」):
//   - 外部プラグインを読み込むか (全体に関わるスイッチなので一番上)
//   - 導入済みの外部プラグインの一覧 (全種別) とアンインストール
//   - ファイルのドラッグ＆ドロップ / 「ファイルからインストール...」による導入
//   - 再起動待ちの変更があるときの「再起動」ボタン
//   - どこから読み込むか (プラグインディレクトリ。一番下)
//
// 導入後の設定 (有効 / 無効・ファイルパターン・プラグイン固有の設定) は、従来どおり
// 「ビュアー」「アーカイブ」の各ページが持つ。同梱プラグインもそちらにだけ出る。
//
// 検証と退避そのものは PluginInstaller が行い、ここは確認と結果表示を担う。
class PluginsTab : public QWidget {
  Q_OBJECT

public:
  explicit PluginsTab(QWidget* parent = nullptr);
  ~PluginsTab() override = default;

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
  // 一覧のキー操作: Enter / Space で選択行の操作ボタンを押す。操作ボタンには
  // Tab でもフォーカスが当たる (一覧 → 各行のボタン → 「ファイルからインストール...」)。
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  // 一覧の 1 行。導入済みの外部プラグイン、または導入待ちの新規ファイル。
  struct Row {
    PluginInstaller::Kind kind = PluginInstaller::Kind::Unknown;
    QString filePath;      // 導入済みのパス。導入待ちの新規ファイルは空
    QString relPath;       // プラグインディレクトリからの相対パス (管理外は空)
    // この行を置き換える導入待ちファイルの相対パス。同名の上書きなら relPath と同じ、
    // 別名の新しい版なら別のパス、導入待ちの新規ファイルの行ではそのファイル自身。
    QString pendingInstallRelPath;
    QString name;
    QString version;
    QString author;
    QString errorReason;
    bool    loaded = false;
    bool    disabledByUser = false;
    bool    blockedExternalDisabled = false;
    bool    pendingInstall = false;   // 新規導入 or 更新が退避中
    bool    pendingRemoval = false;   // 削除が退避中
    bool    installed = false;        // 既にプラグインディレクトリにある
  };

  void setupUi();
  void loadSettings();
  // 退避状況を読み直して一覧と再起動バナーを作り直す。
  void reloadList();
  QList<Row> collectRows() const;
  QString statusEmoji(const Row& row) const;
  QString statusText(const Row& row) const;

  void setDropZoneActive(bool active);
  void chooseFiles();
  // 公式プラグインの一覧ダイアログを開く。
  void openCatalog();
  // 外部プラグインの読込みが OFF なら、ON にするか尋ねる (導入しても動かないため)。
  void offerEnableExternalPlugins();
  // 確認 → 検証 → 退避 → 結果表示までを行う。
  void installFiles(const QStringList& filePaths);
  // 行の操作ボタン: 退避中なら取り消し、そうでなければアンインストール。
  void runRowAction(int row);

  // ── 読込み設定 (旧: General タブの Plugins グループ) ──
  QCheckBox*   m_allowExternalPluginsCheck = nullptr;
  QLineEdit*   m_pluginsDirectoryEdit      = nullptr;
  QToolButton* m_pluginsDirectoryBrowse    = nullptr;
  QToolButton* m_pluginsDirectoryOpen      = nullptr;
  QToolButton* m_pluginsDirectoryDefault   = nullptr;
  bool         m_pluginLoadSettingsChangedOnSave = false;

  // ── 外部プラグインの一覧と導入 ──
  QTableWidget* m_table         = nullptr;
  QLabel*       m_emptyLabel    = nullptr;
  QPushButton*  m_installButton = nullptr;
  QPushButton*  m_catalogButton = nullptr;
  // 点線枠のドロップ領域 (DropZoneFrame)。ドラッグ中は強調表示にする。
  QFrame*       m_dropZone      = nullptr;
  QList<Row>    m_rows;
  // ボタンにフォーカスがあるときの Enter を「そのボタンを押す」にする。何もしないと
  // Enter はダイアログの既定ボタン (OK) に届いて設定ダイアログが閉じてしまう。
  EnterClickFilter* m_enterClickFilter = nullptr;

  // ── 再起動待ちの変更があるときだけ出すバナー ──
  QFrame*      m_restartBanner = nullptr;
  QPushButton* m_restartButton = nullptr;
};

} // namespace Farman
