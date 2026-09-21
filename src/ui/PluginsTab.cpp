#include "PluginsTab.h"

#include "core/ArchiveDispatcher.h"
#include "core/PluginDownloader.h"
#include "core/UpdateChecker.h"
#include "settings/Settings.h"
#include "utils/Dialogs.h"
#include "utils/EnterClickFilter.h"
#include "utils/PluginCompat.h"
#include "viewer/ViewerDispatcher.h"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMimeData>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace Farman {

namespace {

enum Column {
  ColStatus = 0, ColName, ColKind, ColVersion, ColLatest,
  ColDetails, ColUpdate, ColUninstall, ColCount
};
// 行のボタンが並ぶ列 (Tab で辿る順)。
constexpr int kButtonColumns[] = {ColDetails, ColUpdate, ColUninstall};
constexpr const char* kRowProperty = "pluginRow";
constexpr const char* kColumnProperty = "pluginColumn";

// ドロップされた MIME からローカルファイルのパスだけを取り出す。
QStringList localFilesFromMime(const QMimeData* mime) {
  QStringList files;
  if (!mime || !mime->hasUrls()) return files;
  const QList<QUrl> urls = mime->urls();
  for (const QUrl& url : urls) {
    if (!url.isLocalFile()) continue;
    const QString path = url.toLocalFile();
    if (QFileInfo(path).isFile()) {
      files.append(path);
    }
  }
  return files;
}

QString kindLabel(PluginInstaller::Kind kind) {
  switch (kind) {
    case PluginInstaller::Kind::Viewer:  return PluginsTab::tr("Viewer");
    case PluginInstaller::Kind::Archive: return PluginsTab::tr("Archive");
    case PluginInstaller::Kind::Unknown: break;
  }
  return QString();
}

// Web のファイルアップロード欄のような、点線枠のドロップ領域。ドラッグ中は強調する。
// 色はパレットから描くので、Light / Dark の切り替えにもそのまま追従する。
class DropZoneFrame : public QFrame {
public:
  using QFrame::QFrame;

  void setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    update();
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QColor highlight = pal.color(QPalette::Highlight);
    QColor fill = highlight;
    fill.setAlpha(m_active ? 40 : 0);
    QPen pen(m_active ? highlight : pal.color(QPalette::PlaceholderText));
    pen.setWidthF(m_active ? 2.0 : 1.5);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(fill);
    const qreal inset = pen.widthF() / 2.0 + 0.5;
    painter.drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset), 8, 8);
  }

private:
  bool m_active = false;
};

PluginInstaller::Kind kindOfRelPath(const QString& relPath) {
  const QString sub = relPath.section(QLatin1Char('/'), 0, 0);
  if (sub == PluginInstaller::kindSubdir(PluginInstaller::Kind::Viewer)) {
    return PluginInstaller::Kind::Viewer;
  }
  if (sub == PluginInstaller::kindSubdir(PluginInstaller::Kind::Archive)) {
    return PluginInstaller::Kind::Archive;
  }
  return PluginInstaller::Kind::Unknown;
}

} // namespace

PluginsTab::PluginsTab(QWidget* parent)
  : QWidget(parent) {
  setAcceptDrops(true);

  m_downloader = new PluginDownloader(this);
  connect(m_downloader, &PluginDownloader::progress, this,
          [this](qint64 received, qint64 total) {
    m_progressBar->setRange(0, total > 0 ? 1000 : 0);
    if (total > 0) {
      m_progressBar->setValue(static_cast<int>(received * 1000 / total));
    }
  });
  connect(m_downloader, &PluginDownloader::finished, this,
          &PluginsTab::onDownloadFinished);
  connect(&PluginCatalog::instance(), &PluginCatalog::updated, this,
          &PluginsTab::onCatalogUpdated);

  setupUi();
  loadSettings();
}

PluginsTab::~PluginsTab() {
  // ダウンロード中に設定ダイアログが閉じられたら中断する (照合前のファイルは残さない)。
  m_downloader->cancel();
}

QString PluginsTab::uiLanguage() {
  // main.cpp の翻訳ロードと同じ言語解決 ("ja_JP" → "ja")。
  QString lang;
  switch (Settings::instance().language()) {
    case LanguageMode::English:  lang = QStringLiteral("en"); break;
    case LanguageMode::Japanese: lang = QStringLiteral("ja"); break;
    case LanguageMode::Auto:     lang = QLocale::system().name(); break;
  }
  return lang.section(QLatin1Char('_'), 0, 0);
}

