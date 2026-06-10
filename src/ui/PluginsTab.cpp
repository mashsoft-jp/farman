#include "PluginsTab.h"
#include "settings/Settings.h"
#include "viewer/IViewerPlugin.h"
#include "viewer/ViewerDispatcher.h"
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSet>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace Farman {

PluginsTab::PluginsTab(QWidget* parent)
  : QWidget(parent) {
  setupUi();
  loadSettings();
}

void PluginsTab::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);

  // ─── プラグインディレクトリ (旧 General → Viewer Plugins) ───
  auto* dirGroup = new QGroupBox(tr("Plugins Directory"), this);
  auto* dirLayout = new QVBoxLayout(dirGroup);

  auto* dirHint = new QLabel(
    tr("External plugins are loaded on startup from this directory. "
       "Leave empty to use the default user plugins directory."), dirGroup);
  dirHint->setWordWrap(true);
  dirLayout->addWidget(dirHint);

  auto* dirRow = new QWidget(dirGroup);
  auto* dirRowLayout = new QHBoxLayout(dirRow);
  dirRowLayout->setContentsMargins(0, 0, 0, 0);

  m_pluginsDirectoryEdit = new QLineEdit(dirGroup);
  m_pluginsDirectoryEdit->setPlaceholderText(Settings::defaultPluginsDirectory());
  m_pluginsDirectoryEdit->setToolTip(
    tr("Directory containing external viewer plugins (.dylib, .so, .dll). "
       "Changes take effect on next launch."));
  m_pluginsDirectoryBrowse = new QToolButton(dirGroup);
  m_pluginsDirectoryBrowse->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
  m_pluginsDirectoryBrowse->setToolTip(tr("Choose plugins directory..."));
  m_pluginsDirectoryDefault = new QToolButton(dirGroup);
  m_pluginsDirectoryDefault->setText(tr("Default"));
  m_pluginsDirectoryDefault->setToolTip(
    tr("Use the default user plugins directory."));

  dirRowLayout->addWidget(new QLabel(tr("Directory:"), dirGroup));
  dirRowLayout->addWidget(m_pluginsDirectoryEdit, 1);
  dirRowLayout->addWidget(m_pluginsDirectoryBrowse);
  dirRowLayout->addWidget(m_pluginsDirectoryDefault);
  dirLayout->addWidget(dirRow);

  connect(m_pluginsDirectoryBrowse, &QToolButton::clicked, this, [this]() {
    const QString start = m_pluginsDirectoryEdit->text().isEmpty()
                          ? Settings::defaultPluginsDirectory()
                          : m_pluginsDirectoryEdit->text();
    const QString selected = QFileDialog::getExistingDirectory(
      this, tr("Choose plugins directory"), start,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!selected.isEmpty()) {
      m_pluginsDirectoryEdit->setText(selected);
    }
  });
  connect(m_pluginsDirectoryDefault, &QToolButton::clicked,
          m_pluginsDirectoryEdit, &QLineEdit::clear);

  mainLayout->addWidget(dirGroup);

  // ─── プラグイン一覧 (旧 Help → Plugins... ダイアログ) ───
  auto* listGroup = new QGroupBox(tr("Installed Plugins"), this);
  auto* listLayout = new QVBoxLayout(listGroup);

  auto* listHint = new QLabel(
    tr("Plugins are loaded on startup. Enable/disable changes take effect "
       "after restarting farman. Bundled plugins cannot be disabled."),
    listGroup);
  listHint->setWordWrap(true);
  listLayout->addWidget(listHint);

  m_pluginTable = new QTableWidget(listGroup);
  m_pluginTable->setColumnCount(8);
  m_pluginTable->setHorizontalHeaderLabels({
    tr("Enabled"),
    tr("Type"),
    tr("Status"),
    tr("Origin"),
    tr("Plugin ID"),
    tr("Name"),
    tr("Path"),
    tr("Error")
  });
  m_pluginTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_pluginTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_pluginTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_pluginTable->verticalHeader()->setVisible(false);
  // Tab はセル間移動ではなく次のウィジェット (ビュアー関連付け) へ抜ける。
  // テーブルにフォーカスがある間の行移動は ↑/↓ で行う。
  // 遷移自体は eventFilter で明示的に行う (ヘッダコメント参照)。
  m_pluginTable->setTabKeyNavigation(false);
  m_pluginTable->installEventFilter(this);
  listLayout->addWidget(m_pluginTable, 1);

  mainLayout->addWidget(listGroup, 1);

  // ─── ビュアーの拡張子紐付け (旧 Viewers → Viewer Associations) ───
  auto* assocGroup = new QGroupBox(tr("Viewer Associations"), this);
  auto* assocLayout = new QVBoxLayout(assocGroup);

  auto* assocHint = new QLabel(
    tr("Each viewer starts with the extensions declared by its plugin. Edit a "
       "row to override those defaults; unchanged rows keep following the "
       "plugin defaults."),
    assocGroup);
  assocHint->setWordWrap(true);
  assocLayout->addWidget(assocHint);

  m_assocTable = new QTableWidget(assocGroup);
  m_assocTable->setColumnCount(2);
  m_assocTable->setHorizontalHeaderLabels({tr("Viewer"), tr("Extensions")});
  m_assocTable->horizontalHeader()->setStretchLastSection(true);
  m_assocTable->verticalHeader()->setVisible(false);
  m_assocTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_assocTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_assocTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  // こちらも Tab でのセル間移動はせず、拡張子の QLineEdit (セルウィジェット)
  // を通常のフォーカスチェーンとして辿らせる。
  m_assocTable->setTabKeyNavigation(false);
  m_assocTable->installEventFilter(this);
  assocLayout->addWidget(m_assocTable, 1);

  mainLayout->addWidget(assocGroup, 1);
}

