#include "ArchiveTab.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace Farman {

namespace {

// 詳細ダイアログの「暗号化方式」コンボの値。Settings に入る文字列と 1:1。
// 「暗号化しない」はここではなくアーカイブ作成ダイアログのパスワード欄
// (空なら暗号化しない) で決まるので、選択肢は方式だけ。
struct EncryptionChoice {
  const char* label;
  const char* value;
};
const EncryptionChoice kEncryptionChoices[] = {
  {QT_TRANSLATE_NOOP("ArchiveTab", "AES-256 (recommended)"), "aes256"},
  {QT_TRANSLATE_NOOP("ArchiveTab", "ZipCrypto (legacy)"),    "zipcrypt"},
};

// 詳細ダイアログの「ファイル名の文字コード」コンボの値。
// 空文字は自動判別 (UTF-8 として妥当なら UTF-8、でなければ Shift_JIS)。
struct EncodingChoice {
  const char* label;
  const char* value;
};
const EncodingChoice kEncodingChoices[] = {
  {QT_TRANSLATE_NOOP("ArchiveTab", "Auto-detect"), ""},
  {"UTF-8",      "UTF-8"},
  {"Shift_JIS",  "Shift_JIS"},
  {"EUC-JP",     "EUC-JP"},
  {"CP437",      "IBM437"},
  {"ISO-8859-1", "ISO-8859-1"},
};

}  // namespace

ArchiveTab::ArchiveTab(QWidget* parent)
  : QWidget(parent) {
  setupUi();
  loadSettings();
}

