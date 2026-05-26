#include "CsvView.h"

#include "utils/EnterClickFilter.h"

#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QStringDecoder>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <uchardet.h>

namespace Farman {

namespace {

constexpr auto kHeaderStyle = ""; // 占位 (将来カスタムスタイル余地)

// 区切り候補。Delimiter::Auto は内部で文字に解決する。
QChar delimiterChar(CsvView::Delimiter d) {
  switch (d) {
    case CsvView::Delimiter::Comma:     return QChar(',');
    case CsvView::Delimiter::Tab:       return QChar('\t');
    case CsvView::Delimiter::Semicolon: return QChar(';');
    case CsvView::Delimiter::Auto:      return QChar(',');  // dummy
  }
  return QChar(',');
}

CsvView::Delimiter delimiterFromChar(QChar c) {
  if (c == QChar('\t')) return CsvView::Delimiter::Tab;
  if (c == QChar(';'))  return CsvView::Delimiter::Semicolon;
  return CsvView::Delimiter::Comma;
}

QString decodeBytes(const QByteArray& data, const QString& encoding) {
  QStringDecoder decoder(encoding.toUtf8().constData());
  if (decoder.isValid()) return decoder.decode(data);
  return QString::fromUtf8(data);
}

QString detectEncoding(const QByteArray& data) {
  uchardet_t det = uchardet_new();
  QString detected;
  if (det) {
    if (uchardet_handle_data(det, data.constData(),
                             static_cast<size_t>(data.size())) == 0) {
      uchardet_data_end(det);
      detected = QString::fromLatin1(uchardet_get_charset(det));
    }
    uchardet_delete(det);
  }
  return detected.isEmpty() ? QStringLiteral("UTF-8") : detected;
}

// 区切り自動判定: 先頭 ~16 行を quote 考慮しつつ scan して、最頻の候補を返す。
// 拡張子で .tsv なら最初から Tab を返す (ユーザーの明示意図と見なす)。
QChar detectDelimiter(const QString& text, const QString& fileSuffixLower) {
  if (fileSuffixLower == QStringLiteral("tsv")) {
    return QChar('\t');
  }
  const QChar candidates[] = { QChar(','), QChar('\t'), QChar(';') };
  int counts[3] = {0, 0, 0};

  int linesScanned = 0;
  bool inQuotes = false;
  for (int i = 0; i < text.size() && linesScanned < 16; ++i) {
    const QChar c = text.at(i);
    if (c == QChar('"')) {
      inQuotes = !inQuotes;
      continue;
    }
    if (!inQuotes) {
      if (c == QChar('\n')) {
        ++linesScanned;
        continue;
      }
      for (int k = 0; k < 3; ++k) {
        if (c == candidates[k]) ++counts[k];
      }
    }
  }
  int bestIdx = 0;
  for (int k = 1; k < 3; ++k) {
    if (counts[k] > counts[bestIdx]) bestIdx = k;
  }
  if (counts[bestIdx] == 0) return QChar(',');  // 何もマッチしなければ comma
  return candidates[bestIdx];
}

// RFC 4180 ベースの簡易 CSV パーサ。
//   - フィールドは delim で区切る
//   - フィールドが `"` で始まると quoted-field 扱い (改行を含められる)
//   - quoted-field 内の `""` は 1 つの `"` にデコード
//   - 行末は `\n` または `\r\n` (lone `\r` も一応許容)
QVector<QStringList> parseCsv(const QString& text, QChar delim) {
  QVector<QStringList> rows;
  QStringList currentRow;
  QString     currentField;
  bool inQuotes = false;

  auto flushField = [&]() {
    currentRow.append(currentField);
    currentField.clear();
  };
  auto flushRow = [&]() {
    flushField();
    rows.append(currentRow);
    currentRow.clear();
  };

  for (int i = 0; i < text.size(); ++i) {
    const QChar c = text.at(i);
    if (inQuotes) {
      if (c == QChar('"')) {
        // 次が `"` ならエスケープ
        if (i + 1 < text.size() && text.at(i + 1) == QChar('"')) {
          currentField.append(QChar('"'));
          ++i;
        } else {
          inQuotes = false;
        }
      } else {
        currentField.append(c);
      }
      continue;
    }
    // 非 quoted モード
    if (c == QChar('"') && currentField.isEmpty()) {
      // フィールド先頭の `"` だけ開きクオートと解釈
      inQuotes = true;
    } else if (c == delim) {
      flushField();
    } else if (c == QChar('\n')) {
      flushRow();
    } else if (c == QChar('\r')) {
      // \r\n / 単独 \r の両方を行末とみなす
      flushRow();
      if (i + 1 < text.size() && text.at(i + 1) == QChar('\n')) ++i;
    } else {
      currentField.append(c);
    }
  }
  // 末尾。空文字列 1 件だけの "空行" は捨てる (ファイル末尾の改行)。
  if (!currentField.isEmpty() || !currentRow.isEmpty()) {
    flushRow();
  }
  return rows;
}

} // namespace

// ===== CsvTableModel =====

CsvTableModel::CsvTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void CsvTableModel::setRows(QVector<QStringList> rows, bool firstRowAsHeader) {
  beginResetModel();
  m_rows = std::move(rows);
  m_firstRowAsHeader = firstRowAsHeader;
  m_columnCount = 0;
  for (const auto& r : m_rows) {
    if (r.size() > m_columnCount) m_columnCount = r.size();
  }
  endResetModel();
}

void CsvTableModel::clear() {
  beginResetModel();
  m_rows.clear();
  m_columnCount = 0;
  endResetModel();
}

void CsvTableModel::setFirstRowAsHeader(bool enabled) {
  if (m_firstRowAsHeader == enabled) return;
  beginResetModel();
  m_firstRowAsHeader = enabled;
  endResetModel();
}

int CsvTableModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  if (m_rows.isEmpty()) return 0;
  return m_firstRowAsHeader ? (m_rows.size() - 1) : m_rows.size();
}

int CsvTableModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return m_columnCount;
}

QVariant CsvTableModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) return {};
  if (role != Qt::DisplayRole && role != Qt::ToolTipRole && role != Qt::EditRole) {
    return {};
  }
  const int dataRowIdx = m_firstRowAsHeader ? (index.row() + 1) : index.row();
  if (dataRowIdx < 0 || dataRowIdx >= m_rows.size()) return {};
  const QStringList& row = m_rows.at(dataRowIdx);
  if (index.column() < 0 || index.column() >= row.size()) return {};
  return row.at(index.column());
}

QVariant CsvTableModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const {
  if (role != Qt::DisplayRole) return {};
  if (orientation == Qt::Horizontal) {
    if (m_firstRowAsHeader && !m_rows.isEmpty()) {
      const QStringList& head = m_rows.first();
      if (section >= 0 && section < head.size()) return head.at(section);
    }
    return QString::number(section + 1);  // A 列、B 列... の代わりに 1-based 番号
  }
  // 垂直ヘッダ (行番号)。ヘッダ行を抜くと表示用は 1-based なので +1。
  return QString::number(section + 1);
}

// ===== CsvView =====

CsvView::CsvView(QWidget* parent) : QWidget(parent) {
  setupUi();
}

CsvView::~CsvView() = default;

