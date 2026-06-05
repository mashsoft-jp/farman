#pragma once

#include "core/DirectoryCompare.h"
#include <QDialog>
#include <QString>

class QLabel;
class QRadioButton;
class QCheckBox;
class QPushButton;

namespace Farman {

// ディレクトリ比較の起動ダイアログ。
// 左右ペインのカレントディレクトリと、粒度・再帰のオプションをユーザーに
// 提示して、OK で比較を開始する。
class DirectoryCompareDialog : public QDialog {
  Q_OBJECT

public:
  DirectoryCompareDialog(const QString& leftPath,
                         const QString& rightPath,
                         QWidget*       parent = nullptr);

  CompareOptions options() const;

private:
  void setupUi(const QString& leftPath, const QString& rightPath);

  QRadioButton* m_radioNameOnly  = nullptr;
  QRadioButton* m_radioSizeMtime = nullptr;
  QCheckBox*    m_recursiveCheck = nullptr;
};

} // namespace Farman
