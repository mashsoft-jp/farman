#pragma once

#include "settings/Settings.h"
#include <QDialog>

class QComboBox;
class QCheckBox;
class QRadioButton;
class QLineEdit;
class QDialogButtonBox;

namespace Farman {

// 現在開いているディレクトリ用のソート・フィルタ編集ダイアログ。
// OK 時にシグナル経由で編集後の PaneSettings と保存フラグを返す。
// initial がペインのデフォルト (defaults) と違うときは、保存済みの上書きか
// 一時的な設定かと、違う項目を上部に表示する。
class SortFilterDialog : public QDialog {
  Q_OBJECT

public:
  SortFilterDialog(const QString& directoryPath,
                   const PaneSettings& initial,
                   const PaneSettings& defaults,
                   bool initiallySaved,
                   QWidget* parent = nullptr);
  ~SortFilterDialog() override = default;

  PaneSettings result() const { return m_result; }
  bool         saveForDirectory() const { return m_saveForDirectory; }

private slots:
  void onAccepted();

private:
  void setupUi(const QString& directoryPath,
               const PaneSettings& initial,
               const PaneSettings& defaults,
               bool initiallySaved);

  // Sort controls
  QComboBox* m_sortKeyCombo;
  QComboBox* m_sortOrderCombo;
  QComboBox* m_sortKey2ndCombo;
  QComboBox* m_sortDirsTypeCombo;
  QCheckBox* m_sortDotFirstCheck;
  QCheckBox* m_sortCaseSensitiveCheck;

  // Filter controls
  QCheckBox* m_showHiddenCheck;
  // 対象の種別。「のみ」が 2 つ並ぶチェックだと両方 ON にできてしまい
  // 意味が通らないので、検索ダイアログと同じ 3 択のラジオにしている。
  QRadioButton* m_targetBothRadio;   // 既定
  QRadioButton* m_targetFilesRadio;
  QRadioButton* m_targetDirsRadio;
  QLineEdit* m_nameFiltersEdit;

  // Save toggle
  QCheckBox* m_saveCheck;

  QDialogButtonBox* m_buttonBox;

  PaneSettings m_result;
  bool         m_saveForDirectory = false;
};

} // namespace Farman
