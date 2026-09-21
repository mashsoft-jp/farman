#include "PluginCatalogDialog.h"

#include "core/ArchiveDispatcher.h"
#include "core/PluginDownloader.h"
#include "core/PluginInstaller.h"
#include "core/UpdateChecker.h"
#include "settings/Settings.h"
#include "utils/Dialogs.h"
#include "utils/EnterClickFilter.h"
#include "utils/PluginCompat.h"
#include "viewer/ViewerDispatcher.h"

#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace Farman {

namespace {

enum Column { ColName = 0, ColKind, ColLatest, ColInstalled, ColStatus, ColAction, ColCount };

QString kindLabel(PluginInstaller::Kind kind) {
  switch (kind) {
    case PluginInstaller::Kind::Viewer:  return PluginCatalogDialog::tr("Viewer");
    case PluginInstaller::Kind::Archive: return PluginCatalogDialog::tr("Archive");
    case PluginInstaller::Kind::Unknown: break;
  }
  return QString();
}

} // namespace

PluginCatalogDialog::PluginCatalogDialog(QWidget* parent)
  : QDialog(parent) {
  setWindowTitle(tr("Official Plugins"));
  m_downloader = new PluginDownloader(this);
  connect(m_downloader, &PluginDownloader::progress, this,
          [this](qint64 received, qint64 total) {
    m_progressBar->setRange(0, total > 0 ? 1000 : 0);
    if (total > 0) {
      m_progressBar->setValue(static_cast<int>(received * 1000 / total));
    }
  });
  connect(m_downloader, &PluginDownloader::finished, this,
          &PluginCatalogDialog::onDownloadFinished);

  setupUi();

  connect(&PluginCatalog::instance(), &PluginCatalog::updated, this, [this]() {
    setBusy(false);
    reloadRows();
  });
  setBusy(true, tr("Getting the list of official plugins..."));
  PluginCatalog::instance().refresh(/*force=*/false);
}

PluginCatalogDialog::~PluginCatalogDialog() = default;

QString PluginCatalogDialog::uiLanguage() {
  // main.cpp の翻訳ロードと同じ言語解決 ("ja_JP" → "ja")。
  QString lang;
  switch (Settings::instance().language()) {
    case LanguageMode::English:  lang = QStringLiteral("en"); break;
    case LanguageMode::Japanese: lang = QStringLiteral("ja"); break;
    case LanguageMode::Auto:     lang = QLocale::system().name(); break;
  }
  return lang.section(QLatin1Char('_'), 0, 0);
}

void PluginCatalogDialog::setupUi() {
  auto* layout = new QVBoxLayout(this);
  m_enterClickFilter = new EnterClickFilter(this);

  auto* hint = new QLabel(
    tr("Plugins published by the farman project. Downloads are verified with "
       "their SHA256 checksum, and take effect after restarting farman."), this);
  hint->setWordWrap(true);
  layout->addWidget(hint);

  m_table = new QTableWidget(this);
  m_table->setWordWrap(false);
  m_table->setColumnCount(ColCount);
  m_table->setHorizontalHeaderLabels({
    tr("Name"), tr("Type"), tr("Latest"), tr("Installed"), tr("Status"), QString()
  });
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->verticalHeader()->setVisible(false);
  m_table->setTabKeyNavigation(false);
  m_table->installEventFilter(this);
  connect(m_table, &QTableWidget::currentCellChanged, this,
          [this]() { updateDetails(); });
  layout->addWidget(m_table, 1);

  // 選択中のプラグインの説明とリリースページへのリンク。
  m_detailsLabel = new QLabel(this);
  m_detailsLabel->setWordWrap(true);
  m_detailsLabel->setTextFormat(Qt::RichText);
  m_detailsLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
  m_detailsLabel->setOpenExternalLinks(true);
  m_detailsLabel->setMinimumHeight(fontMetrics().lineSpacing() * 4);
  m_detailsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  layout->addWidget(m_detailsLabel);

  m_sourceLabel = new QLabel(this);
  m_sourceLabel->setWordWrap(true);
  m_sourceLabel->setEnabled(false);
  layout->addWidget(m_sourceLabel);

  auto* busyRow = new QHBoxLayout();
  m_busyLabel = new QLabel(this);
  busyRow->addWidget(m_busyLabel);
  m_progressBar = new QProgressBar(this);
  m_progressBar->setTextVisible(false);
  busyRow->addWidget(m_progressBar, 1);
  layout->addLayout(busyRow);

  auto* buttonRow = new QHBoxLayout();
  m_reloadButton = new QPushButton(tr("Reload"), this);
  m_reloadButton->setAutoDefault(false);
  m_reloadButton->setToolTip(tr("Get the latest information again."));
  connect(m_reloadButton, &QPushButton::clicked, this, [this]() {
    setBusy(true, tr("Getting the list of official plugins..."));
    PluginCatalog::instance().refresh(/*force=*/true);
  });
  buttonRow->addWidget(m_reloadButton);
  buttonRow->addStretch(1);
  m_closeButton = new QPushButton(tr("Close"), this);
  applyAltShortcut(m_closeButton, Qt::Key_C);
  m_closeButton->setAutoDefault(false);
  connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
  buttonRow->addWidget(m_closeButton);
  layout->addLayout(buttonRow);
  m_enterClickFilter->installOnButtonsIn(m_reloadButton);
  m_enterClickFilter->installOnButtonsIn(m_closeButton);

  resize(760, 440);
}