void CsvView::setupUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  m_toolbar = new QToolBar(this);
  m_toolbar->setMovable(false);
  m_toolbar->setFloatable(false);
  m_toolbar->setIconSize(QSize(20, 20));
  m_toolbar->setStyleSheet(toolbarStyleSheet());

  // ── エンコーディング ──
  m_toolbar->addWidget(new QLabel(tr("Encoding:"), m_toolbar));
  m_encodingCombo = new QComboBox(m_toolbar);
  m_encodingCombo->addItem(QStringLiteral("Auto"));
  m_encodingCombo->addItem(QStringLiteral("UTF-8"));
  m_encodingCombo->addItem(QStringLiteral("UTF-16LE"));
  m_encodingCombo->addItem(QStringLiteral("UTF-16BE"));
  m_encodingCombo->addItem(QStringLiteral("Shift_JIS"));
  m_encodingCombo->addItem(QStringLiteral("EUC-JP"));
  m_encodingCombo->addItem(QStringLiteral("ISO-8859-1"));
  m_encodingCombo->setFocusPolicy(Qt::StrongFocus);
  m_toolbar->addWidget(m_encodingCombo);

  m_toolbar->addSeparator();

  // ── 区切り文字 ──
  m_toolbar->addWidget(new QLabel(tr("Delimiter:"), m_toolbar));
  m_delimiterCombo = new QComboBox(m_toolbar);
  m_delimiterCombo->addItem(tr("Auto"),       int(Delimiter::Auto));
  m_delimiterCombo->addItem(tr("Comma (,)"),  int(Delimiter::Comma));
  m_delimiterCombo->addItem(tr("Tab (\\t)"),  int(Delimiter::Tab));
  m_delimiterCombo->addItem(tr("Semicolon (;)"), int(Delimiter::Semicolon));
  m_delimiterCombo->setFocusPolicy(Qt::StrongFocus);
  m_toolbar->addWidget(m_delimiterCombo);

  m_toolbar->addSeparator();

  // ── ヘッダ行扱い ──
  m_headerToggle = new QToolButton(m_toolbar);
  m_headerToggle->setText(tr("First row = header"));
  m_headerToggle->setCheckable(true);
  m_headerToggle->setChecked(true);
  m_headerToggle->setToolTip(tr("Treat the first row as column headers"));
  m_headerToggle->setFocusPolicy(Qt::StrongFocus);
  m_toolbar->addWidget(m_headerToggle);

  auto* clickFilter = new EnterClickFilter(this);
  clickFilter->installOnButtonsIn(m_toolbar);

  root->addWidget(m_toolbar);

  // ── 本体: QTableView ──
  m_model = new CsvTableModel(this);
  m_table = new QTableView(this);
  m_table->setModel(m_model);
  m_table->setAlternatingRowColors(true);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setShowGrid(true);
  m_table->setStyleSheet(inactiveSelectionStyleSheet());
  // Tab はツールバー / 反対方向にフォーカスを抜けさせる (= 既定の「Tab で次セル」
  // を無効化)。セル内移動は矢印キーで行う。
  m_table->setTabKeyNavigation(false);
  m_table->horizontalHeader()->setSectionsClickable(false);
  m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_table->verticalHeader()->setDefaultSectionSize(20);
  m_table->verticalHeader()->setMinimumSectionSize(16);
  m_table->setFocusPolicy(Qt::StrongFocus);
  root->addWidget(m_table, /*stretch*/ 1);

  setFocusProxy(m_table);

  connect(m_encodingCombo, &QComboBox::currentTextChanged,
          this,             &CsvView::onEncodingComboChanged);
  connect(m_delimiterCombo, qOverload<int>(&QComboBox::currentIndexChanged),
          this,              &CsvView::onDelimiterComboChanged);
  connect(m_headerToggle, &QToolButton::toggled,
          this,            &CsvView::onHeaderToggleChanged);

  Q_UNUSED(kHeaderStyle);
}

bool CsvView::loadFile(const QString& filePath) {
  PreparedLoad p = prepareLoad(filePath, m_userEncoding, m_userDelimiter);
  if (!p.ok) return false;
  applyPreparedLoad(p);
  return true;
}

CsvView::PreparedLoad CsvView::prepareLoad(const QString& filePath,
                                            const QString& userEncoding,
                                            Delimiter      delimiter,
                                            const std::atomic<bool>* cancelToken,
                                            qint64 maxBytes) {
  PreparedLoad r;
  r.filePath = filePath;

  auto cancelled = [&]() {
    return cancelToken && cancelToken->load(std::memory_order_acquire);
  };
  if (cancelled()) return r;

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) return r;
  r.totalSize = file.size();
  if (maxBytes > 0 && r.totalSize > maxBytes) {
    r.data       = file.read(maxBytes);
    r.loadedSize = r.data.size();
  } else {
    r.data       = file.readAll();
    r.loadedSize = r.data.size();
  }
  file.close();
  if (cancelled()) return r;

  // エンコーディング判定
  if (userEncoding.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0) {
    r.actualEncoding = detectEncoding(r.data);
  } else {
    r.actualEncoding = userEncoding;
  }
  if (cancelled()) return r;

  const QString text = decodeBytes(r.data, r.actualEncoding);
  if (cancelled()) return r;

  // 区切り判定
  if (delimiter == Delimiter::Auto) {
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    r.actualDelimiter = detectDelimiter(text, suffix);
  } else {
    r.actualDelimiter = delimiterChar(delimiter);
  }
  if (cancelled()) return r;

  r.rows = parseCsv(text, r.actualDelimiter);
  r.ok   = true;
  return r;
}

