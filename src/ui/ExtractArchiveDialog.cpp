#include "ExtractArchiveDialog.h"
#include "utils/Dialogs.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QStyle>
#include <QKeyEvent>
#include <QKeySequence>

namespace Farman {

ExtractArchiveDialog::ExtractArchiveDialog(const QString& archivePath,
                                           const QString& defaultOutputDir,
                                           const QString& sourcePaneDir,
                                           QWidget*       parent)
  : QDialog(parent)
  , m_destPaneDir(defaultOutputDir)
  , m_sourcePaneDir(sourcePaneDir)
  , m_dirEdit(nullptr)
  , m_browseButton(nullptr) {
  setupUi(archivePath, defaultOutputDir);
}

QString ExtractArchiveDialog::outputDirectory() const {
  return m_dirEdit->text().trimmed();
}

void ExtractArchiveDialog::setupUi(const QString& archivePath,
                                   const QString& defaultOutputDir) {
  setWindowTitle(tr("Extract Archive"));
  setModal(true);
  resize(560, 0);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  QFormLayout* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  form->addRow(tr("Archive:"), new QLabel(archivePath, this));

  QWidget* dirRow = new QWidget(this);
  QHBoxLayout* dirRowLayout = new QHBoxLayout(dirRow);
  dirRowLayout->setContentsMargins(0, 0, 0, 0);
  m_dirEdit = new QLineEdit(defaultOutputDir, this);
  m_dirEdit->setFocusPolicy(Qt::StrongFocus);
  // Copy/Move ダイアログ (TransferConfirmDialog) と同様に ReadOnly にし、
  // ↑/↓ で「相手ペイン」⇔「自分ペイン」をトグル。フリーテキスト入力で
  // 誤ったパスを作らないようにする (値変更はトグル or Browse のみ)。
  m_dirEdit->setReadOnly(true);
  m_dirEdit->setToolTip(
    tr("Output directory. Press ↑/↓ to toggle between the source pane "
       "and the opposite-pane directory. Click the folder button to browse."));
  m_dirEdit->installEventFilter(this);
  m_browseButton = new QPushButton(this);
  m_browseButton->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
  m_browseButton->setToolTip(tr("Browse folder..."));
  m_browseButton->setFocusPolicy(Qt::StrongFocus);
  dirRowLayout->addWidget(m_dirEdit, 1);
  dirRowLayout->addWidget(m_browseButton);
  // ラベルは altBuddyLabel で作る (視覚ヒント + setBuddy + macOS 用の
  // 明示ショートカット)。旧: withAltMnemonic + setBuddy で
  // Alt+D 押下時に出力先入力欄へフォーカスを送る。
  auto* dirLabel = altBuddyLabel(tr("Output directory:"), Qt::Key_D,
                                 m_dirEdit, this);
  form->addRow(dirLabel, dirRow);

  mainLayout->addLayout(form);

  QHBoxLayout* btnLayout = new QHBoxLayout();
  btnLayout->addStretch(1);
  auto* cancelBtn = new QPushButton(tr("Cancel"), this);
  auto* okBtn     = new QPushButton(tr("Extract"), this);
  applyAltShortcut(cancelBtn, Qt::Key_X);
  applyAltShortcut(okBtn,     Qt::Key_E);
  okBtn->setDefault(true);
  btnLayout->addWidget(cancelBtn);
  btnLayout->addWidget(okBtn);
  mainLayout->addLayout(btnLayout);

  connect(cancelBtn,      &QPushButton::clicked, this, &QDialog::reject);
  connect(okBtn,          &QPushButton::clicked, this, &QDialog::accept);
  connect(m_browseButton, &QPushButton::clicked, this, &ExtractArchiveDialog::onBrowseDir);

  setTabOrder(m_dirEdit,      m_browseButton);
  setTabOrder(m_browseButton, cancelBtn);
  setTabOrder(cancelBtn,      okBtn);

  m_dirEdit->setFocus();
  m_dirEdit->selectAll();
}

void ExtractArchiveDialog::onBrowseDir() {
  const QString start = m_dirEdit->text().trimmed().isEmpty()
                          ? QDir::homePath()
                          : m_dirEdit->text().trimmed();
  const QString selected = QFileDialog::getExistingDirectory(
    this, tr("Select Output Directory"), start,
    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (!selected.isEmpty()) m_dirEdit->setText(selected);
}

bool ExtractArchiveDialog::eventFilter(QObject* watched, QEvent* event) {
  // ↑/↓ で「相手ペイン (m_destPaneDir)」⇔「自分ペイン (m_sourcePaneDir)」を
  // トグル。TransferConfirmDialog と同じロジック。
  // 注意: ShortcutOverride と KeyPress は同じ押下に対して 2 回飛んでくるため、
  // トグル本体は KeyPress のときだけ実行する。
  if (watched == m_dirEdit
      && (event->type() == QEvent::KeyPress
          || event->type() == QEvent::ShortcutOverride)) {
    auto* ke = static_cast<QKeyEvent*>(event);
    const auto mods = ke->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier
                                          | Qt::AltModifier | Qt::MetaModifier);
    if (mods == Qt::NoModifier
        && (ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down)) {
      if (event->type() == QEvent::KeyPress) {
        const QString cur = m_dirEdit->text();
        m_dirEdit->setText(cur == m_sourcePaneDir ? m_destPaneDir
                                                  : m_sourcePaneDir);
        m_dirEdit->selectAll();
      }
      event->accept();
      return true;
    }
  }
  return QDialog::eventFilter(watched, event);
}

void ExtractArchiveDialog::keyPressEvent(QKeyEvent* event) {
  if (event->modifiers() & Qt::AltModifier) {
    if (event->key() == Qt::Key_D) {
      m_dirEdit->setFocus();
      m_dirEdit->selectAll();
      event->accept();
      return;
    }
  }
  QDialog::keyPressEvent(event);
}

} // namespace Farman
