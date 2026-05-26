#include "TransferConfirmDialog.h"
#include "settings/Settings.h"
#include "utils/Dialogs.h"
#include "utils/EnterClickFilter.h"
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace Farman {

TransferConfirmDialog::TransferConfirmDialog(Operation op,
                                             const QString&     sourceDir,
                                             const QStringList& itemPaths,
                                             const QString&     destDir,
                                             QWidget* parent)
  : QDialog(parent)
  , m_overwriteModeCombo(nullptr)
  , m_autoRenameEdit(nullptr)
  , m_buttonBox(nullptr) {
  setupUi(op, sourceDir, itemPaths, destDir);
}

void TransferConfirmDialog::setupUi(Operation op,
                                    const QString&     sourceDir,
                                    const QStringList& itemPaths,
                                    const QString&     destDir) {
  setWindowTitle(op == Copy ? tr("Confirm Copy") : tr("Confirm Move"));
  resize(560, 420);
  m_sourceDir       = sourceDir;
  m_originalDestDir = destDir;

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // ── Source / destination paths ───────────────────
  // Source は読取専用 QLabel、Destination は編集可能 QLineEdit + 参照ボタン。
  // QFormLayout は既定では右側のウィジェットが伸びないので、
  // ExpandingFieldsGrow を指定して横幅一杯まで広げる。
  QFormLayout* pathForm = new QFormLayout();
  pathForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
  pathForm->addRow(tr("Source:"), new QLabel(sourceDir, this));

  m_destEdit = new QLineEdit(destDir, this);
  m_destEdit->setToolTip(
    tr("Destination directory. Press ↑/↓ to toggle between the source "
       "and the opposite-pane directory. Click the folder button to browse."));
  m_destEdit->setFocusPolicy(Qt::StrongFocus);
  m_destEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  // 自由入力で誤ったパスにならないよう読取専用。値の変更は ↓/↑ または
  // フォルダ参照ダイアログ経由のみ。テキストの選択・コピーは可能。
  m_destEdit->setReadOnly(true);
  m_destEdit->installEventFilter(this);

  m_destBrowseButton = new QToolButton(this);
  m_destBrowseButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
  m_destBrowseButton->setToolTip(tr("Browse for destination directory..."));
  m_destBrowseButton->setFocusPolicy(Qt::StrongFocus);
  // 既定の focus rect は macOS スタイルだと弱いので、フォーカス時に
  // ハイライト色の枠を出してわかりやすくする。
  m_destBrowseButton->setStyleSheet(
    "QToolButton { padding: 2px; border: 1px solid transparent; }"
    "QToolButton:focus { border: 2px solid palette(highlight); }");
  // フォーカス中に Enter / Return を押されたら参照ダイアログを開く
  // (デフォルトでは Enter はダイアログの default button = OK へ流れる)。
  m_destBrowseButton->installEventFilter(this);
  connect(m_destBrowseButton, &QToolButton::clicked,
          this, &TransferConfirmDialog::onBrowseDestination);

  QWidget* destRow = new QWidget(this);
  QHBoxLayout* destRowLayout = new QHBoxLayout(destRow);
  destRowLayout->setContentsMargins(0, 0, 0, 0);
  destRowLayout->setSpacing(4);
  destRowLayout->addWidget(m_destEdit, /*stretch*/ 1);
  destRowLayout->addWidget(m_destBrowseButton);
  pathForm->addRow(tr("Destination:"), destRow);
  mainLayout->addLayout(pathForm);

  // Items list
  QGroupBox* itemsGroup = new QGroupBox(
    tr("Items (%1)").arg(itemPaths.size()), this);
  QVBoxLayout* itemsLayout = new QVBoxLayout(itemsGroup);
  QListWidget* list = new QListWidget(this);
  list->setStyleSheet(inactiveSelectionStyleSheet());
  for (const QString& path : itemPaths) {
    QFileInfo info(path);
    QString label = info.fileName();
    if (info.isDir()) label += "/";
    list->addItem(label);
  }
  itemsLayout->addWidget(list);
  mainLayout->addWidget(itemsGroup, 1);

  // Overwrite mode。ラベルに Alt+key の視覚ヒントを埋める (withAltMnemonic 経由)。
  // Windows / Linux は & mnemonic で該当文字をアンダーライン表示、
  // macOS は HIG に従い末尾 "(⌥X)" 形式。
  QFormLayout* overwriteForm = new QFormLayout();
  m_overwriteModeCombo = new QComboBox(this);
  m_overwriteModeCombo->addItem(tr("Ask"),            static_cast<int>(OverwriteMode::Ask));
  m_overwriteModeCombo->addItem(tr("Auto-overwrite"), static_cast<int>(OverwriteMode::AutoOverwrite));
  m_overwriteModeCombo->addItem(tr("Auto-rename"),    static_cast<int>(OverwriteMode::AutoRename));
  m_overwriteModeCombo->setToolTip(
    tr("How to handle files that already exist at the destination."));
  auto* overwriteLabel = new QLabel(
    withAltMnemonic(tr("On overwrite:"), Qt::Key_O), this);
  overwriteLabel->setBuddy(m_overwriteModeCombo);
  overwriteForm->addRow(overwriteLabel, m_overwriteModeCombo);

  // Auto-rename テンプレート: AutoRename モード時のみ有効
  m_autoRenameEdit = new QLineEdit(this);
  m_autoRenameEdit->setText(Settings::instance().autoRenameTemplate());
  m_autoRenameEdit->setToolTip(
    tr("Suffix appended to rename conflicting files. "
       "Use {n} as the counter placeholder (e.g., ' ({n})' → 'foo (1).txt')."));
  auto* renameSuffixLabel = new QLabel(
    withAltMnemonic(tr("Rename suffix:"), Qt::Key_S), this);
  renameSuffixLabel->setBuddy(m_autoRenameEdit);
  overwriteForm->addRow(renameSuffixLabel, m_autoRenameEdit);
  auto updateEditEnabled = [this]() {
    const auto mode = static_cast<OverwriteMode>(
      m_overwriteModeCombo->currentData().toInt());
    m_autoRenameEdit->setEnabled(mode == OverwriteMode::AutoRename);
  };
  connect(m_overwriteModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [updateEditEnabled](int) { updateEditEnabled(); });
  updateEditEnabled();

  mainLayout->addLayout(overwriteForm);

  // Buttons
  m_buttonBox = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  auto* okBtn     = m_buttonBox->button(QDialogButtonBox::Ok);
  auto* cancelBtn = m_buttonBox->button(QDialogButtonBox::Cancel);
  okBtn->setText(op == Copy ? tr("Copy") : tr("Move"));
  applyAltShortcut(okBtn,     op == Copy ? Qt::Key_C : Qt::Key_M);
  applyAltShortcut(cancelBtn, Qt::Key_X);
  okBtn->setDefault(true);
  connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(m_buttonBox);

  // Tab ナビゲーション:
  //   destEdit → folder button → items → combobox → rename suffix → Cancel → Copy/Move
  // 実行系ボタンを最後に置くことで Tab 連打による誤操作を防ぐ。
  list->setFocusPolicy(Qt::StrongFocus);
  m_overwriteModeCombo->setFocusPolicy(Qt::StrongFocus);
  m_autoRenameEdit->setFocusPolicy(Qt::StrongFocus);
  setTabOrder(m_destEdit,           m_destBrowseButton);
  setTabOrder(m_destBrowseButton,   list);
  setTabOrder(list,                 m_overwriteModeCombo);
  setTabOrder(m_overwriteModeCombo, m_autoRenameEdit);
  setTabOrder(m_autoRenameEdit,     cancelBtn);
  setTabOrder(cancelBtn,            okBtn);

  // 開いた直後はコピー先入力欄にフォーカス。テキストを全選択して
  // 入力しやすくするのと、↓ キーでコピー元を流し込む操作にすぐ移れる。
  m_destEdit->setFocus();
  m_destEdit->selectAll();
}

void TransferConfirmDialog::onBrowseDestination() {
  if (!m_destEdit) return;
  const QString start = m_destEdit->text().trimmed();
  const QString picked = QFileDialog::getExistingDirectory(
    this,
    tr("Choose destination directory"),
    start.isEmpty() ? QDir::homePath() : start);
  if (!picked.isEmpty()) {
    m_destEdit->setText(picked);
    m_destEdit->setFocus();
  }
}

bool TransferConfirmDialog::eventFilter(QObject* watched, QEvent* event) {
  // フォルダ参照ボタンにフォーカスがあるときの Enter / Return を奪い、
  // ダイアログの default button (OK) ではなく参照ダイアログを開かせる。
  if (watched == m_destBrowseButton
      && (event->type() == QEvent::KeyPress
          || event->type() == QEvent::ShortcutOverride)) {
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
      if (event->type() == QEvent::KeyPress) {
        m_destBrowseButton->click();
      }
      event->accept();
      return true;
    }
  }
  if (watched == m_destEdit
      && (event->type() == QEvent::KeyPress
          || event->type() == QEvent::ShortcutOverride)) {
    auto* ke = static_cast<QKeyEvent*>(event);
    // 修飾キーは Shift / Ctrl / Alt / Meta だけを見る (KeypadModifier は
    // OS から自動付加されることがあるので無視する)。
    const auto mods = ke->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier
                                          | Qt::AltModifier | Qt::MetaModifier);
    if (mods == Qt::NoModifier
        && (ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down)) {
      // ↑/↓ どちらでも「コピー元」⇔「反対側ペインのディレクトリ」を
      // トグルする。
      // 注意: ShortcutOverride と KeyPress は同じ押下に対して 2 回飛んで
      // くるため、トグル本体は KeyPress のときだけ実行する。
      // ShortcutOverride では accept のみ返してショートカット処理を抑止。
      if (event->type() == QEvent::KeyPress) {
        const QString cur = m_destEdit->text();
        m_destEdit->setText(cur == m_sourceDir ? m_originalDestDir
                                                : m_sourceDir);
        m_destEdit->selectAll();
      }
      event->accept();
      return true;
    }
  }
  return QDialog::eventFilter(watched, event);
}