void PluginsTab::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);
  m_enterClickFilter = new EnterClickFilter(this);

  // ── 外部プラグインを読み込むか ──
  // 外部プラグイン全体に関わるスイッチなので、ページの一番上に置く (下の一覧 / 導入 /
  // ディレクトリはすべてこれが ON のときに意味を持つ)。個々のプラグインの有効 / 無効は
  // 「ビュアー」「アーカイブ」の各タブが持つ。
  m_allowExternalPluginsCheck =
    new QCheckBox(tr("Allow loading external plugins"), this);
  m_allowExternalPluginsCheck->setToolTip(
    tr("When enabled, plugins placed in the directory below are loaded at "
       "startup. External plugins are third-party native code and run with "
       "the same privileges as Farman — only enable this if you trust their "
       "source. Changes take effect on next launch."));
  mainLayout->addWidget(m_allowExternalPluginsCheck);

  QLabel* pluginSecurityHint = new QLabel(
    tr("⚠ External plugins are native code and run with full application "
       "privileges. Only enable plugins from sources you trust."), this);
  pluginSecurityHint->setWordWrap(true);
  pluginSecurityHint->setEnabled(false);
  mainLayout->addWidget(pluginSecurityHint);

  // ── 外部プラグインの一覧と導入 ──
  auto* listGroup = new QGroupBox(tr("External Plugins"), this);
  auto* listLayout = new QVBoxLayout(listGroup);

  auto* listHint = new QLabel(
    tr("Install, update and uninstall external plugins here, for viewers and "
       "archives alike. Official plugins that are not installed yet are listed "
       "too. Changes take effect after restarting farman. To turn a plugin on or "
       "off or change its settings, use the Viewer / Archive pages."),
    listGroup);
  listHint->setWordWrap(true);
  listLayout->addWidget(listHint);

  // 公式プラグインの更新確認。結果は一覧の「最新版」列と「更新する」ボタンに出る。
  auto* checkRow = new QHBoxLayout();
  m_checkButton = new QPushButton(tr("Check for Updates"), listGroup);
  m_checkButton->setAutoDefault(false);
  m_checkButton->setToolTip(
    tr("Look up the latest versions of the official plugins on the internet."));
  connect(m_checkButton, &QPushButton::clicked, this,
          [this]() { checkForUpdates(/*force=*/true); });
  checkRow->addWidget(m_checkButton);
  m_checkLabel = new QLabel(listGroup);
  m_checkLabel->setWordWrap(true);
  m_checkLabel->setEnabled(false);
  checkRow->addWidget(m_checkLabel, 1);
  listLayout->addLayout(checkRow);

  m_table = new QTableWidget(listGroup);
  m_table->setWordWrap(false);
  m_table->setColumnCount(ColCount);
  m_table->setHorizontalHeaderLabels({
    tr("Status"), tr("Name"), tr("Type"), tr("Version"), tr("Latest"),
    QString(), QString(), QString()
  });
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->verticalHeader()->setVisible(false);
  m_table->setTabKeyNavigation(false);
  m_table->installEventFilter(this);
  connect(m_table, &QTableWidget::itemDoubleClicked, this,
          [this](QTableWidgetItem* item) {
    if (item) showRowDetails(item->row());
  });
  listLayout->addWidget(m_table, 1);

  // 更新確認 / ダウンロードの進行表示 (進行中だけ出す)。
  auto* busyRow = new QHBoxLayout();
  m_busyLabel = new QLabel(listGroup);
  busyRow->addWidget(m_busyLabel);
  m_progressBar = new QProgressBar(listGroup);
  m_progressBar->setTextVisible(false);
  busyRow->addWidget(m_progressBar, 1);
  listLayout->addLayout(busyRow);
  m_busyLabel->hide();
  m_progressBar->hide();

  // ドロップ領域 (点線枠)。ドラッグ＆ドロップでも導入できることが見て分かるようにする。
  // ドロップ自体はページのどこでも受けるが、ドラッグ中はこの枠を強調して知らせる。
  auto* dropZone = new DropZoneFrame(listGroup);
  m_dropZone = dropZone;
  auto* dropLayout = new QVBoxLayout(dropZone);
  dropLayout->setContentsMargins(12, 10, 12, 10);
  dropLayout->setSpacing(4);
  auto* dropLabel = new QLabel(
    tr("⬇  Drop plugin files (.%1) here to install")
      .arg(PluginInstaller::nativeLibrarySuffix()), dropZone);
  dropLabel->setAlignment(Qt::AlignCenter);
  dropLayout->addWidget(dropLabel);
  auto* orLabel = new QLabel(tr("or"), dropZone);
  orLabel->setAlignment(Qt::AlignCenter);
  orLabel->setEnabled(false);
  dropLayout->addWidget(orLabel);
  m_installButton = new QPushButton(tr("Install from File..."), dropZone);
  m_installButton->setAutoDefault(false);
  m_installButton->setToolTip(
    tr("Choose plugin files (.%1) to install. The plugin type (viewer / archive) "
       "is detected automatically.").arg(PluginInstaller::nativeLibrarySuffix()));
  connect(m_installButton, &QPushButton::clicked, this, &PluginsTab::chooseFiles);
  m_installButton->installEventFilter(this);
  dropLayout->addWidget(m_installButton, 0, Qt::AlignHCenter);
  listLayout->addWidget(dropZone);

  // 再起動待ちの変更があるときだけ出すバナー。
  m_restartBanner = new QFrame(listGroup);
  m_restartBanner->setFrameShape(QFrame::StyledPanel);
  auto* bannerLayout = new QHBoxLayout(m_restartBanner);
  auto* bannerLabel = new QLabel(
    tr("⏳ Plugin changes are applied when farman is restarted."), m_restartBanner);
  bannerLabel->setWordWrap(true);
  bannerLayout->addWidget(bannerLabel, 1);
  m_restartButton = new QPushButton(tr("Restart farman"), m_restartBanner);
  m_restartButton->setAutoDefault(false);
  m_restartButton->setToolTip(
    tr("Save the settings in this dialog and restart farman now."));
  connect(m_restartButton, &QPushButton::clicked, this,
          &PluginsTab::restartRequested);
  bannerLayout->addWidget(m_restartButton);
  listLayout->addWidget(m_restartBanner);
  m_enterClickFilter->installOnButtonsIn(m_checkButton);
  m_enterClickFilter->installOnButtonsIn(m_installButton);
  m_enterClickFilter->installOnButtonsIn(m_restartButton);

  mainLayout->addWidget(listGroup, 1);

  // ── プラグインディレクトリ (ビュアー / アーカイブ共通の置き場所) ──
  // めったに変えない設定なので、一覧と導入の下に置く。
  QGroupBox* pluginGroup = new QGroupBox(tr("Plugins Directory"), this);
  QVBoxLayout* pluginLayout = new QVBoxLayout(pluginGroup);

  QLabel* pluginDirHint = new QLabel(
    tr("External plugins are loaded on startup from this directory "
       "(viewers/ and archives/ subdirectories). Leave empty to use the "
       "default user plugins directory."), pluginGroup);
  pluginDirHint->setWordWrap(true);
  pluginLayout->addWidget(pluginDirHint);

  QWidget* pluginDirRow = new QWidget(pluginGroup);
  QHBoxLayout* pluginDirRowLayout = new QHBoxLayout(pluginDirRow);
  pluginDirRowLayout->setContentsMargins(0, 0, 0, 0);

  m_pluginsDirectoryEdit = new QLineEdit(pluginGroup);
  m_pluginsDirectoryEdit->setPlaceholderText(Settings::defaultPluginsDirectory());
  m_pluginsDirectoryEdit->setToolTip(
    tr("Directory containing external plugins (.dylib, .so, .dll). "
       "Changes take effect on next launch."));
  m_pluginsDirectoryBrowse = new QToolButton(pluginGroup);
  m_pluginsDirectoryBrowse->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
  m_pluginsDirectoryBrowse->setToolTip(tr("Choose plugins directory..."));
  m_pluginsDirectoryOpen = new QToolButton(pluginGroup);
  m_pluginsDirectoryOpen->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
  m_pluginsDirectoryOpen->setText(tr("Open"));
  m_pluginsDirectoryOpen->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  m_pluginsDirectoryOpen->setToolTip(
    tr("Open the plugins directory in Finder / Explorer."));
  m_pluginsDirectoryDefault = new QToolButton(pluginGroup);
  m_pluginsDirectoryDefault->setText(tr("Default"));
  m_pluginsDirectoryDefault->setToolTip(
    tr("Use the default user plugins directory."));

  pluginDirRowLayout->addWidget(new QLabel(tr("Directory:"), pluginGroup));
  pluginDirRowLayout->addWidget(m_pluginsDirectoryEdit, 1);
  pluginDirRowLayout->addWidget(m_pluginsDirectoryBrowse);
  pluginDirRowLayout->addWidget(m_pluginsDirectoryOpen);
  pluginDirRowLayout->addWidget(m_pluginsDirectoryDefault);
  pluginLayout->addWidget(pluginDirRow);

  // プラグインディレクトリを Finder / エクスプローラーで開く。空欄なら既定
  // ディレクトリを開く。dll/dylib/so を手で置きに行くとき用。無ければ作る。
  connect(m_pluginsDirectoryOpen, &QToolButton::clicked, this, [this]() {
    QString dir = m_pluginsDirectoryEdit->text().trimmed();
    if (dir.isEmpty()) dir = Settings::defaultPluginsDirectory();
    QDir().mkpath(dir);
    QDir().mkpath(dir + QStringLiteral("/viewers"));
    QDir().mkpath(dir + QStringLiteral("/archives"));
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
  });
  connect(m_pluginsDirectoryBrowse, &QToolButton::clicked, this, [this]() {
    const QString start = m_pluginsDirectoryEdit->text().isEmpty()
                          ? Settings::defaultPluginsDirectory()
                          : m_pluginsDirectoryEdit->text();
    // 開始ディレクトリが無いとダイアログが別の場所 (作業ディレクトリ等) に
    // フォールバックし、ユーザーが置き場所を誤解する。無ければ作っておく。
    if (!QDir(start).exists()) QDir().mkpath(start);
    const QString selected = QFileDialog::getExistingDirectory(
      this, tr("Choose plugins directory"), start,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!selected.isEmpty()) {
      m_pluginsDirectoryEdit->setText(selected);
    }
  });
  connect(m_pluginsDirectoryDefault, &QToolButton::clicked,
          m_pluginsDirectoryEdit, &QLineEdit::clear);

  mainLayout->addWidget(pluginGroup);
}