void ArchiveTab::setupUi() {
  auto* mainLayout = new QVBoxLayout(this);

  // ─── 対応形式の一覧 ───
  auto* formatGroup = new QGroupBox(tr("Archive Formats"), this);
  auto* formatLayout = new QVBoxLayout(formatGroup);

  auto* hint = new QLabel(
    tr("Formats that are turned on are recognized by their file name and can "
       "be browsed like a folder. Use \"Details...\" to change the file "
       "patterns and the defaults used when creating an archive. Turning a "
       "plugin format on or off takes effect after restarting farman."),
    formatGroup);
  hint->setWordWrap(true);
  formatLayout->addWidget(hint);

  // 一覧は最低限の列 (有効 / 形式 / 由来 / 拡張子) に絞り、作成時の既定値など
  // は各行右端の「詳細...」ボタンで開くダイアログで見せる。PluginsTab の
  // 一覧と同じ作法 (Enter で詳細・Space でトグル、行移動は ↑/↓)。
  m_formatTable = new QTableWidget(formatGroup);
  m_formatTable->setWordWrap(false);
  m_formatTable->setColumnCount(6);
  m_formatTable->setHorizontalHeaderLabels({
    tr("Enabled"),
    tr("Status"),
    tr("Format"),
    tr("Origin"),
    tr("File Patterns"),
    QString()
  });
  m_formatTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_formatTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_formatTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_formatTable->verticalHeader()->setVisible(false);
  m_formatTable->setTabKeyNavigation(false);
  m_formatTable->installEventFilter(this);
  connect(m_formatTable, &QTableWidget::itemDoubleClicked, this,
          [this](QTableWidgetItem* item) {
    if (item) showFormatDetails(item->row());
  });

  // 一覧のチェックボックス操作を編集状態へ反映する。
  connect(m_formatTable, &QTableWidget::itemChanged, this,
          [this](QTableWidgetItem* item) {
    if (m_populating || !item || item->column() != 0) return;
    setFormatEnabled(item->row(), item->checkState() == Qt::Checked);
  });
  formatLayout->addWidget(m_formatTable, 1);

  mainLayout->addWidget(formatGroup, 1);

  // ─── 共通設定 ───
  auto* commonGroup = new QGroupBox(tr("Common"), this);
  auto* commonForm = new QFormLayout(commonGroup);
  commonForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  // 一時展開先。アーカイブ内のファイルをビュアーで開くときや、アーカイブ内
  // アーカイブに潜るときの書き出し先。
  auto* tempRow = new QWidget(commonGroup);
  auto* tempRowLayout = new QHBoxLayout(tempRow);
  tempRowLayout->setContentsMargins(0, 0, 0, 0);

  m_tempDirectoryEdit = new QLineEdit(commonGroup);
  m_tempDirectoryEdit->setPlaceholderText(
    QStandardPaths::writableLocation(QStandardPaths::TempLocation));
  m_tempDirectoryEdit->setToolTip(
    tr("Where archive contents are extracted temporarily (for viewing files "
       "inside an archive, or opening an archive within an archive). "
       "Leave empty to use the system temporary directory."));
  m_tempDirectoryBrowse = new QToolButton(commonGroup);
  m_tempDirectoryBrowse->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
  m_tempDirectoryBrowse->setToolTip(tr("Choose temporary directory..."));
  m_tempDirectoryDefault = new QToolButton(commonGroup);
  m_tempDirectoryDefault->setText(tr("Default"));
  m_tempDirectoryDefault->setToolTip(tr("Use the system temporary directory."));

  tempRowLayout->addWidget(m_tempDirectoryEdit, 1);
  tempRowLayout->addWidget(m_tempDirectoryBrowse);
  tempRowLayout->addWidget(m_tempDirectoryDefault);
  commonForm->addRow(tr("Temporary directory:"), tempRow);

  connect(m_tempDirectoryBrowse, &QToolButton::clicked, this, [this]() {
    QString start = m_tempDirectoryEdit->text().trimmed();
    if (start.isEmpty()) {
      start = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    // 開始ディレクトリが無いとダイアログが別の場所へフォールバックするので
    // 先に作っておく (PluginsTab のプラグインディレクトリ選択と同じ扱い)。
    if (!QDir(start).exists()) QDir().mkpath(start);
    const QString selected = QFileDialog::getExistingDirectory(
      this, tr("Choose temporary directory"), start,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!selected.isEmpty()) m_tempDirectoryEdit->setText(selected);
  });
  connect(m_tempDirectoryDefault, &QToolButton::clicked,
          m_tempDirectoryEdit, &QLineEdit::clear);

  m_passwordRetrySpin = new QSpinBox(commonGroup);
  m_passwordRetrySpin->setRange(1, 99);
  m_passwordRetrySpin->setToolTip(
    tr("How many times the password prompt is shown again after a wrong "
       "password before giving up."));
  commonForm->addRow(tr("Password attempts:"), m_passwordRetrySpin);

  m_maxNestDepthSpin = new QSpinBox(commonGroup);
  m_maxNestDepthSpin->setRange(0, 99);
  m_maxNestDepthSpin->setSpecialValueText(tr("Unlimited"));
  m_maxNestDepthSpin->setToolTip(
    tr("How many levels of archive-inside-archive can be opened. "
       "0 means unlimited."));
  commonForm->addRow(tr("Maximum nesting depth:"), m_maxNestDepthSpin);

  mainLayout->addWidget(commonGroup);
}

bool ArchiveTab::eventFilter(QObject* watched, QEvent* event) {
  if (watched != m_formatTable) return QWidget::eventFilter(watched, event);

  // フォーカスを得たとき行未選択なら先頭行を選び、すぐ ↑/↓ で動かせるようにする。
  if (event->type() == QEvent::FocusIn) {
    if (m_formatTable->currentRow() < 0 && m_formatTable->rowCount() > 0) {
      m_formatTable->selectRow(0);
    }
    return QWidget::eventFilter(watched, event);
  }

  if (event->type() == QEvent::KeyPress) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const int key = keyEvent->key();
    const bool backtab =
      key == Qt::Key_Backtab
      || (key == Qt::Key_Tab && (keyEvent->modifiers() & Qt::ShiftModifier));
    const bool tab =
      !backtab && key == Qt::Key_Tab && keyEvent->modifiers() == Qt::NoModifier;

    // Enter で詳細ダイアログ、Space で有効 / 無効のトグル。
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      showFormatDetails(m_formatTable->currentRow());
      return true;
    }
    if (key == Qt::Key_Space) {
      const int row = m_formatTable->currentRow();
      if (auto* item = (row >= 0) ? m_formatTable->item(row, 0) : nullptr) {
        setFormatEnabled(row, item->checkState() != Qt::Checked);
      }
      return true;
    }
    // 行移動は ↑/↓ のみ。←/→ での現在セル移動はさせない
    if (key == Qt::Key_Left || key == Qt::Key_Right) {
      return true;
    }
    if (tab) {
      // 「詳細...」ボタンには止まらず、下の共通設定へ抜ける
      m_tempDirectoryEdit->setFocus(Qt::TabFocusReason);
      return true;
    }
    if (backtab) {
      return false;  // 既定のフォーカスチェーンに任せる
    }
  }
  return QWidget::eventFilter(watched, event);
}

