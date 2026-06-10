#include "ViewersTab.h"
#include "settings/Settings.h"
#include "types.h"
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

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

  mainLayout->addStretch();
}

void ViewersTab::loadSettings() {
  const auto mode = Settings::instance().viewerMode();
  for (int i = 0; i < m_viewerModeCombo->count(); ++i) {
    if (m_viewerModeCombo->itemData(i).toInt() == static_cast<int>(mode)) {
      m_viewerModeCombo->setCurrentIndex(i);
      break;
    }
  }
}

void ViewersTab::save() {
  Settings::instance().setViewerMode(
    static_cast<ViewerMode>(m_viewerModeCombo->currentData().toInt()));
}

} // namespace Farman