void PluginsTab::loadSettings() {
  m_allowExternalPluginsCheck->setChecked(Settings::instance().allowExternalPlugins());
  m_pluginsDirectoryEdit->setText(Settings::instance().pluginsDirectory());
  // 同梱のマニフェストだけ読んだ状態 (ネットワークには出ない)。どれが公式かは分かる。
  m_catalog = PluginCatalog::instance().entries();
  reloadList();
}

void PluginsTab::save() {
  Settings& settings = Settings::instance();

  // プラグインの読込み設定 (どちらも次回起動から反映)。
  m_pluginLoadSettingsChangedOnSave = false;
  const bool allowExternal = m_allowExternalPluginsCheck->isChecked();
  if (allowExternal != settings.allowExternalPlugins()) {
    settings.setAllowExternalPlugins(allowExternal);
    m_pluginLoadSettingsChangedOnSave = true;
  }
  const QString newPluginsDirectory = m_pluginsDirectoryEdit->text().trimmed();
  if (newPluginsDirectory != settings.pluginsDirectory()) {
    settings.setPluginsDirectory(newPluginsDirectory);
    m_pluginLoadSettingsChangedOnSave = true;
  }
}

QList<PluginsTab::Row> PluginsTab::collectRows() const {
  const QString root = PluginInstaller::pluginsRoot();
  const PluginInstaller::PendingState pending = PluginInstaller::pendingState(root);
  QList<Row> rows;
  QStringList knownRelPaths;

  // 導入済み (ロードを試みた) 外部プラグイン。同梱プラグインは管理対象外なので
  // 出さない (ビュアー / アーカイブの各ページにある)。
  const auto addRecord = [&](PluginInstaller::Kind kind, Row row) {
    row.kind      = kind;
    row.relPath   = PluginInstaller::managedRelativePath(root, row.filePath);
    row.installed = true;
    if (row.name.isEmpty()) {
      row.name = QFileInfo(row.filePath).fileName();
    }
    if (!row.relPath.isEmpty()) {
      knownRelPaths.append(row.relPath);
      if (pending.replacedBy.contains(row.relPath)) {
        // 別名の新しい版に置き換わる (古いファイルの削除は更新の一部)。導入待ちの
        // ファイルは新規の行としては出さず、この行の「更新待ち」として見せる。
        row.pendingInstall        = true;
        row.pendingInstallRelPath = pending.replacedBy.value(row.relPath);
        knownRelPaths.append(row.pendingInstallRelPath);
      } else if (pending.installs.contains(row.relPath)) {
        row.pendingInstall        = true;
        row.pendingInstallRelPath = row.relPath;
      } else {
        row.pendingRemoval = pending.removals.contains(row.relPath);
      }
    }
    rows.append(row);
  };

  for (const PluginRecord& rec : ViewerDispatcher::instance().pluginRecords()) {
    if (rec.origin != PluginRecord::Origin::External) continue;
    Row row;
    row.filePath = rec.filePath;   row.pluginId  = rec.pluginId;
    row.name     = rec.pluginName; row.version   = rec.version;
    row.author   = rec.author;     row.authorUrl = rec.authorUrl;
    row.errorReason = rec.errorReason;
    row.loaded = rec.loaded;       row.disabledByUser = rec.disabledByUser;
    row.blockedExternalDisabled = rec.blockedExternalDisabled;
    addRecord(PluginInstaller::Kind::Viewer, row);
  }
  for (const ArchivePluginRecord& rec : ArchiveDispatcher::instance().pluginRecords()) {
    if (rec.origin != ArchivePluginRecord::Origin::External) continue;
    Row row;
    row.filePath = rec.filePath;   row.pluginId  = rec.pluginId;
    row.name     = rec.pluginName; row.version   = rec.version;
    row.author   = rec.author;     row.authorUrl = rec.authorUrl;
    row.errorReason = rec.errorReason;
    row.loaded = rec.loaded;       row.disabledByUser = rec.disabledByUser;
    row.blockedExternalDisabled = rec.blockedExternalDisabled;
    addRecord(PluginInstaller::Kind::Archive, row);
  }

  // 導入待ちの新規ファイル (まだロードされていないので名前はファイル名だけ)。
  for (const QString& relPath : pending.installs) {
    if (knownRelPaths.contains(relPath)) continue;
    Row row;
    row.kind           = kindOfRelPath(relPath);
    row.relPath        = relPath;
    row.name           = relPath.section(QLatin1Char('/'), 1);
    row.pendingInstall = true;
    row.pendingInstallRelPath = relPath;
    rows.append(row);
  }

  applyCatalog(&rows);
  return rows;
}