bool PluginsTab::eventFilter(QObject* watched, QEvent* event) {
  const bool isTable = (watched == m_pluginTable || watched == m_assocTable);
  if (!isTable) return QWidget::eventFilter(watched, event);
  auto* table = static_cast<QTableWidget*>(watched);

  // フォーカスを得たとき行未選択なら先頭行を選び、すぐ ↑/↓ で動かせるようにする。
  if (event->type() == QEvent::FocusIn) {
    if (table->currentRow() < 0 && table->rowCount() > 0) {
      table->selectRow(0);
    }
    return QWidget::eventFilter(watched, event);
  }

  if (event->type() == QEvent::KeyPress) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const bool backtab =
      keyEvent->key() == Qt::Key_Backtab
      || (keyEvent->key() == Qt::Key_Tab
          && (keyEvent->modifiers() & Qt::ShiftModifier));
    const bool tab =
      !backtab && keyEvent->key() == Qt::Key_Tab && keyEvent->modifiers() == Qt::NoModifier;

    if (watched == m_pluginTable) {
      if (tab) {
        m_assocTable->setFocus(Qt::TabFocusReason);
        return true;
      }
      if (backtab) {
        m_pluginsDirectoryEdit->setFocus(Qt::BacktabFocusReason);
        return true;
      }
    } else {  // m_assocTable
      if (backtab) {
        m_pluginTable->setFocus(Qt::BacktabFocusReason);
        return true;
      }
      // Tab はそのまま既定動作 (フォーカスチェーンの次へ) に任せる。
    }
  }
  return QWidget::eventFilter(watched, event);
}

void PluginsTab::loadSettings() {
  m_pluginsDirectoryEdit->setText(Settings::instance().pluginsDirectory());
  loadPluginList();
  loadAssociations();
}