void ArchiveTab::loadSettings() {
  const Settings& settings = Settings::instance();

  m_formats.clear();
  for (const ResolvedArchiveFormat& r : ArchiveFormatCatalog::resolvedFormats()) {
    FormatState state;
    state.resolved         = r;
    state.enabled          = r.enabled;
    state.patterns         = r.patterns;
    state.compressionLevel = r.compressionLevel;
    state.encryption       = r.encryption;
    state.filenameEncoding = r.filenameEncoding;
    m_formats.append(state);
  }
  loadFormatList();

  m_tempDirectoryEdit->setText(settings.archiveTempDirectory());
  m_passwordRetrySpin->setValue(settings.archivePasswordRetryCount());
  m_maxNestDepthSpin->setValue(settings.archiveMaxNestDepth());
}

QString ArchiveTab::patternsDisplayText(const QStringList& patterns) const {
  return patterns.join(QStringLiteral(", "));
}

QStringList ArchiveTab::parsePatterns(const QString& text) const {
  QStringList patterns;
  // カンマ / 空白 / セミコロン区切りのどれでも受け付ける。
  const QStringList raw = text.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                     Qt::SkipEmptyParts);
  for (const QString& r : raw) {
    const QString p = r.trimmed();
    if (!p.isEmpty() && !patterns.contains(p, Qt::CaseInsensitive)) {
      patterns.append(p);
    }
  }
  return patterns;
}

// 一覧のチェックボックス / 詳細ダイアログのどちらから変更しても、同じ編集
// 状態 (FormatState::enabled) と同じ表示 (一覧のチェック) を更新する。
void ArchiveTab::setFormatEnabled(int row, bool enabled) {
  if (row < 0 || row >= m_formats.size()) return;
  m_formats[row].enabled = enabled;

  if (auto* item = m_formatTable->item(row, 0)) {
    const Qt::CheckState checkState = enabled ? Qt::Checked : Qt::Unchecked;
    if (item->checkState() != checkState) {
      const bool wasPopulating = m_populating;
      m_populating = true;   // 表示合わせの setCheckState を再入させない
      item->setCheckState(checkState);
      m_populating = wasPopulating;
    }
  }
}

// プラグイン形式のロード状態。ViewerTab の一覧と同じ絵文字 / 文言に揃える。
QString ArchiveTab::pluginStatusText(const ArchivePluginRecord& record) const {
  if (record.loaded) return tr("Loaded");
  if (record.blockedExternalDisabled) return tr("Blocked (external plugins off)");
  return record.disabledByUser ? tr("Disabled") : tr("Failed");
}

QString ArchiveTab::pluginStatusEmoji(const ArchivePluginRecord& record) const {
  if (record.loaded) return QStringLiteral("✅");
  if (record.blockedExternalDisabled) return QStringLiteral("🔒");
  return record.disabledByUser ? QStringLiteral("🚫") : QStringLiteral("❌");
}

QString ArchiveTab::originText(const ArchiveFormatInfo& info) const {
  return info.source == ArchiveFormatInfo::Source::Plugin ? tr("Plugin")
                                                          : tr("Built-in");
}