void PluginCatalogDialog::setBusy(bool busy, const QString& message) {
  m_busy = busy;
  m_busyLabel->setText(message);
  m_busyLabel->setVisible(busy);
  m_progressBar->setVisible(busy);
  if (busy) {
    m_progressBar->setRange(0, 0);  // 不確定表示。ダウンロード中は progress で上書き
  }
  m_reloadButton->setEnabled(!busy);
  for (int row = 0; row < m_table->rowCount(); ++row) {
    if (QWidget* button = m_table->cellWidget(row, ColAction)) {
      button->setEnabled(!busy && row < m_rowStates.size()
                         && m_rowStates[row].action != RowState::Action::None);
    }
  }
}

PluginCatalogDialog::RowState PluginCatalogDialog::rowStateFor(
    const PluginCatalogEntry& entry) const {
  RowState state;
  const QString root = PluginInstaller::pluginsRoot();
  const QString probeName =
    entry.fileName + QLatin1Char('.') + PluginInstaller::nativeLibrarySuffix();
  const PluginInstaller::PendingState pending = PluginInstaller::pendingState(root);
  const QString base = PluginInstaller::pluginBaseName(probeName);
  const QString sub  = PluginInstaller::kindSubdir(entry.kind);

  // 導入済み / 導入待ちのファイル (基底名で判定)。
  bool pendingInstall = false;
  for (const QString& relPath : PluginInstaller::samePluginFiles(root, entry.kind, probeName)) {
    if (QFileInfo::exists(root + QLatin1Char('/') + relPath)) {
      state.installedFiles.append(relPath.section(QLatin1Char('/'), 1));
    }
    if (pending.installs.contains(relPath)) {
      pendingInstall = true;
    }
  }
  // ロード済みなら pluginId で版数が分かる。
  if (entry.kind == PluginInstaller::Kind::Viewer) {
    for (const PluginRecord& rec : ViewerDispatcher::instance().pluginRecords()) {
      if (rec.origin == PluginRecord::Origin::External && rec.pluginId == entry.id) {
        state.installedVersion = rec.version;
      }
    }
  } else {
    for (const ArchivePluginRecord& rec : ArchiveDispatcher::instance().pluginRecords()) {
      if (rec.origin == ArchivePluginRecord::Origin::External && rec.pluginId == entry.id) {
        state.installedVersion = rec.version;
      }
    }
  }
  bool pendingRemoval = false;
  for (const QString& relPath : pending.removals) {
    if (relPath.section(QLatin1Char('/'), 0, 0) == sub
        && PluginInstaller::pluginBaseName(relPath) == base
        && !pending.replacedBy.contains(relPath)) {
      pendingRemoval = true;
    }
  }

  // 退避中の変更があれば、それを優先して見せる (取り消しは Plugins ページの一覧で)。
  if (pendingInstall) {
    state.statusText = state.installedFiles.isEmpty()
      ? tr("⏳ Installed after restart") : tr("⏳ Updated after restart");
    return state;
  }
  if (pendingRemoval) {
    state.statusText = tr("⏳ Uninstalled after restart");
    return state;
  }

  switch (entry.releaseState) {
    case PluginCatalogEntry::ReleaseState::Unknown:
    case PluginCatalogEntry::ReleaseState::Failed:
      state.statusText = tr("Could not get release information");
      return state;
    case PluginCatalogEntry::ReleaseState::NoAssetForPlatform:
      state.statusText = tr("Not available for this platform");
      return state;
    case PluginCatalogEntry::ReleaseState::Ok:
      break;
  }
  if (!hostSatisfiesMinVersion(PluginInstaller::hostVersion(), entry.minFarmanVersion)) {
    state.statusText = tr("Requires farman %1 or later").arg(entry.minFarmanVersion);
    return state;
  }

  if (state.installedFiles.isEmpty() && !state.installedVersion.isEmpty()) {
    // ロードされているのにプラグインディレクトリに無い (同梱ディレクトリなど別の場所に
    // 置かれている)。ここから導入すると同じ ID が 2 つになるので、操作は出さない。
    state.statusText = tr("Installed outside the plugins directory");
  } else if (state.installedFiles.isEmpty()) {
    state.statusText = tr("Not installed");
    state.action = RowState::Action::Install;
  } else if (state.installedVersion.isEmpty()) {
    // 導入済みだがロードされていない (ブロック中 / 失敗 / 無効) ので版数が分からない。
    state.statusText = tr("Installed (version unknown)");
    state.action = RowState::Action::Reinstall;
  } else if (UpdateChecker::compareVersions(state.installedVersion, entry.latestVersion) < 0) {
    state.statusText = tr("Update available");
    state.action = RowState::Action::Update;
  } else {
    state.statusText = tr("Up to date");
  }
  return state;
}

