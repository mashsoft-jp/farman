#include "BookmarkEditDialog.h"
#include "utils/Dialogs.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QDir>

namespace Farman {

BookmarkEditDialog::BookmarkEditDialog(const QString& initialName,
                                       const QString& initialPath,
                                       QWidget* parent)
  : QDialog(parent)
  , m_nameEdit(nullptr)
  , m_pathEdit(nullptr)
  , m_browseButton(nullptr)
  , m_okButton(nullptr) {
  setupUi(initialName, initialPath);
}

QString BookmarkEditDialog::name() const {
  return m_nameEdit ? m_nameEdit->text() : QString();
}

QString BookmarkEditDialog::path() const {
  return m_pathEdit ? m_pathEdit->text().trimmed() : QString();
}

void BookmarkEditDialog::setupUi(const QString& initialName,
                                 const QString& initialPath) {
  setWindowTitle(tr("Edit Bookmark"));
  setModal(true);
  resize(560, 0);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  QFormLayout* form = new QFormLayout();
  // macOS のデフォルト（FieldsStayAtSizeHint）だと入力欄が伸びないので、
  // ダイアログ幅いっぱいまで使う
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  // ラベルは altBuddyLabel で作る (視覚ヒント + setBuddy + macOS 用の明示
  // ショートカット)。macOS は '&' mnemonic が効かないので、setBuddy だけだと
  // ヒントは出るのに Alt+key で何も起きない。
  m_nameEdit = new QLineEdit(initialName, this);
  m_nameEdit->setFocusPolicy(Qt::StrongFocus);
  auto* nameLabel = altBuddyLabel(tr("Name:"), Qt::Key_N, m_nameEdit, this);
  form->addRow(nameLabel, m_nameEdit);

  // Path: 入力欄 + フォルダアイコンボタン
  QWidget* pathRow = new QWidget(this);
  QHBoxLayout* pathRowLayout = new QHBoxLayout(pathRow);
  pathRowLayout->setContentsMargins(0, 0, 0, 0);
  m_pathEdit = new QLineEdit(initialPath, this);
  m_pathEdit->setFocusPolicy(Qt::StrongFocus);
  m_pathEdit->setMinimumWidth(320);

  // QPushButton にアイコンだけ載せる。フラットにしないので Tab 時に
  // プラットフォームごとのフォーカスリングが自然に出る。ショートカットは付けない。
  m_browseButton = new QPushButton(this);
  m_browseButton->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
  m_browseButton->setToolTip(tr("Browse folder..."));
  m_browseButton->setFocusPolicy(Qt::StrongFocus);
  pathRowLayout->addWidget(m_pathEdit, 1);
  pathRowLayout->addWidget(m_browseButton);
  auto* pathLabel = altBuddyLabel(tr("Path:"), Qt::Key_P, m_pathEdit, this);
  form->addRow(pathLabel, pathRow);

  mainLayout->addLayout(form);

  auto* buttonBox = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  m_okButton     = buttonBox->button(QDialogButtonBox::Ok);
  auto* cancelBtn = buttonBox->button(QDialogButtonBox::Cancel);
  applyAltShortcut(m_okButton,  Qt::Key_O);
  applyAltShortcut(cancelBtn,   Qt::Key_X);
  m_okButton->setDefault(true);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttonBox);

  connect(m_browseButton, &QPushButton::clicked, this, &BookmarkEditDialog::onBrowse);
  connect(m_nameEdit, &QLineEdit::textChanged, this, &BookmarkEditDialog::updateOkButtonState);
  connect(m_pathEdit, &QLineEdit::textChanged, this, &BookmarkEditDialog::updateOkButtonState);

  // Tab: Name → Path → Browse → Cancel → OK
  setTabOrder(m_nameEdit,     m_pathEdit);
  setTabOrder(m_pathEdit,     m_browseButton);
  setTabOrder(m_browseButton, cancelBtn);
  setTabOrder(cancelBtn,      m_okButton);

  m_nameEdit->setFocus();
  m_nameEdit->selectAll();
  updateOkButtonState();
}

void BookmarkEditDialog::keyPressEvent(QKeyEvent* event) {
  if (event->modifiers() & Qt::AltModifier) {
    switch (event->key()) {
      case Qt::Key_N:
        m_nameEdit->setFocus();
        m_nameEdit->selectAll();
        event->accept();
        return;
      case Qt::Key_P:
        m_pathEdit->setFocus();
        m_pathEdit->selectAll();
        event->accept();
        return;
      default: break;
    }
  }
  QDialog::keyPressEvent(event);
}

void BookmarkEditDialog::onBrowse() {
  const QString start = m_pathEdit->text().trimmed().isEmpty()
                          ? QDir::homePath()
                          : m_pathEdit->text().trimmed();
  const QString selected = QFileDialog::getExistingDirectory(
    this, tr("Select Directory"), start,
    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (!selected.isEmpty()) {
    m_pathEdit->setText(selected);
  }
}

void BookmarkEditDialog::updateOkButtonState() {
  if (!m_okButton) return;
  // Path が空でなければ OK（Name は空でも可：一覧では path 表示される）
  m_okButton->setEnabled(!m_pathEdit->text().trimmed().isEmpty());
}

} // namespace Farman
