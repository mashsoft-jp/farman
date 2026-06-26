#pragma once

#include <QDialog>
#include <QString>

class QRadioButton;

namespace Farman {

// 削除確認ダイアログ。
// - 上部にメッセージ（"Delete 'foo'?" など）
// - ラジオボタン 2 つ: Move to Trash (⌥T) / Delete permanently (⌥P)
//   初期選択は呼び出し側が渡す defaultToTrash に従う。
// - Cancel (⌥X) / OK (⌥O)
class DeleteConfirmDialog : public QDialog {
  Q_OBJECT

public:
  DeleteConfirmDialog(const QString& message,
                      bool defaultToTrash,
                      QWidget* parent = nullptr,
                      const QString& detailTooltip = QString(),
                      bool trashAvailable = true);
  ~DeleteConfirmDialog() override = default;

  bool toTrash() const;

protected:
  void keyPressEvent(QKeyEvent* event) override;

private:
  void setupUi(const QString& message, bool defaultToTrash,
               const QString& detailTooltip, bool trashAvailable);

  QRadioButton* m_trashRadio;
  QRadioButton* m_permanentRadio;
};

} // namespace Farman
