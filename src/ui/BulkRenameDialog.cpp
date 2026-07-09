#include "BulkRenameDialog.h"
#include "utils/Dialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace Farman {

BulkRenameDialog::BulkRenameDialog(const QString& sourceDir,
                                   const QStringList& names,
                                   QWidget* parent)
  : QDialog(parent)
  , m_sourceDir(sourceDir)
  , m_originalNames(names) {
  setWindowTitle(tr("Bulk Rename"));
  resize(800, 560);
  setupUi();
  refreshPreview();
}

void BulkRenameDialog::setupUi() {
  QVBoxLayout* outer = new QVBoxLayout(this);

  // ── Inputs ──────────────────────
  QGroupBox* inputGroup = new QGroupBox(tr("Rules"), this);
  QFormLayout* form = new QFormLayout(inputGroup);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  m_templateEdit = new QLineEdit(this);
  m_templateEdit->setText(QStringLiteral("{name}.{ext}"));
  m_templateEdit->setToolTip(
    tr("Template for the new name. Placeholders:\n"
       "  {name}  original base name (no extension)\n"
       "  {ext}   original extension (without dot)\n"
       "  {n}     counter (no padding)\n"
       "  {n2} {n3} {n4} {n5}  zero-padded counter"));

  // テンプレートで使えるプレースホルダの一覧をダイアログ上に常時表示する。
  // ツールチップだけだと存在に気付きづらい。テンプレート入力欄と縦に密着
  // させたいので、両者を VBox に詰めて 1 行ぶんとして addRow する。
  auto* placeholderHelp = new QLabel(
    tr("Placeholders:  "
       "<code>{name}</code> original base name  ·  "
       "<code>{ext}</code> extension  ·  "
       "<code>{n}</code> counter (default pad)  ·  "
       "<code>{n2}</code> <code>{n3}</code> <code>{n4}</code> <code>{n5}</code> "
       "zero-padded counter"),
    this);
  placeholderHelp->setTextFormat(Qt::RichText);
  placeholderHelp->setWordWrap(true);
  placeholderHelp->setStyleSheet("QLabel { color: palette(mid); font-size: 11px; }");

  QWidget* templateCell = new QWidget(this);
  QVBoxLayout* templateCellLayout = new QVBoxLayout(templateCell);
  templateCellLayout->setContentsMargins(0, 0, 0, 0);
  templateCellLayout->setSpacing(2);
  templateCellLayout->addWidget(m_templateEdit);
  templateCellLayout->addWidget(placeholderHelp);
  form->addRow(tr("Template:"), templateCell);

  // Find / Replace + regex
  QWidget* findRow = new QWidget(this);
  QHBoxLayout* findLayout = new QHBoxLayout(findRow);
  findLayout->setContentsMargins(0, 0, 0, 0);
  m_findEdit    = new QLineEdit(this);
  m_replaceEdit = new QLineEdit(this);
  m_regexCheck  = new QCheckBox(tr("Regex"), this);
  m_findEdit->setPlaceholderText(tr("Find"));
  m_replaceEdit->setPlaceholderText(tr("Replace"));
  m_findEdit->setToolTip(
    tr("Substring or regex to find in the new name. Empty = no replacement."));
  m_replaceEdit->setToolTip(
    tr("Replacement text. With Regex on, use \\1 etc. for capture groups."));
  findLayout->addWidget(m_findEdit, 1);
  findLayout->addWidget(new QLabel(QStringLiteral("→"), this));
  findLayout->addWidget(m_replaceEdit, 1);
  findLayout->addWidget(m_regexCheck);
  form->addRow(tr("Find/Replace:"), findRow);

  // Number row
  QWidget* numRow = new QWidget(this);
  QHBoxLayout* numLayout = new QHBoxLayout(numRow);
  numLayout->setContentsMargins(0, 0, 0, 0);
  m_numStartSpin = new QSpinBox(this);
  m_numStartSpin->setRange(0, 999999);
  m_numStartSpin->setValue(1);
  m_numStepSpin  = new QSpinBox(this);
  m_numStepSpin->setRange(1, 1000);
  m_numStepSpin->setValue(1);
  m_numPadSpin   = new QSpinBox(this);
  m_numPadSpin->setRange(1, 9);
  m_numPadSpin->setValue(3);
  m_numPadSpin->setToolTip(tr("Default zero-padding width when {n} is used "
                              "without an explicit width like {n3}."));
  numLayout->addWidget(new QLabel(tr("Start:"), this));
  numLayout->addWidget(m_numStartSpin);
  numLayout->addSpacing(8);
  numLayout->addWidget(new QLabel(tr("Step:"), this));
  numLayout->addWidget(m_numStepSpin);
  numLayout->addSpacing(8);
  numLayout->addWidget(new QLabel(tr("Default Pad:"), this));
  numLayout->addWidget(m_numPadSpin);
  numLayout->addStretch();
  form->addRow(tr("Number:"), numRow);

  // Case
  m_caseCombo = new QComboBox(this);
  m_caseCombo->addItem(tr("Keep"),     0);
  m_caseCombo->addItem(tr("lower"),    1);
  m_caseCombo->addItem(tr("UPPER"),    2);
  form->addRow(tr("Case:"), m_caseCombo);

  outer->addWidget(inputGroup);

  // ── Preview ────────────────────
  m_previewTable = new QTableWidget(0, 2, this);
  m_previewTable->setWordWrap(false);  // 折り返さず省略 (…) で 1 行表示
  m_previewTable->setHorizontalHeaderLabels({tr("Original"), tr("New")});
  m_previewTable->horizontalHeader()->setStretchLastSection(true);
  m_previewTable->horizontalHeader()->resizeSection(0, 320);
  m_previewTable->verticalHeader()->setVisible(false);
  m_previewTable->setSelectionMode(QAbstractItemView::NoSelection);
  m_previewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  outer->addWidget(m_previewTable, 1);

  m_warningLabel = new QLabel(this);
  m_warningLabel->setStyleSheet("QLabel { color: #c0392b; }");
  outer->addWidget(m_warningLabel);

  // ── Buttons ─────────────────────
  m_buttonBox = new QDialogButtonBox(
    QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
  m_buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Rename"));
  applyAltShortcut(m_buttonBox->button(QDialogButtonBox::Ok),     Qt::Key_R);
  applyAltShortcut(m_buttonBox->button(QDialogButtonBox::Cancel), Qt::Key_X);
  outer->addWidget(m_buttonBox);

  connect(m_buttonBox, &QDialogButtonBox::accepted, this, &BulkRenameDialog::onAccept);
  connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

  // 入力が変わるたびにプレビューを再構築
  auto onInputChanged = [this]() { refreshPreview(); };
  connect(m_templateEdit, &QLineEdit::textChanged, this, onInputChanged);
  connect(m_findEdit,     &QLineEdit::textChanged, this, onInputChanged);
  connect(m_replaceEdit,  &QLineEdit::textChanged, this, onInputChanged);
  connect(m_regexCheck,   &QCheckBox::toggled,     this, onInputChanged);
  connect(m_numStartSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, onInputChanged);
  connect(m_numStepSpin,  QOverload<int>::of(&QSpinBox::valueChanged), this, onInputChanged);
  connect(m_numPadSpin,   QOverload<int>::of(&QSpinBox::valueChanged), this, onInputChanged);
  connect(m_caseCombo,    QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, onInputChanged);
}

QString BulkRenameDialog::buildNewName(int index, QString* errorOut) const {
  if (errorOut) errorOut->clear();
  if (index < 0 || index >= m_originalNames.size()) return QString();

  const QString original = m_originalNames[index];
  const QFileInfo fi(original);
  const QString base = fi.completeBaseName();
  const QString ext  = fi.suffix();

  QString tmpl = m_templateEdit->text();
  if (tmpl.isEmpty()) tmpl = QStringLiteral("{name}.{ext}");

  // {n} {n2} {n3} ... の連番展開
  const int counter = m_numStartSpin->value() + index * m_numStepSpin->value();
  QString result = tmpl;

  // {nDIGIT} (e.g. {n3}) を先に処理
  static const QRegularExpression kPaddedNumRe(QStringLiteral("\\{n([1-9])\\}"));
  QRegularExpressionMatchIterator it = kPaddedNumRe.globalMatch(result);
  // 後ろから置換するためマッチを集める
  QList<QRegularExpressionMatch> matches;
  while (it.hasNext()) matches.append(it.next());
  for (int i = matches.size() - 1; i >= 0; --i) {
    const QRegularExpressionMatch& m = matches[i];
    const int width = m.captured(1).toInt();
    const QString num = QString::number(counter).rightJustified(width, QLatin1Char('0'));
    result.replace(m.capturedStart(), m.capturedLength(), num);
  }
  // {n} (デフォルトのゼロ埋め桁数を使う)
  {
    const int defaultPad = m_numPadSpin->value();
    const QString num = QString::number(counter).rightJustified(defaultPad, QLatin1Char('0'));
    result.replace(QStringLiteral("{n}"), num);
  }

  // {name} {ext}
  result.replace(QStringLiteral("{name}"), base);
  result.replace(QStringLiteral("{ext}"),  ext);

  // Find / Replace
  const QString findStr = m_findEdit->text();
  if (!findStr.isEmpty()) {
    if (m_regexCheck->isChecked()) {
      QRegularExpression re(findStr);
      if (!re.isValid()) {
        if (errorOut) *errorOut = tr("Invalid regex");
        return QString();
      }
      result.replace(re, m_replaceEdit->text());
    } else {
      result.replace(findStr, m_replaceEdit->text());
    }
  }

  // Case
  switch (m_caseCombo->currentData().toInt()) {
    case 1: result = result.toLower(); break;
    case 2: result = result.toUpper(); break;
    default: break;
  }

  // バリデーション: 空 / パスセパレータを含む
  if (result.isEmpty()) {
    if (errorOut) *errorOut = tr("Empty name");
    return QString();
  }
  if (result.contains(QLatin1Char('/')) || result.contains(QLatin1Char('\\'))) {
    if (errorOut) *errorOut = tr("Invalid character");
    return QString();
  }

  return result;
}

void BulkRenameDialog::refreshPreview() {
  m_previewTable->setRowCount(m_originalNames.size());

  // 1) 全エントリの新名を計算
  QStringList newNames;
  newNames.reserve(m_originalNames.size());
  QStringList errors;
  errors.reserve(m_originalNames.size());
  for (int i = 0; i < m_originalNames.size(); ++i) {
    QString err;
    newNames.append(buildNewName(i, &err));
    errors.append(err);
  }

  // 2) バッチ内の重複検出 + 既存ファイルとの衝突検出
  QSet<QString> dupSet;
  QSet<QString> seen;
  for (const QString& n : std::as_const(newNames)) {
    if (n.isEmpty()) continue;
    if (seen.contains(n)) dupSet.insert(n);
    seen.insert(n);
  }

  // 既存ファイル一覧 (リネーム対象自身は除外)
  // 注: entryList() の戻り値は一時 QStringList なので、必ず一度ローカルに
  // 受けてからイテレータを取る (begin() と end() が別の一時から来ると UB)
  QDir dir(m_sourceDir);
  const QStringList entries =
    dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
  const QSet<QString> existing(entries.begin(), entries.end());
  const QSet<QString> sources(m_originalNames.begin(), m_originalNames.end());

  int errorCount = 0;
  for (int i = 0; i < m_originalNames.size(); ++i) {
    const QString& orig = m_originalNames[i];
    const QString& newN = newNames[i];
    QString display = newN;
    QString tooltip;
    bool isError = false;

    if (newN.isEmpty()) {
      display = errors[i].isEmpty() ? tr("(error)") : QStringLiteral("(") + errors[i] + QStringLiteral(")");
      tooltip = errors[i];
      isError = true;
    } else if (newN == orig) {
      // 変更なし: 普通に表示。エラー扱いしない
    } else {
      if (dupSet.contains(newN)) {
        tooltip = tr("Duplicate within this batch");
        isError = true;
      } else if (existing.contains(newN) && !sources.contains(newN)) {
        tooltip = tr("File already exists in this directory");
        isError = true;
      }
    }

    auto* origItem = new QTableWidgetItem(orig);
    origItem->setFlags(origItem->flags() & ~Qt::ItemIsEditable);
    auto* newItem  = new QTableWidgetItem(display);
    newItem->setFlags(newItem->flags() & ~Qt::ItemIsEditable);
    if (isError) {
      newItem->setForeground(QBrush(QColor(0xc0, 0x39, 0x2b)));
      newItem->setToolTip(tooltip);
      ++errorCount;
    } else if (newN == orig) {
      newItem->setForeground(QBrush(QColor(Qt::darkGray)));
    }
    m_previewTable->setItem(i, 0, origItem);
    m_previewTable->setItem(i, 1, newItem);
  }

  // 警告メッセージ + Rename ボタンの有効/無効
  if (errorCount > 0) {
    m_warningLabel->setText(tr("%1 entries have errors. Fix them or adjust the rules to continue.")
                              .arg(errorCount));
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
  } else {
    m_warningLabel->clear();
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
  }
}

void BulkRenameDialog::onAccept() {
  m_renames.clear();
  for (int i = 0; i < m_originalNames.size(); ++i) {
    const QString newN = buildNewName(i);
    if (newN.isEmpty()) continue;
    if (newN == m_originalNames[i]) continue;
    m_renames.append({m_originalNames[i], newN});
  }
  accept();
}

} // namespace Farman