void PluginsTab::loadPluginList() {
  const QList<PluginRecord> records =
    ViewerDispatcher::instance().pluginRecords();

  m_pluginTable->setRowCount(records.size());

  auto setItem = [this](int row, int col, const QString& text,
                        const QString& toolTip = QString()) {
    auto* item = new QTableWidgetItem(text);
    item->setToolTip(toolTip.isEmpty() ? text : toolTip);
    m_pluginTable->setItem(row, col, item);
  };

  // パス列はフルパスだと長すぎて収まらないので短縮表示する:
  // 外部プラグインはプラグインディレクトリからの相対パス、同梱プラグインは
  // アプリバンドル内 (場所は実装詳細) なのでファイル名のみ。
  // フルパスはツールチップで参照できる。
  const QString settingsDir = Settings::instance().pluginsDirectory();
  const QDir pluginsDir(settingsDir.isEmpty()
                          ? Settings::defaultPluginsDirectory()
                          : settingsDir);
  auto displayPath = [&pluginsDir](const PluginRecord& rec) -> QString {
    if (rec.filePath.isEmpty()) return {};
    if (rec.origin == PluginRecord::Origin::External) {
      const QString relative = pluginsDir.relativeFilePath(rec.filePath);
      // ディレクトリ外 (../ で始まる) ならフルパスのまま見せる。
      if (!relative.startsWith(QStringLiteral(".."))) return relative;
      return rec.filePath;
    }
    return QFileInfo(rec.filePath).fileName();
  };

  for (int row = 0; row < records.size(); ++row) {
    const PluginRecord& rec = records[row];
    const bool isExternal = rec.origin == PluginRecord::Origin::External;
    auto* enabledItem = new QTableWidgetItem();
    enabledItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    enabledItem->setData(Qt::UserRole, rec.pluginId);
    enabledItem->setData(Qt::UserRole + 1, isExternal);
    if (isExternal && !rec.pluginId.isEmpty()) {
      enabledItem->setFlags(enabledItem->flags() | Qt::ItemIsUserCheckable);
      enabledItem->setCheckState(
        Settings::instance().isViewerPluginDisabled(rec.pluginId)
          ? Qt::Unchecked
          : Qt::Checked);
      enabledItem->setToolTip(tr("Changes take effect after restarting farman."));
    } else {
      enabledItem->setCheckState(Qt::Checked);
      enabledItem->setToolTip(isExternal
                                ? tr("Plugin ID is unavailable, so this plugin cannot be toggled.")
                                : tr("Bundled plugins cannot be disabled."));
    }
    m_pluginTable->setItem(row, 0, enabledItem);

    // 現状はビュアープラグインのみ。将来 Content / FS / Archive 等の
    // 種別が増えたらここで出し分ける。
    setItem(row, 1, tr("Viewer"));
    setItem(row, 2, rec.loaded
                    ? tr("Loaded")
                    : (rec.disabledByUser
                         ? tr("Disabled")
                         : tr("Failed")));
    setItem(row, 3, rec.origin == PluginRecord::Origin::Bundled
                      ? tr("Bundled")
                      : tr("External"));
    setItem(row, 4, rec.pluginId);
    setItem(row, 5, rec.pluginName);
    setItem(row, 6, displayPath(rec), rec.filePath);
    setItem(row, 7, rec.errorReason);
  }

  m_pluginTable->resizeColumnsToContents();
  m_pluginTable->horizontalHeader()->setStretchLastSection(true);
  m_pluginTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
}

QString PluginsTab::normalizedExtension(const QString& extension) const {
  QString result = extension.trimmed().toLower();
  while (result.startsWith(QLatin1Char('.'))) {
    result.remove(0, 1);
  }
  return result;
}

QStringList PluginsTab::normalizedExtensions(const QString& text) const {
  QSet<QString> extensions;
  const QStringList parts = text.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                       Qt::SkipEmptyParts);
  for (const QString& part : parts) {
    const QString normalized = normalizedExtension(part);
    if (!normalized.isEmpty()) {
      extensions.insert(normalized);
    }
  }
  QStringList result = extensions.values();
  result.sort(Qt::CaseInsensitive);
  return result;
}

QString PluginsTab::extensionsTextForPlugin(
  const QMap<QString, QString>& associations,
  const QString& pluginId) const {
  QStringList extensions;
  for (auto it = associations.cbegin(); it != associations.cend(); ++it) {
    if (it.value() == pluginId) {
      extensions.append(it.key());
    }
  }
  extensions.sort(Qt::CaseInsensitive);
  return extensions.join(QStringLiteral(", "));
}

QStringList PluginsTab::defaultExtensionsForPlugin(IViewerPlugin* plugin) const {
  return plugin ? defaultExtensionsFromList(plugin->supportedExtensions()) : QStringList();
}

