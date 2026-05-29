#include "CreateArchiveDialog.h"
#include "utils/Dialogs.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QStyle>
#include <QKeyEvent>
#include <QKeySequence>

namespace Farman {

CreateArchiveDialog::CreateArchiveDialog(const QStringList& inputPaths,
                                         const QString&     defaultOutputDir,
                                         const QString&     sourcePaneDir,
                                         QWidget*           parent)
  : QDialog(parent)
  , m_destPaneDir(defaultOutputDir)
  , m_sourcePaneDir(sourcePaneDir)
  , m_inputPaths(inputPaths)
  , m_formatCombo(nullptr)
  , m_dirEdit(nullptr)
  , m_browseButton(nullptr)
  , m_nameEdit(nullptr) {
  setupUi(defaultOutputDir);
}

QString CreateArchiveDialog::outputPath() const {
  return QDir(m_dirEdit->text().trimmed()).absoluteFilePath(m_nameEdit->text().trimmed());
}

ArchiveCreateWorker::Format CreateArchiveDialog::format() const {
  return static_cast<ArchiveCreateWorker::Format>(m_formatCombo->currentData().toInt());
}

QString CreateArchiveDialog::passphrase() const {
  // 暗号化は zip のみ。パスワード空なら暗号化しない。
  if (format() != ArchiveCreateWorker::Format::Zip) return QString();
  return m_passwordEdit->text();
}

ArchiveCreateWorker::Encryption CreateArchiveDialog::encryption() const {
  if (format() != ArchiveCreateWorker::Format::Zip
      || m_passwordEdit->text().isEmpty()) {
    return ArchiveCreateWorker::Encryption::None;
  }
  return static_cast<ArchiveCreateWorker::Encryption>(
    m_encryptionCombo->currentData().toInt());
}

int CreateArchiveDialog::compressionLevel() const {
  // 無圧縮の Tar は対象外。それ以外は combo の値 (-1 = 既定 / 0〜9)。
  if (format() == ArchiveCreateWorker::Format::Tar) return -1;
  return m_compressionCombo->currentData().toInt();
}

QString CreateArchiveDialog::baseName() const {
  // 単一／複数選択にかかわらず、先頭エントリ名を起点にする。
  // ファイルなら最後の拡張子を取り除く（`foo.txt` → `foo`）、ディレクトリは
  // そのまま（`photos.2024` などドット付きでも保持する）。
  if (m_inputPaths.isEmpty()) return QStringLiteral("archive");
  const QFileInfo fi(m_inputPaths.first());
  const QString name = fi.isDir() ? fi.fileName() : fi.completeBaseName();
  return name.isEmpty() ? QStringLiteral("archive") : name;
}

QString CreateArchiveDialog::extensionForFormat(ArchiveCreateWorker::Format fmt) const {
  switch (fmt) {
    case ArchiveCreateWorker::Format::Zip:    return QStringLiteral(".zip");
    case ArchiveCreateWorker::Format::Tar:    return QStringLiteral(".tar");
    case ArchiveCreateWorker::Format::TarGz:  return QStringLiteral(".tar.gz");
    case ArchiveCreateWorker::Format::TarBz2: return QStringLiteral(".tar.bz2");
    case ArchiveCreateWorker::Format::TarXz:  return QStringLiteral(".tar.xz");
  }
  return QStringLiteral(".zip");
}

void CreateArchiveDialog::setupUi(const QString& defaultOutputDir) {
  setWindowTitle(tr("Create Archive"));
  setModal(true);
  resize(560, 0);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  QFormLayout* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  // Format
  // ラベルに Alt+key の視覚ヒントを埋め (withAltMnemonic 経由) + setBuddy で
  // Alt+key 押下時に対応フィールドへフォーカスを送る。
  m_formatCombo = new QComboBox(this);
  m_formatCombo->addItem(QStringLiteral("zip"),     static_cast<int>(ArchiveCreateWorker::Format::Zip));
  m_formatCombo->addItem(QStringLiteral("tar"),     static_cast<int>(ArchiveCreateWorker::Format::Tar));
  m_formatCombo->addItem(QStringLiteral("tar.gz"),  static_cast<int>(ArchiveCreateWorker::Format::TarGz));
  m_formatCombo->addItem(QStringLiteral("tar.bz2"), static_cast<int>(ArchiveCreateWorker::Format::TarBz2));
  m_formatCombo->addItem(QStringLiteral("tar.xz"),  static_cast<int>(ArchiveCreateWorker::Format::TarXz));
  m_formatCombo->setFocusPolicy(Qt::StrongFocus);
  auto* formatLabel = new QLabel(withAltMnemonic(tr("Format:"), Qt::Key_F), this);
  formatLabel->setBuddy(m_formatCombo);
  form->addRow(formatLabel, m_formatCombo);

  // Output directory + Browse
  QWidget* dirRow = new QWidget(this);
  QHBoxLayout* dirRowLayout = new QHBoxLayout(dirRow);
  dirRowLayout->setContentsMargins(0, 0, 0, 0);
  m_dirEdit = new QLineEdit(defaultOutputDir, this);
  m_dirEdit->setFocusPolicy(Qt::StrongFocus);
  // Copy/Move ダイアログと同様に ReadOnly にし、↑/↓ で「相手ペイン」⇔
  // 「自分ペイン」をトグル。値変更はトグル or Browse のみ。
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
  auto* dirLabel = new QLabel(withAltMnemonic(tr("Directory:"), Qt::Key_D), this);
  dirLabel->setBuddy(m_dirEdit);
  form->addRow(dirLabel, dirRow);

  // Output filename
  m_nameEdit = new QLineEdit(this);
  m_nameEdit->setFocusPolicy(Qt::StrongFocus);
  auto* nameLabel = new QLabel(withAltMnemonic(tr("File name:"), Qt::Key_M), this);
  nameLabel->setBuddy(m_nameEdit);
  form->addRow(nameLabel, m_nameEdit);

  // Compression level (gz / bz2 / xz / zip。Tar は無圧縮なので無効化)
  m_compressionCombo = new QComboBox(this);
  m_compressionCombo->setFocusPolicy(Qt::StrongFocus);
  m_compressionCombo->addItem(tr("Default"), -1);
  for (int lv = 0; lv <= 9; ++lv) {
    QString label = QString::number(lv);
    if (lv == 0) label += tr(" (store/fastest)");
    else if (lv == 9) label += tr(" (best)");
    m_compressionCombo->addItem(label, lv);
  }
  form->addRow(new QLabel(tr("Compression:"), this), m_compressionCombo);

  // Password (zip 暗号化。zip 以外は無効化)
  m_passwordEdit = new QLineEdit(this);
  m_passwordEdit->setFocusPolicy(Qt::StrongFocus);
  m_passwordEdit->setEchoMode(QLineEdit::Password);
  m_passwordEdit->setPlaceholderText(tr("Leave empty for no encryption (zip only)"));
  form->addRow(new QLabel(tr("Password:"), this), m_passwordEdit);

  m_passwordConfirmEdit = new QLineEdit(this);
  m_passwordConfirmEdit->setFocusPolicy(Qt::StrongFocus);
  m_passwordConfirmEdit->setEchoMode(QLineEdit::Password);
  form->addRow(new QLabel(tr("Confirm:"), this), m_passwordConfirmEdit);

  // Encryption method (パスワードを入れた zip のときだけ有効)
  m_encryptionCombo = new QComboBox(this);
  m_encryptionCombo->setFocusPolicy(Qt::StrongFocus);
  m_encryptionCombo->addItem(tr("AES-256 (recommended)"),
    static_cast<int>(ArchiveCreateWorker::Encryption::Aes256));
  m_encryptionCombo->addItem(tr("ZipCrypto (legacy, weak)"),
    static_cast<int>(ArchiveCreateWorker::Encryption::ZipCrypt));
  form->addRow(new QLabel(tr("Encryption:"), this), m_encryptionCombo);

  mainLayout->addLayout(form);

  // ボタン
  QHBoxLayout* btnLayout = new QHBoxLayout();
  btnLayout->addStretch(1);
  auto* cancelBtn = new QPushButton(tr("Cancel"), this);
  auto* okBtn     = new QPushButton(tr("Create"), this);
  applyAltShortcut(cancelBtn, Qt::Key_X);
  applyAltShortcut(okBtn,     Qt::Key_C);
  okBtn->setDefault(true);
  btnLayout->addWidget(cancelBtn);
  btnLayout->addWidget(okBtn);
  mainLayout->addLayout(btnLayout);

  connect(cancelBtn,     &QPushButton::clicked, this, &QDialog::reject);
  connect(okBtn,         &QPushButton::clicked, this, &CreateArchiveDialog::tryAccept);
  connect(m_browseButton,&QPushButton::clicked, this, &CreateArchiveDialog::onBrowseDir);
  connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { onFormatChanged(); });
  // パスワード入力の有無で暗号化方式コンボの有効/無効が変わる
  connect(m_passwordEdit, &QLineEdit::textChanged,
          this, [this](const QString&) { updateEncryptionEnabled(); });