void TransferConfirmDialog::keyPressEvent(QKeyEvent* event) {
  // Alt+ラベル文字 で対応フィールドにフォーカス移動（Windows 風）。
  // QLabel::setBuddy だけだと macOS で動かないことがあるため、明示的に処理する。
  if (event->modifiers() & Qt::AltModifier) {
    switch (event->key()) {
      case Qt::Key_O:
        if (m_overwriteModeCombo) {
          m_overwriteModeCombo->setFocus();
          m_overwriteModeCombo->showPopup();
        }
        event->accept();
        return;
      case Qt::Key_S:
        if (m_autoRenameEdit && m_autoRenameEdit->isEnabled()) {
          m_autoRenameEdit->setFocus();
          m_autoRenameEdit->selectAll();
        }
        event->accept();
        return;
      default:
        break;
    }
  }

  QDialog::keyPressEvent(event);
}

OverwriteMode TransferConfirmDialog::overwriteMode() const {
  return static_cast<OverwriteMode>(m_overwriteModeCombo->currentData().toInt());
}

QString TransferConfirmDialog::autoRenameTemplate() const {
  return m_autoRenameEdit->text();
}

QString TransferConfirmDialog::destinationDir() const {
  return m_destEdit ? m_destEdit->text().trimmed() : QString();
}

} // namespace Farman