QStringList PluginsTab::defaultExtensionsFromList(const QStringList& source) const {
  QStringList extensions;
  for (const QString& ext : source) {
    const QString normalized = normalizedExtension(ext);
    if (!normalized.isEmpty() && !extensions.contains(normalized)) {
      extensions.append(normalized);
    }
  }
  extensions.sort(Qt::CaseInsensitive);
  return extensions;
}

bool PluginsTab::hasViewerRow(const QString& pluginId) const {
  for (int row = 0; row < m_assocTable->rowCount(); ++row) {
    const auto* item = m_assocTable->item(row, 0);
    if (item && item->data(Qt::UserRole).toString() == pluginId) {
      return true;
    }
  }
  return false;
}

void PluginsTab::addViewerRow(const QString& pluginId,
                              const QString& pluginName,
                              const QStringList& extensions,
                              const QStringList& defaultExtensions) {
  if (pluginId.isEmpty() || hasViewerRow(pluginId)) return;

  const int row = m_assocTable->rowCount();
  m_assocTable->insertRow(row);

  auto* viewerItem = new QTableWidgetItem(pluginName.isEmpty() ? pluginId : pluginName);
  viewerItem->setData(Qt::UserRole, pluginId);
  viewerItem->setToolTip(pluginId);
  viewerItem->setFlags(viewerItem->flags() & ~Qt::ItemIsEditable);
  m_assocTable->setItem(row, 0, viewerItem);

  auto* edit = new QLineEdit(m_assocTable);
  edit->setText(extensions.join(QStringLiteral(", ")));
  edit->setProperty("defaultExtensions", defaultExtensions);
  edit->setPlaceholderText(tr("mp4, mkv"));
  edit->setToolTip(tr("Comma, semicolon, or space separated extensions without leading dots."));
  m_assocTable->setCellWidget(row, 1, edit);
}

void PluginsTab::loadAssociations() {
  m_assocTable->setRowCount(0);
  const QMap<QString, QString> associations =
    Settings::instance().viewerAssociations();
  QSet<QString> assignedExtensions;
  for (auto it = associations.cbegin(); it != associations.cend(); ++it) {
    assignedExtensions.insert(it.key());
  }

  QSet<QString> loadedPluginIds;
  QSet<QString> externalPluginIds;
  QMap<QString, PluginRecord> disabledPluginRecords;
  const QList<PluginRecord> records = ViewerDispatcher::instance().pluginRecords();
  for (const PluginRecord& record : records) {
    if (record.loaded
        && record.origin == PluginRecord::Origin::External
        && !record.pluginId.isEmpty()) {
      externalPluginIds.insert(record.pluginId);
    } else if (!record.loaded
               && record.origin == PluginRecord::Origin::External
               && !record.pluginId.isEmpty()
               && record.disabledByUser) {
      disabledPluginRecords.insert(record.pluginId, record);
    }
  }

  QList<IViewerPlugin*> plugins = ViewerDispatcher::instance().allPlugins();
  std::stable_sort(plugins.begin(), plugins.end(),
                   [&externalPluginIds](IViewerPlugin* lhs, IViewerPlugin* rhs) {
    const bool lhsExternal = lhs && externalPluginIds.contains(lhs->pluginId());
    const bool rhsExternal = rhs && externalPluginIds.contains(rhs->pluginId());
    return lhsExternal && !rhsExternal;
  });

  auto addDisabledPluginRows = [&]() {
    QStringList disabledPluginIds = disabledPluginRecords.keys();
    disabledPluginIds.sort(Qt::CaseInsensitive);
    for (const QString& pluginId : disabledPluginIds) {
      if (loadedPluginIds.contains(pluginId)) continue;
      const PluginRecord record = disabledPluginRecords.value(pluginId);
      loadedPluginIds.insert(pluginId);
      const QStringList explicitExtensions =
        normalizedExtensions(extensionsTextForPlugin(associations, pluginId));
      QStringList defaults = defaultExtensionsFromList(record.supportedExtensions);
      QStringList visibleExtensions = explicitExtensions;
      if (visibleExtensions.isEmpty()) {
        for (int i = defaults.size() - 1; i >= 0; --i) {
          if (assignedExtensions.contains(defaults.at(i))) {
            defaults.removeAt(i);
          }
        }
        visibleExtensions = defaults;
      }
      const QString name = record.pluginName.isEmpty()
                             ? tr("Disabled plugin")
                             : tr("%1 (disabled)").arg(record.pluginName);
      addViewerRow(pluginId, name, visibleExtensions, defaults);
    }
  };

  bool disabledRowsInserted = false;
  for (IViewerPlugin* plugin : plugins) {
    if (!plugin) continue;
    const QString pluginId = plugin->pluginId();
    if (!disabledRowsInserted && !externalPluginIds.contains(pluginId)) {
      addDisabledPluginRows();
      disabledRowsInserted = true;
    }
    loadedPluginIds.insert(pluginId);
    const QStringList explicitExtensions =
      normalizedExtensions(extensionsTextForPlugin(associations, pluginId));
    QStringList defaults = defaultExtensionsForPlugin(plugin);
    QStringList visibleExtensions = explicitExtensions;
    if (visibleExtensions.isEmpty()) {
      for (int i = defaults.size() - 1; i >= 0; --i) {
        if (assignedExtensions.contains(defaults.at(i))) {
          defaults.removeAt(i);
        }
      }
      visibleExtensions = defaults;
    }
    addViewerRow(pluginId, plugin->pluginName(), visibleExtensions, defaults);
  }
  if (!disabledRowsInserted) {
    addDisabledPluginRows();
  }

  QSet<QString> missingPluginIds;
  for (auto it = associations.cbegin(); it != associations.cend(); ++it) {
    if (!loadedPluginIds.contains(it.value())) {
      missingPluginIds.insert(it.value());
    }
  }
  QStringList missing = missingPluginIds.values();
  missing.sort(Qt::CaseInsensitive);
  for (const QString& pluginId : missing) {
    addViewerRow(pluginId, tr("Missing plugin"),
                 normalizedExtensions(extensionsTextForPlugin(associations, pluginId)),
                 QStringList());
  }
}

