#include "PluginsTab.h"

#include "core/ArchiveDispatcher.h"
#include "settings/Settings.h"
#include "utils/Dialogs.h"
#include "viewer/ViewerDispatcher.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace Farman {

namespace {

enum Column { ColStatus = 0, ColName, ColKind, ColVersion, ColAuthor, ColAction, ColCount };

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
  setupUi();
  loadSettings();
}

void PluginsTab::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);

  // ── 外部プラグインの一覧と導入 ──
  auto* listGroup = new QGroupBox(tr("External Plugins"), this);
  auto* listLayout = new QVBoxLayout(listGroup);

  auto* listHint = new QLabel(
    tr("Install, update and uninstall external plugins here, for viewers and "
       "archives alike. Changes take effect after restarting farman. To turn a "
       "plugin on or off or change its settings, use the Viewer / Archive pages."),
    listGroup);
  listHint->setWordWrap(true);
  listLayout->addWidget(listHint);

  m_table = new QTableWidget(listGroup);
  m_table->setWordWrap(false);
  m_table->setColumnCount(ColCount);
  m_table->setHorizontalHeaderLabels({
    tr("Status"), tr("Name"), tr("Type"), tr("Version"), tr("Author"), QString()
  });
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->verticalHeader()->setVisible(false);
  m_table->setTabKeyNavigation(false);
  m_table->installEventFilter(this);
  listLayout->addWidget(m_table, 1);

  m_emptyLabel = new QLabel(
    tr("No external plugins are installed. Drop plugin files onto this page, or "
       "use \"Install from File...\"."), listGroup);
  m_emptyLabel->setWordWrap(true);
  m_emptyLabel->setAlignment(Qt::AlignCenter);
  m_emptyLabel->setEnabled(false);
  listLayout->addWidget(m_emptyLabel, 1);

  auto* installRow = new QHBoxLayout();
  m_installButton = new QPushButton(tr("Install from File..."), listGroup);
  m_installButton->setAutoDefault(false);
  m_installButton->setToolTip(
    tr("Choose plugin files (.%1) to install. The plugin type (viewer / archive) "
       "is detected automatically.").arg(PluginInstaller::nativeLibrarySuffix()));
  connect(m_installButton, &QPushButton::clicked, this, &PluginsTab::chooseFiles);
  installRow->addWidget(m_installButton);
  auto* dropHint = new QLabel(
    tr("You can also drop plugin files onto this page."), listGroup);
  dropHint->setEnabled(false);
  installRow->addWidget(dropHint, 1);
  listLayout->addLayout(installRow);

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

  mainLayout->addWidget(listGroup, 1);

  // ── 読込み設定 (ビュアー / アーカイブ共通の置き場所) ──
  // 個々のプラグインの有効 / 無効は「ビュアー」「アーカイブ」の各タブが持つ。
  // ここは「そもそも外部を読み込むか / どこから読み込むか」だけを扱う。
  QGroupBox* pluginGroup = new QGroupBox(tr("Loading"), this);
  QVBoxLayout* pluginLayout = new QVBoxLayout(pluginGroup);

  m_allowExternalPluginsCheck =
    new QCheckBox(tr("Allow loading external plugins"), pluginGroup);
  m_allowExternalPluginsCheck->setToolTip(
    tr("When enabled, plugins placed in the directory below are loaded at "
       "startup. External plugins are third-party native code and run with "
       "the same privileges as Farman — only enable this if you trust their "
       "source. Changes take effect on next launch."));
  pluginLayout->addWidget(m_allowExternalPluginsCheck);

  QLabel* pluginSecurityHint = new QLabel(
    tr("⚠ External plugins are native code and run with full application "
       "privileges. Only enable plugins from sources you trust."), pluginGroup);
  pluginSecurityHint->setWordWrap(true);
  pluginSecurityHint->setEnabled(false);
  pluginLayout->addWidget(pluginSecurityHint);

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
  const auto addRecord = [&](PluginInstaller::Kind kind, const QString& filePath,
                             const QString& pluginName, const QString& version,
                             const QString& author, const QString& errorReason,
                             bool loaded, bool disabledByUser, bool blocked) {
    Row row;
    row.kind        = kind;
    row.filePath    = filePath;
    row.relPath     = PluginInstaller::managedRelativePath(root, filePath);
    row.name        = pluginName.isEmpty() ? QFileInfo(filePath).fileName() : pluginName;
    row.version     = version;
    row.author      = author;
    row.errorReason = errorReason;
    row.loaded      = loaded;
    row.disabledByUser          = disabledByUser;
    row.blockedExternalDisabled = blocked;
    row.installed   = true;
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
    addRecord(PluginInstaller::Kind::Viewer, rec.filePath, rec.pluginName, rec.version,
              rec.author, rec.errorReason, rec.loaded, rec.disabledByUser,
              rec.blockedExternalDisabled);
  }
  for (const ArchivePluginRecord& rec : ArchiveDispatcher::instance().pluginRecords()) {
    if (rec.origin != ArchivePluginRecord::Origin::External) continue;
    addRecord(PluginInstaller::Kind::Archive, rec.filePath, rec.pluginName, rec.version,
              rec.author, rec.errorReason, rec.loaded, rec.disabledByUser,
              rec.blockedExternalDisabled);
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
  return rows;
}

