#include "CreateArchiveDialog.h"
#include "core/ArchiveFormatCatalog.h"
#include "utils/Dialogs.h"
#include <QCheckBox>
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
  // 暗号化は zip のみ、かつ「暗号化する」にチェックがあるときだけ。
  if (format() != ArchiveCreateWorker::Format::Zip) return QString();
  if (!m_encryptCheck || !m_encryptCheck->isChecked()) return QString();
  return m_passwordEdit->text();
}

ArchiveCreateWorker::Encryption CreateArchiveDialog::encryption() const {
  // zip + パスワードありなら暗号化する。方式は設定 → アーカイブの zip 形式の
  // 「既定の暗号化」に従う (既定は AES-256)。旧式 ZipCrypto は脆弱なので、
  // このダイアログには方式の選択 UI は出さず設定側だけで選ばせる。
  if (passphrase().isEmpty()) {
    return ArchiveCreateWorker::Encryption::None;
  }
  const ResolvedArchiveFormat zip =
    ArchiveFormatCatalog::resolvedFormat(QStringLiteral("zip"));
  if (zip.encryption == QStringLiteral("zipcrypt")) {
    return ArchiveCreateWorker::Encryption::ZipCrypt;
  }
  return ArchiveCreateWorker::Encryption::Aes256;
}

int CreateArchiveDialog::compressionLevel() const {
  // 無圧縮の Tar は対象外。それ以外は combo の値 (-1 = 既定 / 0〜9)。
  if (format() == ArchiveCreateWorker::Format::Tar) return -1;
  return m_compressionCombo->currentData().toInt();
}

QString CreateArchiveDialog::catalogIdForFormat(ArchiveCreateWorker::Format fmt) const {
  // 作成できる 5 形式は ArchiveFormatCatalog の組み込み形式 ID と 1:1。
  switch (fmt) {
    case ArchiveCreateWorker::Format::Zip:    return QStringLiteral("zip");
    case ArchiveCreateWorker::Format::Tar:    return QStringLiteral("tar");
    case ArchiveCreateWorker::Format::TarGz:  return QStringLiteral("tar.gz");
    case ArchiveCreateWorker::Format::TarBz2: return QStringLiteral("tar.bz2");
    case ArchiveCreateWorker::Format::TarXz:  return QStringLiteral("tar.xz");
  }
  return QStringLiteral("zip");
}