void CsvView::applyPreparedLoad(const PreparedLoad& r) {
  m_filePath        = r.filePath;
  m_data            = r.data;
  m_actualEncoding  = r.actualEncoding;
  m_actualDelimiter = r.actualDelimiter;
  m_totalSize       = r.totalSize;
  m_loadedSize      = r.loadedSize;

  m_model->setRows(r.rows, m_headerToggle && m_headerToggle->isChecked());

  // 自動判定の結果を UI に反映 (ユーザーが Auto を選んでいる場合に検出値を見せる)
  if (m_encodingCombo) {
    const QString currentText = m_encodingCombo->currentText();
    if (currentText.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0
        && !r.actualEncoding.isEmpty()) {
      m_encodingCombo->setToolTip(tr("Detected: %1").arg(r.actualEncoding));
    } else {
      m_encodingCombo->setToolTip(QString());
    }
  }
  if (m_delimiterCombo && m_userDelimiter == Delimiter::Auto) {
    const Delimiter detected = delimiterFromChar(r.actualDelimiter);
    QString label;
    switch (detected) {
      case Delimiter::Comma:     label = tr("Comma");     break;
      case Delimiter::Tab:       label = tr("Tab");       break;
      case Delimiter::Semicolon: label = tr("Semicolon"); break;
      default: break;
    }
    if (!label.isEmpty()) {
      m_delimiterCombo->setToolTip(tr("Detected: %1").arg(label));
    } else {
      m_delimiterCombo->setToolTip(QString());
    }
  }

  // 列幅をデータに応じて 1 回だけ自動調整 (デフォルトのままだと狭すぎる)
  m_table->resizeColumnsToContents();
  // ただし極端に広い列はクリップ
  for (int c = 0; c < m_model->columnCount(); ++c) {
    if (m_table->columnWidth(c) > 360) m_table->setColumnWidth(c, 360);
  }

  // 先頭行を「現在行 + 選択行」として初期化しておく。これがないと最初に
  // テーブルへフォーカスが移ったときカレントマークが見えず、↓ キーで初めて
  // 2 行目に飛んでから強調表示が出る、という挙動になる。
  // 非アクティブ時 (= ツールバーにフォーカスがあるとき) は
  // inactiveSelectionStyleSheet で薄いグレーに落ちる。
  if (m_model->rowCount() > 0) {
    m_table->setCurrentIndex(m_model->index(0, 0));
    m_table->selectRow(0);
  }
}

void CsvView::reloadFromBuffer() {
  if (m_data.isEmpty()) return;
  // bytes はそのまま、encoding / delimiter だけ作り直して再パース。
  const QString text = decodeBytes(m_data,
    (m_userEncoding.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0)
      ? m_actualEncoding : m_userEncoding);
  QChar delim;
  if (m_userDelimiter == Delimiter::Auto) {
    delim = detectDelimiter(text, QFileInfo(m_filePath).suffix().toLower());
  } else {
    delim = delimiterChar(m_userDelimiter);
  }
  m_actualDelimiter = delim;
  m_model->setRows(parseCsv(text, delim),
                    m_headerToggle && m_headerToggle->isChecked());
  m_table->resizeColumnsToContents();
  for (int c = 0; c < m_model->columnCount(); ++c) {
    if (m_table->columnWidth(c) > 360) m_table->setColumnWidth(c, 360);
  }
  if (m_model->rowCount() > 0) {
    m_table->setCurrentIndex(m_model->index(0, 0));
    m_table->selectRow(0);
  }
}

void CsvView::onEncodingComboChanged(const QString& encoding) {
  m_userEncoding = encoding.trimmed();
  if (m_userEncoding.isEmpty()) return;
  if (m_userEncoding.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0) {
    // Auto に戻したら検出し直し
    m_actualEncoding = detectEncoding(m_data);
  } else {
    m_actualEncoding = m_userEncoding;
  }
  reloadFromBuffer();
}

void CsvView::onDelimiterComboChanged(int idx) {
  if (!m_delimiterCombo) return;
  m_userDelimiter = static_cast<Delimiter>(
    m_delimiterCombo->itemData(idx).toInt());
  reloadFromBuffer();
}

void CsvView::onHeaderToggleChanged(bool useHeader) {
  if (m_model) m_model->setFirstRowAsHeader(useHeader);
}

void CsvView::clearContent() {
  m_filePath.clear();
  m_data.clear();
  m_totalSize = 0;
  m_loadedSize = 0;
  if (m_model) m_model->clear();
}

QString CsvView::statusInfo() const {
  if (m_filePath.isEmpty() || !m_model) return QString();
  const int rows = m_model->rowCount();   // ヘッダ抜きの実データ行数
  const int cols = m_model->columnCount();
  QString s = tr("CSV · %1 rows · %2 cols · %3 · %4")
                .arg(QLocale().toString(rows))
                .arg(cols)
                .arg(m_actualEncoding.isEmpty() ? QStringLiteral("?")
                                                 : m_actualEncoding)
                .arg(QLocale(QLocale::English).formattedDataSize(m_totalSize));
  if (m_totalSize > m_loadedSize) {
    s += QStringLiteral(" · [truncated]");
  }
  return s;
}

} // namespace Farman