void PluginsTab::applyCatalog(QList<Row>* rows) const {
  const QString lang = uiLanguage();
  const QString host = PluginInstaller::hostVersion();

  for (int ci = 0; ci < m_catalog.size(); ++ci) {
    const PluginCatalogEntry& entry = m_catalog[ci];
    const QString entryBase = PluginInstaller::pluginBaseName(entry.fileName);

    // 公式プラグインの行を探す: ロード済みなら pluginId、そうでなければファイルの基底名。
    bool matched = false;
    for (Row& row : *rows) {
      if (row.kind != entry.kind) continue;
      const QString fileName = !row.filePath.isEmpty()
        ? QFileInfo(row.filePath).fileName() : row.relPath.section(QLatin1Char('/'), 1);
      const bool same = (!row.pluginId.isEmpty() && row.pluginId == entry.id)
                     || PluginInstaller::pluginBaseName(fileName) == entryBase;
      if (!same) continue;
      matched = true;
      row.catalogIndex = ci;
      if (!row.installed) {
        row.name = entry.name(lang);   // 導入待ちの新規ファイルは、ファイル名より表示名で
      }
    }
    if (!matched) {
      Row row;   // 未導入の公式プラグイン
      row.kind         = entry.kind;
      row.name         = entry.name(lang);
      row.catalogIndex = ci;
      rows->append(row);
    }
  }

  // 「更新する」/「インストール」の可否。
  for (Row& row : *rows) {
    if (row.catalogIndex < 0) continue;
    const PluginCatalogEntry& entry = m_catalog[row.catalogIndex];
    row.updateState = UpdateState::Unavailable;
    if (row.pendingInstall || row.pendingRemoval) {
      row.updateNote = tr("A change to this plugin is waiting for a restart.");
      continue;
    }
    switch (entry.releaseState) {
      case PluginCatalogEntry::ReleaseState::Unknown:
        row.updateState = UpdateState::Unknown;
        row.updateNote  = tr("The latest version has not been checked yet.");
        continue;
      case PluginCatalogEntry::ReleaseState::Failed:
        row.updateNote = tr("Could not get release information (%1).")
                           .arg(entry.releaseError);
        continue;
      case PluginCatalogEntry::ReleaseState::NoAssetForPlatform:
        row.updateNote = tr("Not available for this platform.");
        continue;
      case PluginCatalogEntry::ReleaseState::Ok:
        break;
    }
    if (!hostSatisfiesMinVersion(host, entry.minFarmanVersion)) {
      row.updateNote = tr("Requires farman %1 or later.").arg(entry.minFarmanVersion);
    } else if (!row.installed) {
      row.updateState = UpdateState::NotInstalled;
    } else if (row.relPath.isEmpty()) {
      // ロードされているのにプラグインディレクトリに無い。ここから入れると同じ ID が
      // 2 つになるので、更新はさせない。
      row.updateNote = tr("Installed outside the plugins directory.");
    } else if (row.version.isEmpty()) {
      row.updateState = UpdateState::VersionUnknown;
      row.updateNote  = tr("The installed version is unknown because the plugin is "
                           "not loaded. Updating installs the latest version.");
    } else if (UpdateChecker::compareVersions(row.version, entry.latestVersion) < 0) {
      row.updateState = UpdateState::UpdateAvailable;
    } else {
      row.updateState = UpdateState::UpToDate;
    }
  }
}

QString PluginsTab::statusEmoji(const Row& row) const {
  if (row.pendingInstall || row.pendingRemoval) return QStringLiteral("⏳");
  if (!row.installed) return QStringLiteral("-");
  if (row.loaded) return QStringLiteral("✅");
  if (row.blockedExternalDisabled) return QStringLiteral("🔒");
  return row.disabledByUser ? QStringLiteral("🚫") : QStringLiteral("❌");
}

QString PluginsTab::statusText(const Row& row) const {
  if (row.pendingRemoval) return tr("Uninstalled after restart");
  if (row.pendingInstall) {
    if (!row.installed) return tr("Installed after restart");
    const QString newName = row.pendingInstallRelPath.section(QLatin1Char('/'), 1);
    return row.pendingInstallRelPath == row.relPath
      ? tr("Updated after restart")
      : tr("Updated after restart (replaced by %1)").arg(newName);
  }
  if (!row.installed) return tr("Not installed");
  if (row.loaded) return tr("Loaded");
  if (row.blockedExternalDisabled) return tr("Blocked (external plugins off)");
  if (row.disabledByUser) return tr("Disabled");
  return row.errorReason.isEmpty() ? tr("Failed")
                                   : tr("Failed: %1").arg(row.errorReason);
}

QString PluginsTab::updateStateText(const Row& row) const {
  switch (row.updateState) {
    case UpdateState::NotOfficial:     return tr("Not an official plugin");
    case UpdateState::NotInstalled:    return tr("Not installed");
    case UpdateState::UpdateAvailable: return tr("Update available");
    case UpdateState::UpToDate:        return tr("Up to date");
    case UpdateState::VersionUnknown:
    case UpdateState::Unknown:
    case UpdateState::Unavailable:     break;
  }
  return row.updateNote;
}

QPushButton* PluginsTab::rowButton(int row, int column) const {
  if (row < 0 || row >= m_table->rowCount()) return nullptr;
  return qobject_cast<QPushButton*>(m_table->cellWidget(row, column));
}