QString PluginsTab::statusEmoji(const Row& row) const {
  if (row.pendingInstall || row.pendingRemoval) return QStringLiteral("⏳");
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
  if (row.loaded) return tr("Loaded");
  if (row.blockedExternalDisabled) return tr("Blocked (external plugins off)");
  if (row.disabledByUser) return tr("Disabled");
  return row.errorReason.isEmpty() ? tr("Failed")
                                   : tr("Failed: %1").arg(row.errorReason);
}

void PluginsTab::reloadList() {
  m_rows = collectRows();
  m_table->setRowCount(m_rows.size());

  const auto setItem = [this](int row, int col, const QString& text,
                              const QString& toolTip = QString()) {
    auto* item = new QTableWidgetItem(text);
    item->setToolTip(toolTip.isEmpty() ? text : toolTip);
    m_table->setItem(row, col, item);
    return item;
  };

  bool anyPending = false;
  for (int i = 0; i < m_rows.size(); ++i) {
    const Row& row = m_rows[i];
    const bool pending = row.pendingInstall || row.pendingRemoval;
    anyPending = anyPending || pending;

    auto* statusItem = setItem(i, ColStatus, statusEmoji(row), statusText(row));
    statusItem->setTextAlignment(Qt::AlignCenter);

    auto* nameItem = setItem(i, ColName, row.name,
                             row.filePath.isEmpty() ? row.relPath : row.filePath);
    if (!pending && !row.loaded && !row.disabledByUser
        && !row.blockedExternalDisabled) {
      nameItem->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
      nameItem->setToolTip(statusText(row));
    }
    setItem(i, ColKind, kindLabel(row.kind));
    setItem(i, ColVersion, row.version.isEmpty() ? QStringLiteral("-") : row.version);
    setItem(i, ColAuthor, row.author.isEmpty() ? QStringLiteral("-") : row.author);

    // 操作ボタン: 退避中なら取り消し、そうでなければアンインストール。
    // プラグインディレクトリの外にある外部プラグイン (同梱ディレクトリに置かれた
    // 第三者製など) は farman からは消せない。
    auto* button = new QPushButton(pending ? tr("Cancel") : tr("Uninstall..."), m_table);
    button->setAutoDefault(false);
    button->setFocusPolicy(Qt::NoFocus);  // キーボードからは一覧の Enter / Space で押す
    if (row.relPath.isEmpty()) {
      button->setEnabled(false);
      button->setToolTip(
        tr("This plugin is outside the plugins directory, so farman cannot "
           "uninstall it."));
    } else if (row.pendingRemoval) {
      button->setToolTip(tr("Keep this plugin installed."));
    } else if (row.pendingInstall) {
      button->setToolTip(row.installed ? tr("Do not update this plugin.")
                                       : tr("Do not install this plugin."));
    } else {
      button->setToolTip(tr("Delete this plugin when farman is restarted."));
    }
    connect(button, &QPushButton::clicked, this, [this, i]() {
      m_table->selectRow(i);
      runRowAction(i);
    });
    m_table->setCellWidget(i, ColAction, button);
  }

  m_table->resizeColumnsToContents();
  m_table->resizeRowsToContents();
  m_table->horizontalHeader()->setStretchLastSection(false);
  m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);

  m_table->setVisible(!m_rows.isEmpty());
  m_emptyLabel->setVisible(m_rows.isEmpty());

  // 削除待ちは一覧の行から分かるが、退避だけが残っている場合 (例: 既に手で消された
  // ファイルの削除記録) も再起動で片付くので、退避が 1 件でもあればバナーを出す。
  const PluginInstaller::PendingState state =
    PluginInstaller::pendingState(PluginInstaller::pluginsRoot());
  m_restartBanner->setVisible(anyPending || !state.installs.isEmpty()
                              || !state.removals.isEmpty());
}

void PluginsTab::runRowAction(int index) {
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

  // 押されたボタン自身が一覧の作り直しで破棄されるので、シグナル処理を抜けてから行う。
  QTimer::singleShot(0, this, [this]() { reloadList(); });
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

  // 外部プラグインの読込みが OFF のままだと導入しても動かない。黙って ON には
  // せず、ここで尋ねる。断っても導入は続ける (一覧には「ブロック中」で出る)。
  // ON にするのはこのページのチェックで、保存は OK / 適用 / 再起動のとき。
  if (!m_allowExternalPluginsCheck->isChecked()) {
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

void PluginsTab::dragEnterEvent(QDragEnterEvent* event) {
  if (!localFilesFromMime(event->mimeData()).isEmpty()) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
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

void PluginsTab::dropEvent(QDropEvent* event) {
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

bool PluginsTab::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_table && event->type() == QEvent::KeyPress) {
    const auto* keyEvent = static_cast<QKeyEvent*>(event);
    switch (keyEvent->key()) {
      case Qt::Key_Return:
      case Qt::Key_Enter:
      case Qt::Key_Space:
        runRowAction(m_table->currentRow());
        return true;
      default:
        break;
    }
  }
  return QWidget::eventFilter(watched, event);
}

} // namespace Farman
