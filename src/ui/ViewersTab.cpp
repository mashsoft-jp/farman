#include "ViewersTab.h"
#include "settings/Settings.h"
#include "types.h"
#include "viewer/ViewerDispatcher.h"
#include "viewer/IViewerPlugin.h"
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>

namespace Farman {

ViewersTab::ViewersTab(QWidget* parent)
  : QWidget(parent) {
  setupUi();
  loadSettings();
}

void ViewersTab::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);

  auto* displayGroup = new QGroupBox(tr("Viewer Display"), this);
  auto* displayForm = new QFormLayout(displayGroup);
  displayForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  m_viewerModeCombo = new QComboBox(displayGroup);
  m_viewerModeCombo->addItem(tr("Inline (in main window)"),
                             static_cast<int>(ViewerMode::Inline));
  m_viewerModeCombo->addItem(tr("External (separate windows)"),
                             static_cast<int>(ViewerMode::External));
  m_viewerModeCombo->setToolTip(tr(
    "Inline: show the viewer inside the main window (Enter / Esc returns to "
    "the file list).\n"
    "External: open a separate window per file (multiple files can be open "
    "side by side, can be moved to another display)."));
  displayForm->addRow(tr("Display mode:"), m_viewerModeCombo);
  mainLayout->addWidget(displayGroup);

  auto* group = new QGroupBox(tr("Viewer Associations"), this);
  auto* groupLayout = new QVBoxLayout(group);

  m_hintLabel = new QLabel(
    tr("Each viewer starts with the extensions declared by its plugin. Edit a "
       "row to override those defaults; unchanged rows keep following the "
       "plugin defaults."),
    group);
  m_hintLabel->setWordWrap(true);
  groupLayout->addWidget(m_hintLabel);

  m_table = new QTableWidget(group);
  m_table->setColumnCount(2);
  m_table->setHorizontalHeaderLabels({tr("Viewer"), tr("Extensions")});
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->verticalHeader()->setVisible(false);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  groupLayout->addWidget(m_table, 1);

  mainLayout->addWidget(group);
}

QString ViewersTab::normalizedExtension(const QString& extension) const {
  QString result = extension.trimmed().toLower();
  while (result.startsWith(QLatin1Char('.'))) {
    result.remove(0, 1);
  }
  return result;
}

QStringList ViewersTab::normalizedExtensions(const QString& text) const {
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

QString ViewersTab::extensionsTextForPlugin(
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

QStringList ViewersTab::defaultExtensionsForPlugin(IViewerPlugin* plugin) const {
  return plugin ? defaultExtensionsFromList(plugin->supportedExtensions()) : QStringList();
}

QStringList ViewersTab::defaultExtensionsFromList(const QStringList& source) const {
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

bool ViewersTab::hasViewerRow(const QString& pluginId) const {
  for (int row = 0; row < m_table->rowCount(); ++row) {
    const auto* item = m_table->item(row, 0);
    if (item && item->data(Qt::UserRole).toString() == pluginId) {
      return true;
    }
  }
  return false;
}

void ViewersTab::addViewerRow(const QString& pluginId,
                                         const QString& pluginName,
                                         const QStringList& extensions,
                                         const QStringList& defaultExtensions) {
  if (pluginId.isEmpty() || hasViewerRow(pluginId)) return;

  const int row = m_table->rowCount();
  m_table->insertRow(row);

  auto* viewerItem = new QTableWidgetItem(pluginName.isEmpty() ? pluginId : pluginName);
  viewerItem->setData(Qt::UserRole, pluginId);
  viewerItem->setToolTip(pluginId);
  viewerItem->setFlags(viewerItem->flags() & ~Qt::ItemIsEditable);
  m_table->setItem(row, 0, viewerItem);

  auto* edit = new QLineEdit(m_table);
  edit->setText(extensions.join(QStringLiteral(", ")));
  edit->setProperty("defaultExtensions", defaultExtensions);
  edit->setPlaceholderText(tr("mp4, mkv"));
  edit->setToolTip(tr("Comma, semicolon, or space separated extensions without leading dots."));
  m_table->setCellWidget(row, 1, edit);
}

void ViewersTab::loadSettings() {
  if (m_viewerModeCombo) {
    const auto mode = Settings::instance().viewerMode();
    for (int i = 0; i < m_viewerModeCombo->count(); ++i) {
      if (m_viewerModeCombo->itemData(i).toInt() == static_cast<int>(mode)) {
        m_viewerModeCombo->setCurrentIndex(i);
        break;
      }
    }
  }

  m_table->setRowCount(0);
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

void ViewersTab::save() {
  if (m_viewerModeCombo) {
    Settings::instance().setViewerMode(
      static_cast<ViewerMode>(m_viewerModeCombo->currentData().toInt()));
  }

  QMap<QString, QString> associations;
  for (int row = 0; row < m_table->rowCount(); ++row) {
    const auto* viewerItem = m_table->item(row, 0);
    auto* edit = qobject_cast<QLineEdit*>(m_table->cellWidget(row, 1));
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
  Settings::instance().setViewerAssociations(associations);
}

} // namespace Farman
