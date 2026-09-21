#pragma once

#include "core/PluginCatalog.h"

#include <QDialog>
#include <QList>

class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;

namespace Farman {

class EnterClickFilter;
class PluginDownloader;

// 公式プラグインの一覧から導入 / 更新するダイアログ (設定 → Plugins の
// 「公式プラグインを入手...」)。仕様は SPEC.md「プラグインのインストール」。
//
// 開いたときに PluginCatalog を更新し (1 時間キャッシュ)、各プラグインの最新版と
// 導入状況を並べる。「インストール」/「更新」でダウンロード → SHA256 照合 →
// PluginInstaller による検証と退避までを行う。反映は farman の再起動時。
class PluginCatalogDialog : public QDialog {
  Q_OBJECT

public:
  explicit PluginCatalogDialog(QWidget* parent = nullptr);
  ~PluginCatalogDialog() override;

  // このダイアログで 1 件でも導入 / 更新を退避したか。
  bool stagedAny() const { return m_stagedAny; }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void reject() override;

private:
  // 行ごとの導入状況と、押せる操作。
  struct RowState {
    enum class Action { None, Install, Update, Reinstall };
    Action  action = Action::None;
    QString statusText;
    QString installedVersion;      // ロード済みの版数 (不明なら空)
    QStringList installedFiles;    // 導入済みの同じプラグインのファイル名
  };

  void setupUi();
  void reloadRows();
  RowState rowStateFor(const PluginCatalogEntry& entry) const;
  void updateDetails();
  void runRowAction(int row);
  void onDownloadFinished(bool ok, const QString& filePath, const QString& errorReason);
  void setBusy(bool busy, const QString& message = QString());
  static QString uiLanguage();

  QLabel*       m_sourceLabel   = nullptr;
  QTableWidget* m_table         = nullptr;
  QLabel*       m_detailsLabel  = nullptr;
  QLabel*       m_busyLabel     = nullptr;
  QProgressBar* m_progressBar   = nullptr;
  QPushButton*  m_reloadButton  = nullptr;
  QPushButton*  m_closeButton   = nullptr;
  EnterClickFilter* m_enterClickFilter = nullptr;

  PluginDownloader* m_downloader = nullptr;
  QList<PluginCatalogEntry> m_entries;
  QList<RowState>           m_rowStates;
  PluginCatalogEntry        m_downloadingEntry;
  bool m_busy = false;
  bool m_stagedAny = false;
};

} // namespace Farman