void CreateArchiveDialog::applyFormatDefaults() {
  // 設定 → アーカイブで形式ごとに保存した「作成時の既定」を初期値にする。
  // ユーザーがこのダイアログで変えた値は、その回だけの上書きとして扱う
  // (設定側には書き戻さない)。
  const ResolvedArchiveFormat resolved =
    ArchiveFormatCatalog::resolvedFormat(catalogIdForFormat(format()));
  const int index = m_compressionCombo->findData(resolved.compressionLevel);
  m_compressionCombo->setCurrentIndex(index >= 0 ? index : 0);
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

  // 各行のラベルは altBuddyLabel で作る (視覚ヒント + setBuddy + macOS 用の
  // 明示ショートカット)。macOS は '&' mnemonic が効かないので、setBuddy
  // だけだとヒントは出るのに押しても何も起きない。

  // 出力先のパス + Browse。何に対する操作かが最初に分かるよう、他のダイアログの
  // 「パス:」見出しと同じく一番上に置く。
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
  auto* dirLabel = altBuddyLabel(tr("Path:"), Qt::Key_P, m_dirEdit, this);
  form->addRow(dirLabel, dirRow);

  // Format
  m_formatCombo = new QComboBox(this);
  m_formatCombo->addItem(QStringLiteral("zip"),     static_cast<int>(ArchiveCreateWorker::Format::Zip));
  m_formatCombo->addItem(QStringLiteral("tar"),     static_cast<int>(ArchiveCreateWorker::Format::Tar));
  m_formatCombo->addItem(QStringLiteral("tar.gz"),  static_cast<int>(ArchiveCreateWorker::Format::TarGz));
  m_formatCombo->addItem(QStringLiteral("tar.bz2"), static_cast<int>(ArchiveCreateWorker::Format::TarBz2));
  m_formatCombo->addItem(QStringLiteral("tar.xz"),  static_cast<int>(ArchiveCreateWorker::Format::TarXz));
  m_formatCombo->setFocusPolicy(Qt::StrongFocus);
  auto* formatLabel = altBuddyLabel(tr("Format:"), Qt::Key_F, m_formatCombo, this);
  form->addRow(formatLabel, m_formatCombo);

  // Output filename
  m_nameEdit = new QLineEdit(this);
  m_nameEdit->setFocusPolicy(Qt::StrongFocus);
  auto* nameLabel = altBuddyLabel(tr("File name:"), Qt::Key_N, m_nameEdit, this);
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
  form->addRow(altBuddyLabel(tr("Compression:"), Qt::Key_C, m_compressionCombo, this),
               m_compressionCombo);

  // 暗号化 (zip のみ)。以前は「パスワード欄が空なら暗号化しない」という暗黙の
  // 扱いだったが分かりにくいので、チェックで明示的に ON にし、ON のときだけ
  // パスワード / 確認欄を有効にする。方式は設定 → アーカイブの zip 形式の
  // 「既定の暗号化」に従う (既定は AES-256)。
  m_encryptCheck = new QCheckBox(tr("Encrypt (zip only)"), this);
  applyAltShortcut(m_encryptCheck, Qt::Key_E);
  connect(m_encryptCheck, &QCheckBox::toggled, this, [this](bool on) {
    updateEncryptionEnabled();
    // ON にしたらそのまま打ち始められるようにする。
    if (on && m_passwordEdit->isEnabled()) m_passwordEdit->setFocus();
  });
  form->addRow(QString(), m_encryptCheck);

  // パスワード / 確認は「暗号化する」に従属する入力欄なので、ショートカットは
  // 付けない (チェックを入れると自動でパスワード欄へフォーカスが移る)。
  m_passwordEdit = new QLineEdit(this);
  m_passwordEdit->setFocusPolicy(Qt::StrongFocus);
  m_passwordEdit->setEchoMode(QLineEdit::Password);
  form->addRow(new QLabel(tr("Password:"), this), m_passwordEdit);

  m_passwordConfirmEdit = new QLineEdit(this);
  m_passwordConfirmEdit->setFocusPolicy(Qt::StrongFocus);
  m_passwordConfirmEdit->setEchoMode(QLineEdit::Password);
  form->addRow(new QLabel(tr("Confirm:"), this), m_passwordConfirmEdit);

  mainLayout->addLayout(form);

  // ボタン
  QHBoxLayout* btnLayout = new QHBoxLayout();
  btnLayout->addStretch(1);
  auto* cancelBtn = new QPushButton(tr("Cancel"), this);
  auto* okBtn     = new QPushButton(tr("Create"), this);
  applyAltShortcut(cancelBtn, Qt::Key_X);
  applyAltShortcut(okBtn,     Qt::Key_O);
  okBtn->setDefault(true);
  btnLayout->addWidget(cancelBtn);
  btnLayout->addWidget(okBtn);
  mainLayout->addLayout(btnLayout);

  connect(cancelBtn,     &QPushButton::clicked, this, &QDialog::reject);
  connect(okBtn,         &QPushButton::clicked, this, &CreateArchiveDialog::tryAccept);
  connect(m_browseButton,&QPushButton::clicked, this, &CreateArchiveDialog::onBrowseDir);
  connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { onFormatChanged(); });

  // Tab: format → dir → browse → name → compression → password → confirm
  //       → Cancel → OK
  setTabOrder(m_formatCombo,        m_dirEdit);
  setTabOrder(m_dirEdit,            m_browseButton);
  setTabOrder(m_browseButton,       m_nameEdit);
  setTabOrder(m_nameEdit,           m_compressionCombo);
  setTabOrder(m_compressionCombo,   m_encryptCheck);
  setTabOrder(m_encryptCheck,       m_passwordEdit);
  setTabOrder(m_passwordEdit,       m_passwordConfirmEdit);
  setTabOrder(m_passwordConfirmEdit,cancelBtn);
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
  // - 暗号化 (パスワード/確認): zip のみ (暗号化方式は AES-256 固定で UI なし)
  const bool isZip       = (format() == ArchiveCreateWorker::Format::Zip);
  const bool compressible = (format() != ArchiveCreateWorker::Format::Tar);
  m_compressionCombo->setEnabled(compressible);
  Q_UNUSED(isZip);
  updateEncryptionEnabled();

  // 形式が変わったら、その形式の既定値を入れ直す。
  applyFormatDefaults();
}

void CreateArchiveDialog::tryAccept() {
  // 「暗号化する」が有効なら、パスワードの入力と一致を検証してから確定する。
  if (m_encryptCheck->isEnabled() && m_encryptCheck->isChecked()) {
    if (m_passwordEdit->text().isEmpty()) {
      // チェックを入れたのに空のまま = 暗号化されないまま作られてしまう。
      // 以前の「空なら暗号化しない」に戻さず、入力を促す。
      warn(this, tr("Create Archive"), tr("Enter a password to encrypt the archive."));
      m_passwordEdit->setFocus();
      return;
    }
    if (m_passwordEdit->text() != m_passwordConfirmEdit->text()) {
      warn(this, tr("Create Archive"), tr("Passwords do not match."));
      m_passwordConfirmEdit->setFocus();
      m_passwordConfirmEdit->selectAll();
      return;
    }
  }
  accept();
}

void CreateArchiveDialog::updateEncryptionEnabled() {
  // 暗号化できるのは zip だけ。チェック自体も zip 以外では選べなくする。
  const bool isZip = (format() == ArchiveCreateWorker::Format::Zip);
  m_encryptCheck->setEnabled(isZip);
  const bool on = isZip && m_encryptCheck->isChecked();
  m_passwordEdit->setEnabled(on);
  m_passwordConfirmEdit->setEnabled(on);
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

} // namespace Farman