void PluginsTab::reloadList() {
  const int previousRow = m_table->currentRow();
  m_rows = collectRows();
  m_table->setRowCount(m_rows.size());

  const auto setItem = [this](int row, int col, const QString& text,
                              const QString& toolTip = QString()) {
    auto* item = new QTableWidgetItem(text);
    item->setToolTip(toolTip.isEmpty() ? text : toolTip);
    m_table->setItem(row, col, item);
    return item;
  };
  const auto addButton = [this](int row, int col, const QString& text,
                                const QString& toolTip, bool enabled) {
    auto* button = new QPushButton(text, m_table);
    button->setAutoDefault(false);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setToolTip(toolTip);
    button->setEnabled(enabled);
    button->setProperty(kRowProperty, row);
    button->setProperty(kColumnProperty, col);
    button->installEventFilter(this);             // Tab / フォーカス時の行選択
    m_enterClickFilter->installOnButtonsIn(button);
    m_table->setCellWidget(row, col, button);
    return button;
  };

  bool anyPending = false;
  for (int i = 0; i < m_rows.size(); ++i) {
    const Row& row = m_rows[i];
    const bool pending = row.pendingInstall || row.pendingRemoval;
    anyPending = anyPending || pending;
    const bool official = row.catalogIndex >= 0;

    auto* statusItem = setItem(i, ColStatus, statusEmoji(row), statusText(row));
    statusItem->setTextAlignment(Qt::AlignCenter);

    auto* nameItem = setItem(i, ColName, row.name,
                             row.filePath.isEmpty() ? row.relPath : row.filePath);
    if (row.installed && !pending && !row.loaded && !row.disabledByUser
        && !row.blockedExternalDisabled) {
      nameItem->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
      nameItem->setToolTip(statusText(row));
    }
    setItem(i, ColKind, kindLabel(row.kind));
    setItem(i, ColVersion, row.version.isEmpty() ? QStringLiteral("-") : row.version);
    const QString latest = official ? m_catalog[row.catalogIndex].latestVersion : QString();
    setItem(i, ColLatest, latest.isEmpty() ? QStringLiteral("-") : latest,
            official ? updateStateText(row) : tr("Not an official plugin"));

    // ── 詳細 ──
    auto* details = addButton(i, ColDetails, tr("Details..."),
                              tr("Show all information about this plugin."), true);
    connect(details, &QPushButton::clicked, this, [this, i]() {
      m_table->selectRow(i);
      showRowDetails(i);
    });

    // ── 更新する / インストール (公式プラグインだけ) ──
    m_table->removeCellWidget(i, ColUpdate);
    if (official) {
      const bool canUpdate = row.updateState == UpdateState::NotInstalled
                          || row.updateState == UpdateState::UpdateAvailable
                          || row.updateState == UpdateState::VersionUnknown;
      const QString label = row.installed || row.pendingInstall ? tr("Update")
                                                                : tr("Install");
      auto* update = addButton(i, ColUpdate, label, updateStateText(row),
                               canUpdate && !m_busy);
      update->setProperty("canUpdate", canUpdate);
      connect(update, &QPushButton::clicked, this, [this, i]() {
        m_table->selectRow(i);
        runRowUpdate(i);
      });
    }

    // ── アンインストール / 取り消し ──
    // プラグインディレクトリの外にある外部プラグイン (同梱ディレクトリに置かれた
    // 第三者製など) は farman からは消せない。未導入の公式プラグインにはボタンを出さない。
    m_table->removeCellWidget(i, ColUninstall);
    if (row.installed || pending) {
      QString toolTip;
      if (row.relPath.isEmpty()) {
        toolTip = tr("This plugin is outside the plugins directory, so farman cannot "
                     "uninstall it.");
      } else if (row.pendingRemoval) {
        toolTip = tr("Keep this plugin installed.");
      } else if (row.pendingInstall) {
        toolTip = row.installed ? tr("Do not update this plugin.")
                                : tr("Do not install this plugin.");
      } else {
        toolTip = tr("Delete this plugin when farman is restarted.");
      }
      auto* uninstall = addButton(i, ColUninstall,
                                  pending ? tr("Cancel") : tr("Uninstall..."),
                                  toolTip, !row.relPath.isEmpty());
      connect(uninstall, &QPushButton::clicked, this, [this, i]() {
        m_table->selectRow(i);
        runRowUninstall(i);
      });
    }
  }

  // 行のボタンは一覧を作り直すたびに生成されるので、そのままだとフォーカスチェーンの
  // 末尾 (OK / キャンセルの後ろ) に入ってしまう。チェーン上は 一覧 → 各行のボタン →
  // 「ファイルからインストール...」に並べておく。実際の Tab は eventFilter が
  // 「選択行のボタンだけ」を辿らせる。
  QWidget::setTabOrder(m_checkButton, m_table);
  QWidget* previous = m_table;
  for (int i = 0; i < m_rows.size(); ++i) {
    for (const int col : kButtonColumns) {
      if (QPushButton* button = rowButton(i, col)) {
        QWidget::setTabOrder(previous, button);
        previous = button;
      }
    }
  }
  QWidget::setTabOrder(previous, m_installButton);

  m_table->resizeColumnsToContents();
  m_table->resizeRowsToContents();
  m_table->horizontalHeader()->setStretchLastSection(false);
  m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
  // ボタンの列は、その列でいちばん幅の広いボタンに合わせる (ボタンの無い列は詰める)。
  for (const int col : kButtonColumns) {
    int width = 0;
    for (int i = 0; i < m_rows.size(); ++i) {
      if (const QPushButton* button = rowButton(i, col)) {
        width = qMax(width, button->sizeHint().width() + 8);
      }
    }
    m_table->setColumnWidth(col, width);
  }
  if (m_table->rowCount() > 0) {
    m_table->selectRow(qBound(0, previousRow, m_table->rowCount() - 1));
  }

  // 削除待ちは一覧の行から分かるが、退避だけが残っている場合 (例: 既に手で消された
  // ファイルの削除記録) も再起動で片付くので、退避が 1 件でもあればバナーを出す。
  const PluginInstaller::PendingState state =
    PluginInstaller::pendingState(PluginInstaller::pluginsRoot());
  m_restartBanner->setVisible(anyPending || !state.installs.isEmpty()
                              || !state.removals.isEmpty());
}

void PluginsTab::reloadListKeepingFocus(int row, int column) {
  // 押されたボタン自身が一覧の作り直しで破棄されるので、シグナル処理を抜けてから行う。
  // ボタンにフォーカスがあった場合 (Tab で辿って押した) は、作り直した同じ位置のボタンへ
  // フォーカスを戻す。続けて「取り消し」などを押せるようにするため。
  const QWidget* focused = QApplication::focusWidget();
  const bool buttonHadFocus = focused && focused != m_table
                           && m_table->isAncestorOf(focused);
  QTimer::singleShot(0, this, [this, row, column, buttonHadFocus]() {
    reloadList();
    if (!buttonHadFocus) return;
    QPushButton* button = rowButton(row, column);
    if (button && button->isEnabled()) {
      button->setFocus(Qt::OtherFocusReason);
    } else {
      m_table->setFocus(Qt::OtherFocusReason);
    }
  });
}