void PluginsTab::save() {
  auto& settings = Settings::instance();
  m_restartRequiredOnSave = false;

  // プラグインディレクトリ (次回起動から有効)
  const QString newDirectory = m_pluginsDirectoryEdit->text().trimmed();
  if (newDirectory != settings.pluginsDirectory()) {
    settings.setPluginsDirectory(newDirectory);
    m_restartRequiredOnSave = true;
  }

  // 外部プラグインの有効 / 無効 (次回起動から有効)
  QStringList disabled = settings.disabledViewerPlugins();
  for (int row = 0; row < m_pluginTable->rowCount(); ++row) {
    const auto* enabledItem = m_pluginTable->item(row, 0);
    if (!enabledItem || !enabledItem->data(Qt::UserRole + 1).toBool()) {
      continue;
    }
    const QString pluginId = enabledItem->data(Qt::UserRole).toString();
    if (pluginId.isEmpty()) continue;
    disabled.removeAll(pluginId);
    if (enabledItem->checkState() == Qt::Unchecked) {
      disabled.append(pluginId);
    }
  }
  disabled.sort(Qt::CaseInsensitive);
  disabled.removeDuplicates();
  if (disabled != settings.disabledViewerPlugins()) {
    settings.setDisabledViewerPlugins(disabled);
    m_restartRequiredOnSave = true;
  }

  // ビュアーの拡張子紐付け
  QMap<QString, QString> associations;
  for (int row = 0; row < m_assocTable->rowCount(); ++row) {
    const auto* viewerItem = m_assocTable->item(row, 0);
    auto* edit = qobject_cast<QLineEdit*>(m_assocTable->cellWidget(row, 1));
    if (!viewerItem || !edit) continue;
    const QString pluginId = viewerItem->data(Qt::UserRole).toString();
    if (pluginId.isEmpty()) continue;
    const QStringList extensions = normalizedExtensions(edit->text());
    const QStringList defaultExtensions =
      edit->property("defaultExtensions").toStringList();
    if (extensions == defaultExtensions) {
      continue;
    }
    for (const QString& extension : extensions) {
      if (!associations.contains(extension)) {
        associations.insert(extension, pluginId);
      }
    }
  }
  settings.setViewerAssociations(associations);
}

} // namespace Farman