void PluginCatalogDialog::reloadRows() {
  const int previousRow = m_table->currentRow();
  m_entries = PluginCatalog::instance().entries();
  m_rowStates.clear();
  const QString lang = uiLanguage();

  m_table->setRowCount(m_entries.size());
  const auto setItem = [this](int row, int col, const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setToolTip(text);
    m_table->setItem(row, col, item);
  };

  QWidget* previous = m_table;
  for (int row = 0; row < m_entries.size(); ++row) {
    const PluginCatalogEntry& entry = m_entries[row];
    const RowState state = rowStateFor(entry);
    m_rowStates.append(state);

    setItem(row, ColName, entry.name(lang));
    setItem(row, ColKind, kindLabel(entry.kind));
    setItem(row, ColLatest, entry.latestVersion.isEmpty() ? QStringLiteral("-")
                                                          : entry.latestVersion);
    setItem(row, ColInstalled,
            !state.installedVersion.isEmpty() ? state.installedVersion
            : state.installedFiles.isEmpty()  ? QStringLiteral("-")
                                              : tr("Yes"));
    setItem(row, ColStatus, state.statusText);
    if (entry.releaseState == PluginCatalogEntry::ReleaseState::Failed) {
      m_table->item(row, ColStatus)->setToolTip(entry.releaseError);
    }

    QString label;
    switch (state.action) {
      case RowState::Action::Install:   label = tr("Install"); break;
      case RowState::Action::Update:    label = tr("Update"); break;
      case RowState::Action::Reinstall: label = tr("Reinstall"); break;
      case RowState::Action::None:      break;
    }
    if (!label.isEmpty()) {
      auto* button = new QPushButton(label, m_table);
      button->setAutoDefault(false);
      button->setFocusPolicy(Qt::StrongFocus);
      m_enterClickFilter->installOnButtonsIn(button);
      connect(button, &QPushButton::clicked, this, [this, row]() {
        m_table->selectRow(row);
        runRowAction(row);
      });
      m_table->setCellWidget(row, ColAction, button);
      // 一覧 → 各行のボタン → 再読み込み の順に Tab で辿れるようにする。
      QWidget::setTabOrder(previous, button);
      previous = button;
    } else {
      m_table->removeCellWidget(row, ColAction);
    }
  }
  QWidget::setTabOrder(previous, m_reloadButton);

  m_table->resizeColumnsToContents();
  m_table->resizeRowsToContents();
  m_table->horizontalHeader()->setStretchLastSection(false);
  m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
  if (m_table->rowCount() > 0) {
    m_table->selectRow(qBound(0, previousRow, m_table->rowCount() - 1));
  }

  if (m_entries.isEmpty()) {
    m_sourceLabel->setText(tr("The list of official plugins could not be loaded."));
  } else if (!PluginCatalog::instance().manifestFromNetwork()) {
    m_sourceLabel->setText(
      tr("The online list could not be reached, so the list bundled with this "
         "farman is shown. Newer plugins may be missing."));
  } else {
    m_sourceLabel->clear();
  }
  m_sourceLabel->setVisible(!m_sourceLabel->text().isEmpty());
  updateDetails();
}