void PluginsTab::showRowDetails(int index) {
  if (index < 0 || index >= m_rows.size()) return;
  const Row& row = m_rows[index];
  const bool official = row.catalogIndex >= 0;

  QDialog dialog(this);
  dialog.setWindowTitle(tr("Plugin Details"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  const auto addField = [&dialog, form](const QString& label, const QString& value) {
    auto* valueLabel = new QLabel(value.isEmpty() ? QStringLiteral("-") : value, &dialog);
    valueLabel->setWordWrap(true);
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(label, valueLabel);
  };
  const auto addLink = [&dialog, form](const QString& label, const QString& url,
                                       const QString& text) {
    auto* link = new QLabel(
      QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), text.toHtmlEscaped()),
      &dialog);
    link->setTextInteractionFlags(Qt::TextBrowserInteraction);
    link->setOpenExternalLinks(true);
    link->setWordWrap(true);
    form->addRow(label, link);
  };

  addField(tr("Name:"), row.name);
  addField(tr("Type:"), kindLabel(row.kind));
  addField(tr("Source:"), official ? tr("Official (farman project)") : tr("Unofficial"));
  addField(tr("Status:"), statusEmoji(row) + QLatin1Char(' ') + statusText(row));
  addField(tr("Version:"), row.version);
  if (official) {
    const PluginCatalogEntry& entry = m_catalog[row.catalogIndex];
    addField(tr("Latest version:"), entry.latestVersion);
    addField(tr("Update:"), updateStateText(row));
    addField(tr("Description:"), entry.description(uiLanguage()));
    if (!entry.minFarmanVersion.isEmpty()) {
      addField(tr("Requires:"), tr("farman %1 or later").arg(entry.minFarmanVersion));
    }
    const QString url = entry.releaseUrl.isEmpty()
      ? QStringLiteral("https://github.com/%1").arg(entry.repo) : entry.releaseUrl;
    addLink(entry.releaseUrl.isEmpty() ? tr("Repository:") : tr("Release notes:"), url, url);
  }
  addField(tr("Author:"), row.author);
  if (!row.authorUrl.isEmpty()) {
    addLink(tr("Author URL:"), row.authorUrl, row.authorUrl);
  }
  addField(tr("Plugin ID:"), row.pluginId);
  addField(tr("Path:"), row.filePath.isEmpty() ? row.relPath : row.filePath);
  if (row.installed && !row.loaded && !row.errorReason.isEmpty() && !row.disabledByUser) {
    addField(tr("Error:"), row.errorReason);
  }
  if (row.installed) {
    auto* hint = new QLabel(
      row.kind == PluginInstaller::Kind::Viewer
        ? tr("To turn this plugin on or off or change its settings, use the Viewer page.")
        : tr("To turn this plugin on or off or change its settings, use the Archive page."),
      &dialog);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    form->addRow(QString(), hint);
  }
  layout->addLayout(form);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  layout->addWidget(buttons);
  // パスが長いと初期幅が画面いっぱいまで伸びるので適度に抑える。
  dialog.resize(std::clamp(dialog.sizeHint().width(), 440, 600), dialog.sizeHint().height());
  dialog.exec();
}

void PluginsTab::runRowUninstall(int index) {
  if (index < 0 || index >= m_rows.size()) return;
  const Row row = m_rows[index];
  if (row.relPath.isEmpty()) return;
  const QString root = PluginInstaller::pluginsRoot();

  if (row.pendingRemoval) {
    PluginInstaller::cancelPendingRemoval(root, row.relPath);
  } else if (row.pendingInstall) {
    // 別名の新しい版への更新なら、古い版の削除待ちも一緒に取り消される。
    PluginInstaller::cancelPendingInstall(root, row.pendingInstallRelPath);
  } else {
    const bool ok = confirm(
      this, tr("Uninstall Plugin"),
      tr("Uninstall \"%1\"?\n\nThe plugin file is deleted when farman is "
         "restarted. Until then you can cancel this from the list.").arg(row.name),
      /*defaultYes=*/false);
    if (!ok) return;
    QString error;
    if (!PluginInstaller::stageRemoval(root, row.filePath, &error)) {
      warn(this, tr("Uninstall Plugin"), error);
      return;
    }
  }
  reloadListKeepingFocus(index, ColUninstall);
}

void PluginsTab::runRowUpdate(int index) {
  if (m_busy || index < 0 || index >= m_rows.size()) return;
  const Row& row = m_rows[index];
  if (row.catalogIndex < 0) return;
  const PluginCatalogEntry& entry = m_catalog[row.catalogIndex];
  const bool canUpdate = row.updateState == UpdateState::NotInstalled
                      || row.updateState == UpdateState::UpdateAvailable
                      || row.updateState == UpdateState::VersionUnknown;
  if (!canUpdate) return;

  // 確認は 1 回。導入済みの別名ファイルがあれば、アンインストール (置き換え) されることを
  // ここで伝える。配布物は SHA256 で照合するので、入手元の確認は求めない。
  QString question = row.installed
    ? tr("Update \"%1\" to %2?").arg(row.name, entry.latestVersion)
    : tr("Install \"%1\" %2?").arg(row.name, entry.latestVersion);
  QStringList replaced;
  const QString probeName =
    entry.fileName + QLatin1Char('.') + PluginInstaller::nativeLibrarySuffix();
  const QString root = PluginInstaller::pluginsRoot();
  for (const QString& relPath : PluginInstaller::samePluginFiles(root, entry.kind, probeName)) {
    const QString name = relPath.section(QLatin1Char('/'), 1);
    if (name != entry.asset.name && QFileInfo::exists(root + QLatin1Char('/') + relPath)
        && !replaced.contains(name)) {
      replaced.append(name);
    }
  }
  if (!replaced.isEmpty()) {
    question += QStringLiteral("\n\n")
              + tr("The installed file will be uninstalled: %1")
                  .arg(replaced.join(QStringLiteral(", ")));
  }
  question += QStringLiteral("\n\n")
            + tr("The file is downloaded from GitHub and verified with its SHA256 "
                 "checksum.");
  if (!confirm(this, tr("Install Plugins"), question, /*defaultYes=*/true)) return;

  m_downloadingEntry = entry;
  setBusy(true, tr("Downloading %1...").arg(entry.asset.name));
  m_downloader->start(entry);
}

void PluginsTab::onDownloadFinished(bool ok, const QString& filePath,
                                    const QString& errorReason) {
  setBusy(false);
  if (!ok) {
    warn(this, tr("Install Plugins"), errorReason);
    reloadList();
    return;
  }

  // ダウンロードした配布物も、ファイルからの導入と同じ検証 (IID / MinHostVersion) を通す。
  const PluginInstaller::Inspection inspection =
    PluginInstaller::inspect(filePath, PluginInstaller::hostVersion());
  QString error = inspection.error;
  bool staged = false;
  if (inspection.ok && inspection.kind != m_downloadingEntry.kind) {
    error = tr("The downloaded file is not the expected type of plugin.");
  } else if (inspection.ok) {
    staged = PluginInstaller::stageInstall(PluginInstaller::pluginsRoot(), filePath,
                                           inspection.kind, &error);
  }
  QFile::remove(filePath);  // 退避にコピー済み (または失敗) なので、キャッシュ側は消す

  reloadList();
  if (staged) {
    offerEnableExternalPlugins();
  } else {
    warn(this, tr("Install Plugins"), error);
  }
}

void PluginsTab::checkForUpdates(bool force) {
  if (m_busy) return;
  setBusy(true, tr("Checking the official plugins for updates..."));
  PluginCatalog::instance().refresh(force);
}