  // Tab: format → dir → browse → name → compression → password → confirm
  //       → encryption → Cancel → OK
  setTabOrder(m_formatCombo,        m_dirEdit);
  setTabOrder(m_dirEdit,            m_browseButton);
  setTabOrder(m_browseButton,       m_nameEdit);
  setTabOrder(m_nameEdit,           m_compressionCombo);
  setTabOrder(m_compressionCombo,   m_passwordEdit);
  setTabOrder(m_passwordEdit,       m_passwordConfirmEdit);
  setTabOrder(m_passwordConfirmEdit,m_encryptionCombo);
  setTabOrder(m_encryptionCombo,    cancelBtn);
  setTabOrder(cancelBtn,            okBtn);

  // 初期ファイル名 + 形式に応じたフィールドの有効/無効を反映。
  m_nameEdit->setText(baseName() + extensionForFormat(format()));
  onFormatChanged();   // 拡張子は一致するので名前は不変。enable 状態を初期化

  m_nameEdit->setFocus();
  // 拡張子の手前にカーソルを置く
  const int extStart = m_nameEdit->text().indexOf(QLatin1Char('.'));
  if (extStart >= 0) {
    m_nameEdit->setCursorPosition(extStart);
    m_nameEdit->setSelection(0, extStart);
  } else {
    m_nameEdit->selectAll();
  }
}

