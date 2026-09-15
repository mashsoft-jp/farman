#pragma once

#include "types.h"
#include <QDialog>

class QLineEdit;
class QRadioButton;
class QCheckBox;
class QDialogButtonBox;

namespace Farman {

// コピー／移動中に上書き競合が起きたとき（Ask モード）に都度表示する確認ダイアログ。
// ラジオボタンでアクションを選び、OK で確定する:
//   - Overwrite: そのまま dst を上書き
//   - Rename:    入力されたファイル名でリネーム
//   - Skip:      このエントリだけ飛ばして続行 (Cancel は操作全体を中止)
// 「以降の重複にも同じ選択を適用」チェックボックス:
//   - ON で OK すると decision().applyToAll が true になり、ワーカー側は
//     同じ操作の残りの競合でダイアログを出さずに同じ選択を使い回す
//     (Rename は自動リネームテンプレートで一意な名前を付ける)。
//   - その操作限りで、Settings には保存しない。
// ショートカット:
//   - Alt+W / Alt+R / Alt+S でラジオを切替、Alt+A でチェックボックス
//   - Alt+N で Rename 選択時のファイル名入力欄にフォーカス
class OverwriteDialog : public QDialog {
  Q_OBJECT

public:
  OverwriteDialog(const QString& srcPath,
                  const QString& dstPath,
                  QWidget* parent = nullptr);
  ~OverwriteDialog() override = default;

  OverwriteDecision decision() const { return m_decision; }

protected:
  void keyPressEvent(QKeyEvent* event) override;

private slots:
  void onRenameTextChanged(const QString& text);
  void onActionChanged();
  void onAccepted();

private:
  void setupUi(const QString& srcPath, const QString& dstPath);
  void updateOkButtonState();

  QRadioButton* m_overwriteRadio;
  QRadioButton* m_renameRadio;
  QRadioButton* m_skipRadio;
  QLineEdit*    m_renameEdit;
  QCheckBox*    m_applyToAllCheck;
  QDialogButtonBox* m_buttonBox;

  QString m_originalName;  // 衝突している dst のファイル名（比較用）
  OverwriteDecision m_decision;
};

} // namespace Farman