void PluginsTab::onCatalogUpdated() {
  if (!m_downloader->isRunning()) {
    setBusy(false);
  }
  m_catalog = PluginCatalog::instance().entries();
  reloadList();

  const bool anyFailed = std::any_of(m_catalog.cbegin(), m_catalog.cend(),
    [](const PluginCatalogEntry& e) {
      return e.releaseState == PluginCatalogEntry::ReleaseState::Failed;
    });
  const QDateTime fetchedAt = PluginCatalog::instance().fetchedAt();
  if (anyFailed) {
    m_checkLabel->setText(
      tr("Some release information could not be retrieved. Try again later."));
  } else if (!PluginCatalog::instance().manifestFromNetwork()) {
    m_checkLabel->setText(
      tr("The online list of official plugins could not be reached, so the list "
         "bundled with this farman is used."));
  } else if (fetchedAt.isValid()) {
    m_checkLabel->setText(
      tr("Last checked: %1")
        .arg(QLocale().toString(fetchedAt.toLocalTime(), QLocale::ShortFormat)));
  } else {
    m_checkLabel->clear();
  }
}

void PluginsTab::setBusy(bool busy, const QString& message) {
  m_busy = busy;
  m_busyLabel->setText(message);
  m_busyLabel->setVisible(busy);
  m_progressBar->setVisible(busy);
  if (busy) {
    m_progressBar->setRange(0, 0);  // 不確定表示。ダウンロード中は progress で上書き
  }
  m_checkButton->setEnabled(!busy);
  for (int i = 0; i < m_table->rowCount(); ++i) {
    if (QPushButton* update = rowButton(i, ColUpdate)) {
      update->setEnabled(!busy && update->property("canUpdate").toBool());
    }
  }
}

void PluginsTab::offerEnableExternalPlugins() {
  // 外部プラグインの読込みが OFF のままだと導入しても動かない。黙って ON には
  // せず、ここで尋ねる。断っても導入は続ける (一覧には「ブロック中」で出る)。
  // ON にするのはこのページのチェックで、保存は OK / 適用 / 再起動のとき。
  if (m_allowExternalPluginsCheck->isChecked()) return;
  const bool enable = confirm(
    this, tr("Install Plugins"),
    tr("Loading external plugins is currently turned off, so the installed "
       "plugins will not be loaded.\n\nTurn on \"Allow loading external "
       "plugins\"?"),
    /*defaultYes=*/true);
  if (enable) {
    m_allowExternalPluginsCheck->setChecked(true);
  }
}

void PluginsTab::chooseFiles() {
  const QString suffix = PluginInstaller::nativeLibrarySuffix();
  const QStringList files = QFileDialog::getOpenFileNames(
    this, tr("Install Plugins"), QString(),
    tr("farman plugins (*.%1)").arg(suffix));
  if (!files.isEmpty()) {
    installFiles(files);
  }
}

void PluginsTab::installFiles(const QStringList& filePaths) {
  struct Candidate {
    QString               filePath;
    PluginInstaller::Kind kind;
    bool                  overwrites = false;  // 同名のファイルが導入済み / 導入待ち
    // 導入済みの、別名の同じプラグイン (= 別のバージョンと見られるもの) のファイル名。
    // 導入するならアンインストールが要るので、個別に尋ねる。
    QStringList           otherVersions;
  };
  const QString root = PluginInstaller::pluginsRoot();
  QList<Candidate> candidates;
  QStringList failures;  // "Foo.dylib: 理由"

  for (const QString& filePath : filePaths) {
    const QString name = QFileInfo(filePath).fileName();
    const PluginInstaller::Inspection inspection =
      PluginInstaller::inspect(filePath, PluginInstaller::hostVersion());
    if (!inspection.ok) {
      failures.append(QStringLiteral("%1: %2").arg(name, inspection.error));
      continue;
    }
    Candidate candidate{filePath, inspection.kind};
    for (const QString& relPath :
         PluginInstaller::samePluginFiles(root, inspection.kind, name)) {
      const QString oldName = relPath.section(QLatin1Char('/'), 1);
      if (oldName == name) {
        candidate.overwrites = true;
      } else if (QFileInfo::exists(root + QLatin1Char('/') + relPath)) {
        candidate.otherVersions.append(oldName);
      }
      // 別名の導入待ち (まだ導入されていない版) は、尋ねずに後の導入で置き換える。
    }
    candidates.append(candidate);
  }

  if (candidates.isEmpty()) {
    warn(this, tr("Install Plugins"),
         tr("No plugin was installed.") + QStringLiteral("\n\n")
           + failures.join(QLatin1Char('\n')));
    return;
  }

  // 導入の確認。ロードしないと制作者を読めず、ハッシュの照合先も無いので、
  // 入手元を信頼できるかはユーザーに判断してもらう。
  QStringList lines;
  for (const Candidate& c : candidates) {
    QString line = QStringLiteral("• %1 (%2)")
                     .arg(QFileInfo(c.filePath).fileName(), kindLabel(c.kind));
    if (c.overwrites) {
      line += QStringLiteral(" — ") + tr("replaces the installed file");
    }
    lines.append(line);
  }
  const QString question =
    tr("Install the following plugins?") + QStringLiteral("\n\n")
    + lines.join(QLatin1Char('\n')) + QStringLiteral("\n\n")
    + tr("Plugins run as part of farman and can access your files. farman cannot "
         "verify who made these files, so install only plugins obtained from a "
         "source you trust.");
  if (!confirm(this, tr("Install Plugins"), question, /*defaultYes=*/false)) {
    return;
  }

  // 別のバージョンと見られるプラグインが導入済みなら、そちらをアンインストールして
  // よいかを個別に尋ねる。同じプラグインを 2 つ置くと片方しか読み込まれない (重複 ID)
  // ので、断られたらそのファイルは導入しない。
  QList<Candidate> accepted;
  for (const Candidate& c : std::as_const(candidates)) {
    if (c.otherVersions.isEmpty()) {
      accepted.append(c);
      continue;
    }
    const QString newName = QFileInfo(c.filePath).fileName();
    QStringList oldLines;
    for (const QString& oldName : c.otherVersions) {
      // ロード済みなら版数が分かるので添える。
      QString version;
      for (const Row& row : std::as_const(m_rows)) {
        if (row.relPath.section(QLatin1Char('/'), 1) == oldName && row.kind == c.kind) {
          version = row.version;
        }
      }
      oldLines.append(version.isEmpty()
        ? QStringLiteral("• %1").arg(oldName)
        : QStringLiteral("• %1 (%2)").arg(oldName, tr("version %1").arg(version)));
    }
    const bool replace = confirm(
      this, tr("Install Plugins"),
      tr("What appears to be another version of \"%1\" is already installed:")
          .arg(newName)
        + QStringLiteral("\n\n") + oldLines.join(QLatin1Char('\n')) + QStringLiteral("\n\n")
        + tr("Uninstall the installed one and install \"%1\"?\n\nIf you choose No, "
             "\"%1\" is not installed (only one copy of a plugin can be loaded).")
            .arg(newName),
      /*defaultYes=*/true);
    if (replace) {
      accepted.append(c);
    }
  }
  candidates = accepted;
  if (candidates.isEmpty()) {
    return;  // すべて断られた。何も変えていないので結果表示も不要
  }

  offerEnableExternalPlugins();

  int staged = 0;
  for (const Candidate& c : candidates) {
    QString error;
    if (PluginInstaller::stageInstall(root, c.filePath, c.kind, &error)) {
      ++staged;
    } else {
      failures.append(
        QStringLiteral("%1: %2").arg(QFileInfo(c.filePath).fileName(), error));
    }
  }

  reloadList();

  // 成功だけなら、一覧の ⏳ と再起動バナーで結果が分かるのでダイアログは出さない。
  if (!failures.isEmpty()) {
    const QString message = staged > 0
      ? tr("%n plugin(s) will be installed when farman is restarted.", "", staged)
      : tr("No plugin was installed.");
    warn(this, tr("Install Plugins"),
         message + QStringLiteral("\n\n") + tr("Not installed:") + QLatin1Char('\n')
           + failures.join(QLatin1Char('\n')));
  }
}