void CreateArchiveDialog::onBrowseDir() {
  const QString start = m_dirEdit->text().trimmed().isEmpty()
                          ? QDir::homePath()
                          : m_dirEdit->text().trimmed();
  const QString selected = QFileDialog::getExistingDirectory(
    this, tr("Select Output Directory"), start,
    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (!selected.isEmpty()) m_dirEdit->setText(selected);
}

void CreateArchiveDialog::onFormatChanged() {
  // 既存の拡張子を置き換える
  QString name = m_nameEdit->text();
  const QStringList knownExts = {".tar.gz", ".tar.bz2", ".tar.xz", ".tar", ".zip"};
  for (const QString& e : knownExts) {
    if (name.endsWith(e, Qt::CaseInsensitive)) {
      name.chop(e.size());
      break;
    }
  }
  m_nameEdit->setText(name + extensionForFormat(format()));

  // 形式に応じてオプション欄の有効/無効を切替える。
  // - 圧縮レベル: 無圧縮の Tar 以外で有効
  // - 暗号化 (パスワード/確認/方式): zip のみ
  const bool isZip       = (format() == ArchiveCreateWorker::Format::Zip);
  const bool compressible = (format() != ArchiveCreateWorker::Format::Tar);
  m_compressionCombo->setEnabled(compressible);
  m_passwordEdit->setEnabled(isZip);
  m_passwordConfirmEdit->setEnabled(isZip);
  updateEncryptionEnabled();
}

void CreateArchiveDialog::updateEncryptionEnabled() {
  // 暗号化方式は「zip + パスワード入力あり」のときだけ選べる。
  const bool active = (format() == ArchiveCreateWorker::Format::Zip)
                      && !m_passwordEdit->text().isEmpty();
  m_encryptionCombo->setEnabled(active);
}

void CreateArchiveDialog::tryAccept() {
  // zip 暗号化時はパスワード一致を検証してから確定する。
  if (format() == ArchiveCreateWorker::Format::Zip
      && !m_passwordEdit->text().isEmpty()) {
    if (m_passwordEdit->text() != m_passwordConfirmEdit->text()) {
      warn(this, tr("Create Archive"), tr("Passwords do not match."));
      m_passwordConfirmEdit->setFocus();
      m_passwordConfirmEdit->selectAll();
      return;
    }
  }
  accept();
}

bool CreateArchiveDialog::eventFilter(QObject* watched, QEvent* event) {
  // ↑/↓ で「相手ペイン (m_destPaneDir)」⇔「自分ペイン (m_sourcePaneDir)」を
  // トグル。TransferConfirmDialog と同じロジック。
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

void CreateArchiveDialog::keyPressEvent(QKeyEvent* event) {
  if (event->modifiers() & Qt::AltModifier) {
    switch (event->key()) {
      case Qt::Key_F:
        m_formatCombo->setFocus();
        m_formatCombo->showPopup();
        event->accept();
        return;
      case Qt::Key_D:
        m_dirEdit->setFocus();
        m_dirEdit->selectAll();
        event->accept();
        return;
      case Qt::Key_M:
        m_nameEdit->setFocus();
        m_nameEdit->selectAll();
        event->accept();
        return;
      default: break;
    }
  }
  QDialog::keyPressEvent(event);
}

} // namespace Farman
