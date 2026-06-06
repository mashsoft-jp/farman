#pragma once

#include <QWidget>

class QComboBox;
class QCheckBox;
class QSpinBox;
class QLineEdit;
class QToolButton;

namespace Farman {

class BehaviorTab : public QWidget {
  Q_OBJECT

public:
  explicit BehaviorTab(QWidget* parent = nullptr);
  ~BehaviorTab() override = default;

  void save();

private:
  void setupUi();
  void loadSettings();

  // Sort settings
  QComboBox*  m_sortKeyCombo;
  QComboBox*  m_sortOrderCombo;
  QComboBox*  m_sortKey2ndCombo;
  QComboBox*  m_sortDirsTypeCombo;
  QCheckBox*  m_sortDotFirstCheck;
  QCheckBox*  m_sortCaseSensitiveCheck;

  // Display settings
  QCheckBox*  m_showHiddenCheck;
  QCheckBox*  m_cursorLoopCheck;
  QCheckBox*  m_persistHistoryCheck;
  QCheckBox*  m_typeAheadDotfilesCheck;
  QCheckBox*  m_syncBrowseShowDisabledDialogCheck = nullptr;

  // File operation settings
  QLineEdit*  m_autoRenameTemplateEdit;
  QCheckBox*  m_defaultDeleteToTrashCheck;
  QCheckBox*  m_progressAutoCloseCheck = nullptr;
  QLineEdit*  m_searchExcludeDirsEdit;

  // List display formats (per dual / single pane)
  QComboBox*  m_fileSizeFormatDualCombo        = nullptr;
  QComboBox*  m_fileSizeFormatSingleCombo      = nullptr;
  QCheckBox*  m_fileSizeThousandsSepDualCheck  = nullptr;
  QCheckBox*  m_fileSizeThousandsSepSingleCheck= nullptr;
  QComboBox*  m_dateTimeFormatDualCombo        = nullptr;
  QComboBox*  m_dateTimeFormatSingleCombo      = nullptr;

  // List columns visibility (per dual / single pane)
  // インデックスは FileListModel::Column の Type=1..LinkTarget=9 に対応
  // (Name は固定 ON のため UI を持たない)
  static constexpr int kColumnToggleCount = 9;
  QCheckBox*  m_listColumnDualCheck  [kColumnToggleCount] = {};
  QCheckBox*  m_listColumnSingleCheck[kColumnToggleCount] = {};
};

} // namespace Farman