void PluginCatalogDialog::updateDetails() {
  const int row = m_table->currentRow();
  if (row < 0 || row >= m_entries.size()) {
    m_detailsLabel->clear();
    return;
  }
  const PluginCatalogEntry& entry = m_entries[row];
  QString html = entry.description(uiLanguage()).toHtmlEscaped();
  const QString url = entry.releaseUrl.isEmpty()
    ? QStringLiteral("https://github.com/%1").arg(entry.repo) : entry.releaseUrl;
  html += QStringLiteral("<br><a href=\"%1\">%2</a>")
            .arg(url.toHtmlEscaped(),
                 (entry.releaseUrl.isEmpty() ? tr("Repository") : tr("Release notes"))
                   .toHtmlEscaped());
  if (row < m_rowStates.size() && !m_rowStates[row].installedFiles.isEmpty()) {
    html += QStringLiteral("<br>")
          + tr("Installed file: %1")
              .arg(m_rowStates[row].installedFiles.join(QStringLiteral(", ")))
              .toHtmlEscaped();
  }
  m_detailsLabel->setText(html);
}

void PluginCatalogDialog::runRowAction(int row) {
  if (m_busy || row < 0 || row >= m_entries.size() || row >= m_rowStates.size()) return;
  const PluginCatalogEntry& entry = m_entries[row];
  const RowState& state = m_rowStates[row];
  if (state.action == RowState::Action::None) return;

  // 確認は 1 回。導入済みのファイルがあれば、アンインストール (置き換え) されることを
  // ここで伝える。配布物は SHA256 で照合するので、入手元の確認は求めない。
  const QString name = entry.name(uiLanguage());
  QString question = state.action == RowState::Action::Install
    ? tr("Install \"%1\" %2?").arg(name, entry.latestVersion)
    : tr("Update \"%1\" to %2?").arg(name, entry.latestVersion);
  // 同名なら上書き、別名なら古いファイルの削除を伴う。どちらも導入済みの分は消える。
  QStringList replaced;
  for (const QString& file : state.installedFiles) {
    if (file != entry.asset.name) {
      replaced.append(file);
    }
  }
  if (!replaced.isEmpty()) {
    question += QStringLiteral("\n\n")
              + tr("The installed file will be uninstalled: %1")
                  .arg(replaced.join(QStringLiteral(", ")));
  }
  if (!confirm(this, windowTitle(), question, /*defaultYes=*/true)) return;

  m_downloadingEntry = entry;
  setBusy(true, tr("Downloading %1...").arg(entry.asset.name));
  m_downloader->start(entry);
}

void PluginCatalogDialog::onDownloadFinished(bool ok, const QString& filePath,
                                             const QString& errorReason) {
  setBusy(false);
  if (!ok) {
    warn(this, windowTitle(), errorReason);
    reloadRows();
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

  if (!staged) {
    warn(this, windowTitle(), error);
  } else {
    m_stagedAny = true;
  }
  reloadRows();
}

bool PluginCatalogDialog::eventFilter(QObject* watched, QEvent* event) {
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
  return QDialog::eventFilter(watched, event);
}

void PluginCatalogDialog::reject() {
  // ダウンロード中に閉じたら中断する (照合前のファイルは残さない)。
  m_downloader->cancel();
  QDialog::reject();
}

} // namespace Farman