void ArchiveTab::loadFormatList() {
  // 行を作り直す間の itemChanged はユーザー操作ではないので無視する。
  m_populating = true;
  m_formatTable->setRowCount(m_formats.size());

  for (int row = 0; row < m_formats.size(); ++row) {
    const FormatState& state = m_formats[row];
    const ArchiveFormatInfo& info = state.resolved.info;

    // 有効 / 無効はこの一覧で直接切り替えられる (詳細ダイアログでも変更可)。
    auto* enabledItem = new QTableWidgetItem();
    enabledItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                          | Qt::ItemIsUserCheckable);
    enabledItem->setCheckState(state.enabled ? Qt::Checked : Qt::Unchecked);
    enabledItem->setToolTip(
      info.source == ArchiveFormatInfo::Source::Plugin
        ? tr("Click to enable/disable. Takes effect after restarting farman.")
        : tr("Click to enable/disable. When off, files matching the patterns "
             "are treated as ordinary files."));
    m_formatTable->setItem(row, 0, enabledItem);

    // 状態列。組み込み形式は常に使えるので空欄、プラグイン形式だけ
    // ロード状況 (絵文字) を出す。文言はツールチップと詳細ダイアログで見せる。
    auto* statusItem = new QTableWidgetItem();
    statusItem->setTextAlignment(Qt::AlignCenter);
    if (info.source == ArchiveFormatInfo::Source::Plugin) {
      statusItem->setText(pluginStatusEmoji(info.pluginRecord));
      statusItem->setToolTip(pluginStatusText(info.pluginRecord));
    } else {
      statusItem->setText(QStringLiteral("-"));
      statusItem->setToolTip(tr("Built-in formats are always available."));
    }
    m_formatTable->setItem(row, 1, statusItem);

    auto* nameItem = new QTableWidgetItem(info.displayName);
    QStringList notes;
    if (!info.canCreate)          notes << tr("read-only");
    if (info.singleFileCompression) notes << tr("single file");
    nameItem->setToolTip(notes.isEmpty()
                           ? info.displayName
                           : tr("%1 (%2)").arg(info.displayName,
                                               notes.join(QStringLiteral(", "))));
    m_formatTable->setItem(row, 2, nameItem);

    auto* originItem = new QTableWidgetItem(originText(info));
    originItem->setToolTip(originItem->text());
    m_formatTable->setItem(row, 3, originItem);

    auto* patternsItem = new QTableWidgetItem(patternsDisplayText(state.patterns));
    patternsItem->setToolTip(patternsItem->text());
    m_formatTable->setItem(row, 4, patternsItem);

    auto* detailsButton = new QPushButton(tr("Details..."), m_formatTable);
    detailsButton->setAutoDefault(false);
    detailsButton->setToolTip(tr("Show and change the settings for this format."));
    detailsButton->setFocusPolicy(Qt::NoFocus);
    connect(detailsButton, &QPushButton::clicked, this, [this, row]() {
      m_formatTable->selectRow(row);
      showFormatDetails(row);
    });
    m_formatTable->setCellWidget(row, 5, detailsButton);
  }

  m_populating = false;

  m_formatTable->resizeColumnsToContents();
  m_formatTable->resizeRowsToContents();
  m_formatTable->horizontalHeader()->setStretchLastSection(false);
  m_formatTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
  if (auto* button = m_formatTable->cellWidget(0, 5)) {
    m_formatTable->setColumnWidth(5, button->sizeHint().width() + 8);
  }
}