void PluginsTab::setDropZoneActive(bool active) {
  static_cast<DropZoneFrame*>(m_dropZone)->setActive(active);
}

void PluginsTab::dragEnterEvent(QDragEnterEvent* event) {
  if (!localFilesFromMime(event->mimeData()).isEmpty()) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
    setDropZoneActive(true);
  } else {
    event->ignore();
  }
}

void PluginsTab::dragMoveEvent(QDragMoveEvent* event) {
  if (!localFilesFromMime(event->mimeData()).isEmpty()) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  } else {
    event->ignore();
  }
}

void PluginsTab::dragLeaveEvent(QDragLeaveEvent* event) {
  setDropZoneActive(false);
  QWidget::dragLeaveEvent(event);
}

void PluginsTab::dropEvent(QDropEvent* event) {
  setDropZoneActive(false);
  const QStringList files = localFilesFromMime(event->mimeData());
  if (files.isEmpty()) {
    event->ignore();
    return;
  }
  event->setDropAction(Qt::CopyAction);
  event->accept();
  // ドロップ処理の中でモーダルダイアログを出すとドラッグ元 (Finder 等) を
  // 待たせてしまうので、イベントを返してから導入フローに入る。
  QTimer::singleShot(0, this, [this, files]() { installFiles(files); });
}

void PluginsTab::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (m_checkedOnShow) return;
  m_checkedOnShow = true;
  // ページを初めて開いたときに、公式プラグインの更新を確認する (1 時間キャッシュ)。
  // 本体の「起動時にアップデートを確認」を切っている人は、自動では確認しない。
  if (Settings::instance().autoUpdateCheckOnStartup()) {
    checkForUpdates(/*force=*/false);
  } else {
    m_checkLabel->setText(
      tr("Press \"Check for Updates\" to look for new versions of the official "
         "plugins."));
  }
}

bool PluginsTab::eventFilter(QObject* watched, QEvent* event) {
  // 選択行のボタンを Tab 順に並べたもの (有効なものだけ)。
  const auto rowButtons = [this](int row) {
    QList<QPushButton*> buttons;
    for (const int col : kButtonColumns) {
      QPushButton* button = rowButton(row, col);
      if (button && button->isEnabled()) {
        buttons.append(button);
      }
    }
    return buttons;
  };
  const auto isTab = [](const QKeyEvent* e) {
    return e->key() == Qt::Key_Tab && e->modifiers() == Qt::NoModifier;
  };
  const auto isBacktab = [](const QKeyEvent* e) {
    return e->key() == Qt::Key_Backtab
        || (e->key() == Qt::Key_Tab && (e->modifiers() & Qt::ShiftModifier));
  };

  if (watched == m_table && event->type() == QEvent::KeyPress) {
    const auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter
        || keyEvent->key() == Qt::Key_Space) {
      showRowDetails(m_table->currentRow());
      return true;
    }
    if (isTab(keyEvent)) {
      // 一覧の次は「選択している行」のボタン。フォーカスチェーン任せだと常に 1 行目の
      // ボタンへ飛んでしまう。
      const QList<QPushButton*> buttons = rowButtons(m_table->currentRow());
      QWidget* next = buttons.isEmpty() ? static_cast<QWidget*>(m_installButton)
                                        : buttons.first();
      next->setFocus(Qt::TabFocusReason);
      return true;
    }
    return QWidget::eventFilter(watched, event);
  }

  if (watched == m_installButton && event->type() == QEvent::KeyPress) {
    // Shift+Tab は、チェーン上の直前 (最終行のボタン) ではなく一覧へ戻す。
    if (isBacktab(static_cast<QKeyEvent*>(event))) {
      m_table->setFocus(Qt::BacktabFocusReason);
      return true;
    }
    return QWidget::eventFilter(watched, event);
  }

  auto* button = qobject_cast<QPushButton*>(watched);
  if (button && button->property(kRowProperty).isValid()) {
    const int row = button->property(kRowProperty).toInt();
    if (event->type() == QEvent::FocusIn) {
      m_table->selectRow(row);   // ボタンにフォーカスが来たら、その行を選択行にする
    } else if (event->type() == QEvent::KeyPress) {
      const auto* keyEvent = static_cast<QKeyEvent*>(event);
      const bool tab = isTab(keyEvent);
      if (tab || isBacktab(keyEvent)) {
        // 同じ行のボタンの中だけを辿り、端まで来たら一覧の外 (次) / 一覧 (前) へ抜ける。
        const QList<QPushButton*> buttons = rowButtons(row);
        const int pos = buttons.indexOf(button);
        QWidget* next = nullptr;
        if (tab) {
          next = (pos >= 0 && pos + 1 < buttons.size())
                   ? static_cast<QWidget*>(buttons[pos + 1]) : m_installButton;
        } else {
          next = (pos > 0) ? static_cast<QWidget*>(buttons[pos - 1]) : m_table;
        }
        next->setFocus(tab ? Qt::TabFocusReason : Qt::BacktabFocusReason);
        return true;
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

} // namespace Farman
