#pragma once

#include <QWidget>
#include <QMap>

class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace Farman {

class IViewerPlugin;

class ViewersTab : public QWidget {
  Q_OBJECT

public:
  explicit ViewersTab(QWidget* parent = nullptr);
  ~ViewersTab() override = default;

  void save();

private:
  void setupUi();
  void loadSettings();
  void addViewerRow(const QString& pluginId,
                    const QString& pluginName,
                    const QStringList& extensions,
                    const QStringList& defaultExtensions);
  QString normalizedExtension(const QString& extension) const;
  QStringList normalizedExtensions(const QString& text) const;
  QString extensionsTextForPlugin(const QMap<QString, QString>& associations,
                                  const QString& pluginId) const;
  QStringList defaultExtensionsForPlugin(IViewerPlugin* plugin) const;
  QStringList defaultExtensionsFromList(const QStringList& extensions) const;
  bool hasViewerRow(const QString& pluginId) const;

  QTableWidget* m_table = nullptr;
  QLabel*       m_hintLabel = nullptr;
  QComboBox*    m_viewerModeCombo = nullptr;
};

} // namespace Farman