void ArchiveTab::showFormatDetails(int row) {
  if (row < 0 || row >= m_formats.size()) return;
  FormatState& state = m_formats[row];
  const ArchiveFormatInfo& info = state.resolved.info;

  QDialog dialog(this);
  dialog.setWindowTitle(tr("Archive Format Details"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  auto addField = [&dialog, form](const QString& label, const QString& value) {
    auto* v = new QLabel(value.isEmpty() ? QStringLiteral("-") : value, &dialog);
    v->setWordWrap(true);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(label, v);
  };

  auto* enabledCheck = new QCheckBox(&dialog);
  enabledCheck->setChecked(state.enabled);
  enabledCheck->setToolTip(
    info.source == ArchiveFormatInfo::Source::Plugin
      ? tr("Changes take effect after restarting farman.")
      : tr("When off, files matching the patterns below are treated as "
           "ordinary files."));
  form->addRow(tr("Enabled:"), enabledCheck);
  // 「有効」は設定値そのもので、実際にロードできたかは上の状態欄が示す。
  // 外部プラグインの読込みが OFF だと、有効にしていてもロードされない。
  if (info.source == ArchiveFormatInfo::Source::Plugin
      && info.pluginRecord.blockedExternalDisabled) {
    auto* blockedHint = new QLabel(
      tr("External plugin loading is off. Turn on \"Allow loading external "
         "plugins\" in Settings → General to load this plugin."), &dialog);
    blockedHint->setWordWrap(true);
    blockedHint->setEnabled(false);
    form->addRow(QString(), blockedHint);
  }

  addField(tr("Format:"), info.displayName);
  addField(tr("Origin:"), originText(info));

  // プラグイン形式は、かつて設定 → プラグインの Archive 一覧で見せていた
  // 診断情報 (ロード状況 / 版 / 制作者 / パス / エラー全文) をここに出す。
  if (info.source == ArchiveFormatInfo::Source::Plugin) {
    const ArchivePluginRecord& rec = info.pluginRecord;
    addField(tr("Status:"), pluginStatusEmoji(rec) + QStringLiteral(" ")
                              + pluginStatusText(rec));
    addField(tr("Priority:"),
             rec.priority >= 0 ? QString::number(rec.priority) : QString());
    addField(tr("Distribution:"), rec.origin == ArchivePluginRecord::Origin::Bundled
                                    ? tr("Bundled") : tr("External"));
    addField(tr("Plugin ID:"), rec.pluginId);
    addField(tr("Version:"), rec.version);
    addField(tr("Author:"), rec.author);
    if (!rec.authorUrl.isEmpty()) {
      auto* urlLabel = new QLabel(
        QStringLiteral("<a href=\"%1\">%1</a>").arg(rec.authorUrl.toHtmlEscaped()),
        &dialog);
      urlLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
      urlLabel->setOpenExternalLinks(true);
      urlLabel->setWordWrap(true);
      form->addRow(tr("Author URL:"), urlLabel);
    }
    addField(tr("Path:"), rec.filePath);
    if (!rec.errorReason.isEmpty() && !rec.disabledByUser) {
      addField(tr("Error:"), rec.errorReason);
    }
  }

  // 対応拡張子。ファイル名全体に対する glob なので "*.tar.gz" のような
  // 複合拡張子も書ける。
  auto* patternsRow = new QWidget(&dialog);
  auto* patternsLayout = new QHBoxLayout(patternsRow);
  patternsLayout->setContentsMargins(0, 0, 0, 0);
  auto* patternsEdit = new QLineEdit(patternsDisplayText(state.patterns), &dialog);
  patternsEdit->setToolTip(
    tr("File patterns for this format, separated by commas. Wildcards "
       "(* and ?) can be used, e.g. \"*.tar.gz\"."));
  auto* patternsDefault = new QToolButton(&dialog);
  patternsDefault->setText(tr("Default"));
  patternsDefault->setToolTip(tr("Restore the default patterns for this format."));
  connect(patternsDefault, &QToolButton::clicked, patternsEdit,
          [patternsEdit, &info, this]() {
    patternsEdit->setText(patternsDisplayText(info.defaultPatterns));
  });
  patternsLayout->addWidget(patternsEdit, 1);
  patternsLayout->addWidget(patternsDefault);
  form->addRow(tr("File patterns:"), patternsRow);

  // ── 作成時の既定 ──
  // 読取専用の形式では作成系の項目自体を出さない (出しても効かないので)。
  QComboBox* compressionCombo = nullptr;
  QComboBox* encryptionCombo  = nullptr;
  if (info.canCreate && info.supportsCompressionLevel) {
    compressionCombo = new QComboBox(&dialog);
    compressionCombo->addItem(tr("Format default"), -1);
    for (int level = info.minCompressionLevel; level <= info.maxCompressionLevel; ++level) {
      compressionCombo->addItem(QString::number(level), level);
    }
    const int index = compressionCombo->findData(state.compressionLevel);
    compressionCombo->setCurrentIndex(index >= 0 ? index : 0);
    compressionCombo->setToolTip(
      tr("Initial compression level in the Create Archive dialog."));
    form->addRow(tr("Default compression level:"), compressionCombo);
  }
  if (info.canCreate && info.encryption == ArchiveFormatInfo::Encryption::ReadWrite) {
    encryptionCombo = new QComboBox(&dialog);
    for (const EncryptionChoice& choice : kEncryptionChoices) {
      // 文言は kEncryptionChoices で QT_TRANSLATE_NOOP("ArchiveTab", ...) と
      // マークしてあるので、翻訳の引き当ても同じ "ArchiveTab" コンテキストで
      // 行う (tr() だと "Farman::ArchiveTab" を見に行ってしまう)。
      encryptionCombo->addItem(
        QCoreApplication::translate("ArchiveTab", choice.label),
        QString::fromLatin1(choice.value));
    }
    const int index = encryptionCombo->findData(state.encryption);
    encryptionCombo->setCurrentIndex(index >= 0 ? index : 0);
    encryptionCombo->setToolTip(
      tr("Encryption method used when a password is entered in the Create "
         "Archive dialog. Leaving the password empty there creates an "
         "unencrypted archive."));
    form->addRow(tr("Encryption method:"), encryptionCombo);
  }

  QComboBox* encodingCombo = nullptr;
  if (info.supportsFilenameEncoding) {
    encodingCombo = new QComboBox(&dialog);
    for (const EncodingChoice& choice : kEncodingChoices) {
      encodingCombo->addItem(
        QCoreApplication::translate("ArchiveTab", choice.label),
        QString::fromLatin1(choice.value));
    }
    const int index = encodingCombo->findData(state.filenameEncoding);
    encodingCombo->setCurrentIndex(index >= 0 ? index : 0);
    encodingCombo->setToolTip(
      tr("Character encoding used for entry names. \"Auto-detect\" reads them "
         "as UTF-8 when valid and as Shift_JIS otherwise."));
    form->addRow(tr("File name encoding:"), encodingCombo);
  }

  if (!info.canCreate) {
    auto* readOnlyHint = new QLabel(
      tr("farman can browse and extract this format, but cannot create it, "
         "so there are no settings for creating archives."), &dialog);
    readOnlyHint->setWordWrap(true);
    readOnlyHint->setEnabled(false);
    form->addRow(QString(), readOnlyHint);
  }

  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                       &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog.resize(std::clamp(dialog.sizeHint().width(), 420, 560),
                dialog.sizeHint().height());
  if (dialog.exec() != QDialog::Accepted) return;

  setFormatEnabled(row, enabledCheck->isChecked());
  // 空にされた場合は「何も認識しない」ではなく既定に戻す (誤操作で形式が
  // まるごと消えるのを避ける)。明示的に無効化したいなら Enabled を外す。
  const QStringList patterns = parsePatterns(patternsEdit->text());
  state.patterns = patterns.isEmpty() ? info.defaultPatterns : patterns;
  if (compressionCombo) state.compressionLevel = compressionCombo->currentData().toInt();
  if (encryptionCombo)  state.encryption       = encryptionCombo->currentData().toString();
  if (encodingCombo)    state.filenameEncoding = encodingCombo->currentData().toString();

  if (auto* item = m_formatTable->item(row, 4)) {
    item->setText(patternsDisplayText(state.patterns));
    item->setToolTip(item->text());
  }
}

void ArchiveTab::save() {
  auto& settings = Settings::instance();
  m_restartRequiredOnSave = false;

  // 形式ごとの上書き。カタログ既定と一致する項目は書かず、既定の変更に
  // 追従させる (Settings::ArchiveFormatOverride のコメント参照)。
  QMap<QString, ArchiveFormatOverride> overrides = settings.archiveFormatOverrides();
  QStringList disabledPlugins = settings.disabledArchivePlugins();

  for (const FormatState& state : m_formats) {
    const ArchiveFormatInfo& info = state.resolved.info;

    if (info.source == ArchiveFormatInfo::Source::Plugin) {
      // プラグイン形式の有効 / 無効は disabledArchivePlugins に一本化する。
      // 一覧に出ている分だけを、大小違いの残留ごと入れ替える。
      disabledPlugins.removeIf([&info](const QString& id) {
        return id.trimmed().compare(info.pluginId, Qt::CaseInsensitive) == 0;
      });
      if (!state.enabled) disabledPlugins.append(info.pluginId);
    }

    ArchiveFormatOverride ov;
    if (info.source != ArchiveFormatInfo::Source::Plugin
        && state.enabled != info.defaultEnabled) {
      ov.enabled = state.enabled;
    }
    if (state.patterns != info.defaultPatterns) {
      ov.patterns = state.patterns;
    }
    if (state.compressionLevel != -1)      ov.compressionLevel = state.compressionLevel;
    if (state.encryption != info.defaultEncryption) ov.encryption = state.encryption;
    if (!state.filenameEncoding.isEmpty()) ov.filenameEncoding = state.filenameEncoding;

    if (ov.isEmpty()) overrides.remove(info.id);
    else              overrides.insert(info.id, ov);
  }
  settings.setArchiveFormatOverrides(overrides);

  disabledPlugins.sort(Qt::CaseInsensitive);
  disabledPlugins.removeDuplicates();
  if (disabledPlugins != settings.disabledArchivePlugins()) {
    settings.setDisabledArchivePlugins(disabledPlugins);
    // プラグインは起動時に一括ロードするため、反映は次回起動から。
    m_restartRequiredOnSave = true;
  }

  settings.setArchiveTempDirectory(m_tempDirectoryEdit->text());
  settings.setArchivePasswordRetryCount(m_passwordRetrySpin->value());
  settings.setArchiveMaxNestDepth(m_maxNestDepthSpin->value());

  // 組み込み形式の有効 / 無効と拡張子は再起動を待たずに反映させる。
  ArchiveFormatCatalog::applyToArchivePath();
}

} // namespace Farman
