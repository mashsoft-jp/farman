#include "DeleteConfirmDialog.h"
#include "utils/Dialogs.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFontMetrics>
#include <QLabel>
#include <QRadioButton>
#include <QButtonGroup>
#include <QGroupBox>
#include <QPushButton>
#include <QKeyEvent>
#include <QKeySequence>

namespace Farman {

DeleteConfirmDialog::DeleteConfirmDialog(const QString& message,
                                         bool defaultToTrash,
                                         QWidget* parent,
                                         const QString& detailTooltip,
                                         bool trashAvailable)
  : QDialog(parent)
  , m_trashRadio(nullptr)
  , m_permanentRadio(nullptr) {
  setupUi(message, defaultToTrash, detailTooltip, trashAvailable);
}

bool DeleteConfirmDialog::toTrash() const {
  return m_trashRadio ? m_trashRadio->isChecked() : true;
}

void DeleteConfirmDialog::setupUi(const QString& message, bool defaultToTrash,
                                  const QString& detailTooltip, bool trashAvailable) {
  setWindowTitle(tr("Confirm Delete"));
  setModal(true);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  auto* label = new QLabel(message, this);
  label->setWordWrap(true);
  // ダイアログ幅はメッセージの長さに応じて可変にする。1 行で表示したときの幅を測り、
  // 下限/上限 (280〜760) でクランプしてラベル幅を固定する。SetMinimumSize 制約下では
  // レイアウト (= ラベル) の幅がダイアログ幅を駆動するため、短い名前は狭く、長い名前は
  // 上限まで広がり、超過分だけ word wrap で折り返す。
  {
    const QFontMetrics fm(label->fontMetrics());
    const int textWidth = fm.horizontalAdvance(message);
    const int width = qBound(280, textWidth + 24, 760);
    label->setMinimumWidth(width);
    label->setMaximumWidth(width);
  }
  // 中央省略した長いファイル名の全文をホバーで確認できるようにする。
  if (!detailTooltip.isEmpty()) {
    label->setToolTip(detailTooltip);
  }
  mainLayout->addWidget(label);

  // ── Action 選択 ──
  // ラジオラベルに Alt+key の視覚ヒントを埋める (withAltMnemonic 経由)。
  QGroupBox* actionGroup = new QGroupBox(tr("Action"), this);
  QVBoxLayout* actionLayout = new QVBoxLayout(actionGroup);
  m_trashRadio     = new QRadioButton(
    withAltMnemonic(tr("Move to Trash"), Qt::Key_T), this);
  m_permanentRadio = new QRadioButton(
    withAltMnemonic(tr("Delete permanently"), Qt::Key_P), this);
  m_trashRadio->setFocusPolicy(Qt::StrongFocus);
  m_permanentRadio->setFocusPolicy(Qt::StrongFocus);
  if (trashAvailable) {
    (defaultToTrash ? m_trashRadio : m_permanentRadio)->setChecked(true);
  } else {
    // ネットワーク / リムーバブルドライブではゴミ箱 (moveToTrash) が使えないため、
    // ゴミ箱を無効化して完全削除を選択させる。
    m_trashRadio->setEnabled(false);
    m_permanentRadio->setChecked(true);
  }
  actionLayout->addWidget(m_trashRadio);
  actionLayout->addWidget(m_permanentRadio);
  if (!trashAvailable) {
    auto* trashNote = new QLabel(
      tr("Trash is unavailable on this drive (e.g. a network or removable "
         "drive); files will be deleted permanently."), this);
    trashNote->setWordWrap(true);
    actionLayout->addWidget(trashNote);
  }
  mainLayout->addWidget(actionGroup);

  QButtonGroup* group = new QButtonGroup(this);
  group->addButton(m_trashRadio);
  group->addButton(m_permanentRadio);

  // ── OK / Cancel ──
  QHBoxLayout* btnLayout = new QHBoxLayout();
  btnLayout->addStretch(1);
  auto* cancelBtn = new QPushButton(tr("Cancel"), this);
  auto* okBtn     = new QPushButton(tr("OK"),     this);
  applyAltShortcut(cancelBtn, Qt::Key_X);
  applyAltShortcut(okBtn,     Qt::Key_O);
  okBtn->setDefault(true);
  btnLayout->addWidget(cancelBtn);
  btnLayout->addWidget(okBtn);
  mainLayout->addLayout(btnLayout);

  connect(okBtn,     &QPushButton::clicked, this, &QDialog::accept);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

  // Tab 順: Trash → Permanent → Cancel → OK
  setTabOrder(m_trashRadio,     m_permanentRadio);
  setTabOrder(m_permanentRadio, cancelBtn);
  setTabOrder(cancelBtn,        okBtn);

  // 長いファイル名でメッセージが折り返しても各要素が潰れないように、ダイアログを
  // レイアウトの最小サイズで開く。幅はラベル側 (上で設定) が駆動し、QLabel の
  // 折り返し高さ計算 (height-for-width) を正しく効かせて高さ不足を防ぐ。
  mainLayout->setSizeConstraint(QLayout::SetMinimumSize);
}

void DeleteConfirmDialog::keyPressEvent(QKeyEvent* event) {
  if (event->modifiers() & Qt::AltModifier) {
    switch (event->key()) {
      case Qt::Key_T:
        m_trashRadio->setChecked(true);
        event->accept();
        return;
      case Qt::Key_P:
        m_permanentRadio->setChecked(true);
        event->accept();
        return;
      default: break;
    }
  }
  QDialog::keyPressEvent(event);
}

} // namespace Farman
