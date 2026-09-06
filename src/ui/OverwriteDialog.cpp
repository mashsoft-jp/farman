#include "OverwriteDialog.h"
#include "utils/Dialogs.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QButtonGroup>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTimer>
#include <QFileInfo>
#include <QKeyEvent>
#include <QKeySequence>

namespace Farman {

OverwriteDialog::OverwriteDialog(const QString& srcPath,
                                 const QString& dstPath,
                                 QWidget* parent)
  : QDialog(parent)
  , m_overwriteRadio(nullptr)
  , m_renameRadio(nullptr)
  , m_skipRadio(nullptr)
  , m_renameEdit(nullptr)
  , m_buttonBox(nullptr)
  , m_originalName(QFileInfo(dstPath).fileName()) {
  setupUi(srcPath, dstPath);
}

void OverwriteDialog::setupUi(const QString& srcPath, const QString& dstPath) {
  setWindowTitle(tr("File Exists"));
  resize(540, 0);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  QLabel* headline = new QLabel(
    tr("A file already exists at the destination."), this);
  headline->setStyleSheet("QLabel { font-weight: bold; }");
  mainLayout->addWidget(headline);

  QFormLayout* pathForm = new QFormLayout();
  pathForm->addRow(tr("Source:"),      new QLabel(srcPath, this));
  pathForm->addRow(tr("Destination:"), new QLabel(dstPath, this));
  mainLayout->addLayout(pathForm);

  // ── Action 選択 ──
  // ラジオは applyAltShortcut で作る (ヒント表示 + 明示ショートカット)。
  // '&' 由来の mnemonic は macOS では効かないため、setShortcut が要る。
  QGroupBox* actionGroup = new QGroupBox(tr("Action"), this);
  QVBoxLayout* actionLayout = new QVBoxLayout(actionGroup);

  m_overwriteRadio = new QRadioButton(tr("Overwrite the existing file"), this);
  m_renameRadio    = new QRadioButton(tr("Rename to:"), this);
  m_skipRadio      = new QRadioButton(tr("Skip this file"), this);
  applyAltShortcut(m_overwriteRadio, Qt::Key_O);
  applyAltShortcut(m_renameRadio,    Qt::Key_R);
  applyAltShortcut(m_skipRadio,      Qt::Key_S);

  actionLayout->addWidget(m_overwriteRadio);

  // Rename ラジオ + ファイル名入力欄を水平に並べる
  QWidget* renameRow = new QWidget(this);
  QHBoxLayout* renameRowLayout = new QHBoxLayout(renameRow);
  renameRowLayout->setContentsMargins(0, 0, 0, 0);
  renameRowLayout->addWidget(m_renameRadio);
  m_renameEdit = new QLineEdit(this);
  m_renameEdit->setText(m_originalName);
  m_renameEdit->setPlaceholderText(tr("New filename"));
  m_renameEdit->setToolTip(tr("Alt+N to focus this field"));
  renameRowLayout->addWidget(m_renameEdit, 1);
  actionLayout->addWidget(renameRow);

  actionLayout->addWidget(m_skipRadio);
  mainLayout->addWidget(actionGroup);

  // 初期選択: Rename（ユーザーが競合を解決する意図が強いケース）
  m_renameRadio->setChecked(true);

  QButtonGroup* group = new QButtonGroup(this);
  group->addButton(m_overwriteRadio);
  group->addButton(m_renameRadio);
  group->addButton(m_skipRadio);
  connect(group, &QButtonGroup::buttonClicked,
          this, [this](QAbstractButton*) { onActionChanged(); });

  connect(m_renameEdit, &QLineEdit::textChanged,
          this, &OverwriteDialog::onRenameTextChanged);
  // Rename 欄に入力している時点で「Rename を選んでいる」と推定してラジオを合わせる
  connect(m_renameEdit, &QLineEdit::textEdited,
          this, [this](const QString&) {
            m_renameRadio->setChecked(true);
            onActionChanged();
          });

  // 初期選択: ベース名を選択しつつカーソルを拡張子の手前に置く。
  // 拡張子はそのまま残しつつ、ユーザーがすぐにベース名だけタイプし直せるようにする。
  // 拡張子は「最後の '.' 以降」とみなす ("foo.tar.gz" なら "foo.tar" を選択)。
  // 先頭 '.' (dot-file) は拡張子としてではなくベース名の一部とみなす。
  // inputText(BeforeExtension) と同じ規則。
  // ディレクトリは名前中のドットに拡張子としての意味が無い ("photos.2024" など)
  // ので全体を選択する。
  const bool isDir = QFileInfo(dstPath).isDir();
  const int lastDot = m_originalName.lastIndexOf(QLatin1Char('.'));
  const int basenameLen =
    (!isDir && lastDot > 0) ? lastDot : m_originalName.length();
  QTimer::singleShot(0, m_renameEdit, [this, basenameLen]() {
    m_renameEdit->setFocus();
    // setSelection(start, length) はカーソルを選択末尾に置くので別途
    // setCursorPosition は不要。
    m_renameEdit->setSelection(0, basenameLen);
  });

  // ── OK / Cancel ──
  m_buttonBox = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  auto* okBtn     = m_buttonBox->button(QDialogButtonBox::Ok);
  auto* cancelBtn = m_buttonBox->button(QDialogButtonBox::Cancel);
  applyAltShortcut(okBtn,     Qt::Key_K);  // "OK" の Alt+O は Overwrite ラジオで使用中
  applyAltShortcut(cancelBtn, Qt::Key_X);
  okBtn->setDefault(true);
  connect(m_buttonBox, &QDialogButtonBox::accepted, this, &OverwriteDialog::onAccepted);
  connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(m_buttonBox);

  // Tab 順: Overwrite → Rename ラジオ → Rename 入力 → Skip → Cancel → OK
  m_overwriteRadio->setFocusPolicy(Qt::StrongFocus);
  m_renameRadio->setFocusPolicy(Qt::StrongFocus);
  m_skipRadio->setFocusPolicy(Qt::StrongFocus);
  m_renameEdit->setFocusPolicy(Qt::StrongFocus);
  setTabOrder(m_overwriteRadio, m_renameRadio);
  setTabOrder(m_renameRadio,    m_renameEdit);
  setTabOrder(m_renameEdit,     m_skipRadio);
  setTabOrder(m_skipRadio,      cancelBtn);
  setTabOrder(cancelBtn,        okBtn);

  onActionChanged();
  onRenameTextChanged(m_renameEdit->text());
}

void OverwriteDialog::keyPressEvent(QKeyEvent* event) {
  // Alt+O/R/S でラジオを切替、Alt+N で Rename 入力にフォーカス移動。
  if (event->modifiers() & Qt::AltModifier) {
    switch (event->key()) {
      case Qt::Key_O:
        m_overwriteRadio->setChecked(true);
        onActionChanged();
        event->accept();
        return;
      case Qt::Key_R:
        m_renameRadio->setChecked(true);
        onActionChanged();
        event->accept();
        return;
      case Qt::Key_S:
        m_skipRadio->setChecked(true);
        onActionChanged();
        event->accept();
        return;
      case Qt::Key_N:
        m_renameRadio->setChecked(true);
        onActionChanged();
        m_renameEdit->setFocus();
        m_renameEdit->selectAll();
        event->accept();
        return;
      default: break;
    }
  }
  QDialog::keyPressEvent(event);
}

void OverwriteDialog::onActionChanged() {
  m_renameEdit->setEnabled(m_renameRadio->isChecked());
  updateOkButtonState();
}

void OverwriteDialog::onRenameTextChanged(const QString& /*text*/) {
  updateOkButtonState();
}

void OverwriteDialog::updateOkButtonState() {
  if (!m_buttonBox) return;
  auto* okBtn = m_buttonBox->button(QDialogButtonBox::Ok);
  if (!okBtn) return;
  bool ok = true;
  if (m_renameRadio->isChecked()) {
    const QString trimmed = m_renameEdit->text().trimmed();
    // 空 or 元と同じ名前は不可
    ok = !trimmed.isEmpty() && trimmed != m_originalName;
  }
  okBtn->setEnabled(ok);
}

void OverwriteDialog::onAccepted() {
  if (m_overwriteRadio->isChecked()) {
    m_decision.action = OverwriteDecision::Action::Overwrite;
  } else if (m_renameRadio->isChecked()) {
    m_decision.action  = OverwriteDecision::Action::Rename;
    m_decision.newName = m_renameEdit->text().trimmed();
  } else {
    m_decision.action = OverwriteDecision::Action::Cancel;
  }
  accept();
}

} // namespace Farman
