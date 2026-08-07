#include "CsvView.h"

#include "settings/Settings.h"
#include "keybinding/ViewerKeyBindingManager.h"
#include "utils/EnterClickFilter.h"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QModelIndex>
#include <QSizePolicy>
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

// テキスト全体を 1 パスして、各行の開始 offset と最大列数を求める。
// quoted-field 内の改行 / 区切りはスキップして判定する (RFC 4180 互換)。
// 戻り値の rowStarts は最後に sentinel として text.size() を持つ。
// (= rowStarts.size() == 行数 + 1)
// cancelToken が non-null かつ途中で true になった場合は、それまで集めた
// 部分結果のまま early return する。呼び出し側はその後に再度 cancel チェック
// して partial を捨てるかどうかを決めること。
// 戻り値: 走り切れたら true、cancel 中断なら false。
bool buildRowIndex(const QString& text, QChar delim,
                    QVector<int>* outRowStarts,
                    int*          outColumnCount,
                    const std::atomic<bool>* cancelToken = nullptr) {
  outRowStarts->clear();
  *outColumnCount = 0;
  if (text.isEmpty()) return true;

  outRowStarts->append(0);
  int curCols = 1;   // 行内のフィールド数 (空行でも 1 と数える)
  bool inQuotes = false;
  // cancel チェックは 64K 文字ごと (≈ 256 KB UTF-16)。十分こまかい一方で
  // 通常ファイルでは数回しか走らないオーバーヘッド。
  constexpr int kCancelStride = 1 << 16;
  for (int i = 0; i < text.size(); ++i) {
    if ((i & (kCancelStride - 1)) == 0
        && cancelToken
        && cancelToken->load(std::memory_order_acquire)) {
      return false;
    }
    const QChar c = text.at(i);
    if (inQuotes) {
      if (c == QChar('"')) {
        if (i + 1 < text.size() && text.at(i + 1) == QChar('"')) {
          ++i;  // エスケープ ""
        } else {
          inQuotes = false;
        }
      }
      continue;
    }
    if (c == QChar('"')) {
      inQuotes = true;
    } else if (c == delim) {
      ++curCols;
    } else if (c == QChar('\n') || c == QChar('\r')) {
      if (curCols > *outColumnCount) *outColumnCount = curCols;
      // \r\n は 1 行扱い
      int rowStart = i + 1;
      if (c == QChar('\r') && i + 1 < text.size()
          && text.at(i + 1) == QChar('\n')) {
        ++i;
        rowStart = i + 1;
      }
      if (rowStart < text.size()) {
        outRowStarts->append(rowStart);
        curCols = 1;
      }
    }
  }
  // 末尾の sentinel (= text 末尾位置)
  if (curCols > *outColumnCount) *outColumnCount = curCols;
  outRowStarts->append(text.size());

  // ファイル末尾が改行で終わっていた場合、最後に追加された行は空 (text.size() を
  // 開始 offset とする 0 長の行) になっている。これは「ファイル末尾の空行」と
  // 見なして削除する。
  if (outRowStarts->size() >= 2
      && outRowStarts->at(outRowStarts->size() - 2) == text.size()) {
    outRowStarts->removeAt(outRowStarts->size() - 2);
  }
  return true;
}

} // namespace

// ===== CsvTableModel =====

CsvTableModel::CsvTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void CsvTableModel::setSource(QString text,
                                QChar delimiter,
                                QVector<int> rowStarts,
                                int columnCount,
                                bool firstRowAsHeader) {
  beginResetModel();
  m_text             = std::move(text);
  m_delimiter        = delimiter;
  m_rowStarts        = std::move(rowStarts);
  m_columnCount      = columnCount;
  m_firstRowAsHeader = firstRowAsHeader;
  m_rowCache.clear();
  m_cacheOrder.clear();
  rebuildMatches();
  endResetModel();
}

void CsvTableModel::clear() {
  beginResetModel();
  m_text.clear();
  m_rowStarts.clear();
  m_columnCount = 0;
  m_rowCache.clear();
  m_cacheOrder.clear();
  m_matches.clear();
  endResetModel();
}

void CsvTableModel::setFirstRowAsHeader(bool enabled) {
  if (m_firstRowAsHeader == enabled) return;
  beginResetModel();
  m_firstRowAsHeader = enabled;
  rebuildMatches();
  endResetModel();
}

int CsvTableModel::setSearchString(const QString& needle, bool caseSensitive) {
  m_search = needle;
  m_searchCaseSensitive = caseSensitive;
  rebuildMatches();
  if (rowCount() > 0 && m_columnCount > 0) {
    emit dataChanged(index(0, 0),
                     index(rowCount() - 1, m_columnCount - 1),
                     { Qt::BackgroundRole });
  }
  return m_matches.size();
}

QPair<int,int> CsvTableModel::matchAt(int matchIndex) const {
  if (matchIndex < 0 || matchIndex >= m_matches.size()) return { -1, -1 };
  return m_matches.at(matchIndex);
}

int CsvTableModel::findRowForOffset(int offset) const {
  // m_rowStarts は昇順。最後の要素は sentinel (text.size())。
  // offset を含む行 = m_rowStarts[r] <= offset < m_rowStarts[r+1] となる r。
  if (m_rowStarts.size() < 2) return -1;
  int lo = 0, hi = m_rowStarts.size() - 2;   // sentinel 手前まで
  while (lo < hi) {
    const int mid = (lo + hi + 1) / 2;
    if (m_rowStarts.at(mid) <= offset) lo = mid;
    else                                hi = mid - 1;
  }
  return lo;
}

int CsvTableModel::findColForOffsetWithinRow(int rawRow, int offsetInRow) const {
  // 行内の delim カウントで列を割り出す。quoted 区間は無視。
  if (rawRow < 0 || rawRow + 1 >= m_rowStarts.size()) return 0;
  const int start = m_rowStarts.at(rawRow);
  const int end   = m_rowStarts.at(rawRow + 1);
  int col = 0;
  bool inQuotes = false;
  for (int i = start; i < end && i < start + offsetInRow; ++i) {
    const QChar c = m_text.at(i);
    if (inQuotes) {
      if (c == QChar('"')) {
        if (i + 1 < end && m_text.at(i + 1) == QChar('"')) ++i;
        else                                                inQuotes = false;
      }
      continue;
    }
    if (c == QChar('"'))      inQuotes = true;
    else if (c == m_delimiter) ++col;
  }
  return col;
}

void CsvTableModel::rebuildMatches() {
  m_matches.clear();
  if (m_search.isEmpty() || m_text.isEmpty()) return;
  const Qt::CaseSensitivity cs = m_searchCaseSensitive
                                   ? Qt::CaseSensitive
                                   : Qt::CaseInsensitive;
  const int viewRowOffset = m_firstRowAsHeader ? 1 : 0;
  // text 全体を 1 パスで indexOf 連射。1 セルに複数ヒットしても 1 マッチとして
  // 数える: 同じ (row, col) を重複追加しないために最後のものを覚えておく。
  int pos = 0;
  int lastRow = -1, lastCol = -1;
  while (true) {
    const int hit = m_text.indexOf(m_search, pos, cs);
    if (hit < 0) break;
    const int rawRow = findRowForOffset(hit);
    if (rawRow >= viewRowOffset) {
      const int rowStart = m_rowStarts.at(rawRow);
      const int col = findColForOffsetWithinRow(rawRow, hit - rowStart);
      const int viewRow = rawRow - viewRowOffset;
      if (viewRow != lastRow || col != lastCol) {
        m_matches.append({ viewRow, col });
        lastRow = viewRow;
        lastCol = col;
      }
    }
    pos = hit + qMax(1, m_search.size());
  }
}

int CsvTableModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  if (m_rowStarts.size() < 2) return 0;
  const int raw = m_rowStarts.size() - 1;  // sentinel を引いた行数
  return m_firstRowAsHeader ? (raw - 1) : raw;
}

int CsvTableModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return m_columnCount;
}

QStringList CsvTableModel::parseOneRow(int startOffset, int endOffset) const {
  QStringList row;
  QString     field;
  bool inQuotes = false;
  // 末尾の改行は含まないよう調整。
  while (endOffset > startOffset) {
    const QChar t = m_text.at(endOffset - 1);
    if (t == QChar('\n') || t == QChar('\r')) --endOffset;
    else                                       break;
  }
  for (int i = startOffset; i < endOffset; ++i) {
    const QChar c = m_text.at(i);
    if (inQuotes) {
      if (c == QChar('"')) {
        if (i + 1 < endOffset && m_text.at(i + 1) == QChar('"')) {
          field.append(QChar('"'));
          ++i;
        } else {
          inQuotes = false;
        }
      } else {
        field.append(c);
      }
      continue;
    }
    if (c == QChar('"') && field.isEmpty()) {
      inQuotes = true;
    } else if (c == m_delimiter) {
      row.append(field);
      field.clear();
    } else {
      field.append(c);
    }
  }
  row.append(field);
  return row;
}

QStringList CsvTableModel::rowAtRaw(int rawRow) const {
  if (rawRow < 0 || rawRow + 1 >= m_rowStarts.size()) return {};
  auto it = m_rowCache.find(rawRow);
  if (it != m_rowCache.end()) return it.value();
  // パース + キャッシュ。キャッシュ枠を超えたら古いものから捨てる。
  const int start = m_rowStarts.at(rawRow);
  const int end   = m_rowStarts.at(rawRow + 1);
  QStringList row = parseOneRow(start, end);
  m_rowCache.insert(rawRow, row);
  m_cacheOrder.append(rawRow);
  while (m_cacheOrder.size() > kCacheLimit) {
    const int oldRow = m_cacheOrder.takeFirst();
    m_rowCache.remove(oldRow);
  }
  return row;
}

QVariant CsvTableModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) return {};
  const int rawRow = m_firstRowAsHeader ? (index.row() + 1) : index.row();
  if (rawRow < 0 || rawRow + 1 >= m_rowStarts.size()) return {};
  const QStringList row = rowAtRaw(rawRow);
  if (index.column() < 0 || index.column() >= row.size()) return {};

  if (role == Qt::DisplayRole || role == Qt::ToolTipRole || role == Qt::EditRole) {
    return row.at(index.column());
  }
  if (role == Qt::BackgroundRole) {
    if (!m_search.isEmpty()) {
      const Qt::CaseSensitivity cs = m_searchCaseSensitive
                                       ? Qt::CaseSensitive
                                       : Qt::CaseInsensitive;
      if (row.at(index.column()).contains(m_search, cs)) {
        return QColor(255, 235, 100);
      }
    }
  }
  return {};
}

QVariant CsvTableModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const {
  if (role != Qt::DisplayRole) return {};
  if (orientation == Qt::Horizontal) {
    if (m_firstRowAsHeader && m_rowStarts.size() >= 2) {
      const QStringList head = rowAtRaw(0);
      if (section >= 0 && section < head.size()) return head.at(section);
    }
    return QString::number(section + 1);
  }
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
  applyToolbarStyle(m_toolbar);

  // 各ショートカットのネイティブ表記 (macOS: ⌘ / その他: Ctrl+ 等)。
  // ラベルには併記せず、ツールチップの中で案内する。
  auto& vkb = ViewerKeyBindingManager::instance();
  const QString findScut = vkb.primaryKeyText(QStringLiteral("viewer.csv.find_focus"));
  const QString encScut  = vkb.primaryKeyText(QStringLiteral("viewer.csv.encoding_focus"));
  const QString delScut  = vkb.primaryKeyText(QStringLiteral("viewer.csv.delimiter_focus"));
  const QString headerScut = vkb.primaryKeyText(QStringLiteral("viewer.csv.toggle_header"));
  const QString caseScut   = vkb.primaryKeyText(QStringLiteral("viewer.csv.toggle_case"));

  // ── エンコーディング ──
  auto* encodingLabel = new QLabel(tr("Encoding:"), m_toolbar);
  encodingLabel->setToolTip(
    tr("Character encoding used to decode this file (%1)").arg(encScut));
  m_toolbar->addWidget(encodingLabel);
  m_encodingCombo = new QComboBox(m_toolbar);
  m_encodingCombo->addItem(QStringLiteral("Auto"));
  m_encodingCombo->addItem(QStringLiteral("UTF-8"));
  m_encodingCombo->addItem(QStringLiteral("UTF-16LE"));
  m_encodingCombo->addItem(QStringLiteral("UTF-16BE"));
  m_encodingCombo->addItem(QStringLiteral("Shift_JIS"));
  m_encodingCombo->addItem(QStringLiteral("EUC-JP"));
  m_encodingCombo->addItem(QStringLiteral("ISO-8859-1"));
  m_encodingCombo->setFocusPolicy(Qt::StrongFocus);
  m_encodingCombo->setToolTip(
    tr("Character encoding used to decode this file (%1)").arg(encScut));
  m_toolbar->addWidget(m_encodingCombo);

  m_toolbar->addSeparator();

  // ── 区切り文字 ──
  auto* delimiterLabel = new QLabel(tr("Delimiter:"), m_toolbar);
  delimiterLabel->setToolTip(
    tr("Character used to separate columns (%1)").arg(delScut));
  m_toolbar->addWidget(delimiterLabel);
  m_delimiterCombo = new QComboBox(m_toolbar);
  m_delimiterCombo->addItem(tr("Auto"),       int(Delimiter::Auto));
  m_delimiterCombo->addItem(tr("Comma (,)"),  int(Delimiter::Comma));
  m_delimiterCombo->addItem(tr("Tab (\\t)"),  int(Delimiter::Tab));
  m_delimiterCombo->addItem(tr("Semicolon (;)"), int(Delimiter::Semicolon));
  m_delimiterCombo->setFocusPolicy(Qt::StrongFocus);
  m_delimiterCombo->setToolTip(
    tr("Character used to separate columns (%1)").arg(delScut));
  m_toolbar->addWidget(m_delimiterCombo);

  m_toolbar->addSeparator();

  // ── ヘッダ行扱い ──
  m_headerToggle = new QToolButton(m_toolbar);
  m_headerToggle->setText(tr("First row = header"));
  m_headerToggle->setCheckable(true);
  m_headerToggle->setChecked(true);
  m_headerToggle->setToolTip(
    tr("Treat the first row as column headers (%1)").arg(headerScut));
  m_headerToggle->setFocusPolicy(Qt::StrongFocus);
  m_toolbar->addWidget(m_headerToggle);

  m_toolbar->addSeparator();

  // ── 検索 (TextView / PdfView / MarkdownView と同じレイアウト) ──
  auto* findLabel = new QLabel(tr("Find:"), m_toolbar);
  findLabel->setToolTip(tr("Search text in this CSV (%1)").arg(findScut));
  m_toolbar->addWidget(findLabel);

  m_findEdit = new QLineEdit(m_toolbar);
  m_findEdit->setPlaceholderText(tr("Search text  (%1)").arg(findScut));
  m_findEdit->setToolTip(tr("Search text in this CSV (%1)").arg(findScut));
  m_findEdit->setClearButtonEnabled(true);
  m_findEdit->setFocusPolicy(Qt::StrongFocus);
  m_findEdit->setMinimumWidth(160);
  m_findEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_findEdit->installEventFilter(this);
  m_toolbar->addWidget(m_findEdit);

  m_findPrevBtn = new QToolButton(m_toolbar);
  m_findPrevBtn->setText(QStringLiteral("◀"));
  m_findPrevBtn->setToolTip(tr("Previous match (Shift+Enter)"));
  m_findPrevBtn->setFocusPolicy(Qt::StrongFocus);
  connect(m_findPrevBtn, &QToolButton::clicked, this, &CsvView::findPrevious);
  m_toolbar->addWidget(m_findPrevBtn);

  m_findNextBtn = new QToolButton(m_toolbar);
  m_findNextBtn->setText(QStringLiteral("▶"));
  m_findNextBtn->setToolTip(tr("Next match (Enter)"));
  m_findNextBtn->setFocusPolicy(Qt::StrongFocus);
  connect(m_findNextBtn, &QToolButton::clicked, this, &CsvView::findNext);
  m_toolbar->addWidget(m_findNextBtn);

  m_findCsButton = new QToolButton(m_toolbar);
  m_findCsButton->setCheckable(true);
  m_findCsButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/case-sensitive.svg")));
  m_findCsButton->setToolTip(
    tr("Toggle case-sensitive search (%1)").arg(caseScut));
  m_findCsButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_findCsButton, &QToolButton::toggled, this, [this](bool) {
    runSearchAndJump();
  });
  m_toolbar->addWidget(m_findCsButton);

  m_findStatus = new QLabel(m_toolbar);
  m_findStatus->setMinimumWidth(80);
  m_toolbar->addWidget(m_findStatus);

  connect(m_findEdit, &QLineEdit::textChanged,
          this,        &CsvView::onFindTextChanged);

  // Cmd/Ctrl+F のグローバルキャプチャ
  qApp->installEventFilter(this);

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
  // 列幅の自動計算で全行をパースしてしまうと遅延ロードの意味が無くなる。
  // サンプル上限を低めに固定する (Qt の既定は 1000)。
  m_table->horizontalHeader()->setResizeContentsPrecision(100);
  m_table->verticalHeader()->setDefaultSectionSize(20);
  m_table->verticalHeader()->setMinimumSectionSize(16);
  m_table->setFocusPolicy(Qt::StrongFocus);
  root->addWidget(m_table, /*stretch*/ 1);

  setFocusProxy(m_table);

  // 表示フォント (テーマ依存: Settings → Plugins → CSV/TSV Viewer) と配色を
  // 適用する。QTableView に stylesheet を当てると、その時点の qApp パレットが
  // 固定され、以後のテーマ切替 (qApp->setPalette) に追従しなくなる。テーマ変更
  // 時に stylesheet を一旦クリアして再適用することで QStyleSheetStyle を
  // 再ポリッシュさせ、現在のパレットへ更新させる。
  auto applyViewerAppearance = [this]() {
    const QFont f = Settings::instance().csvViewerFont();
    m_table->setFont(f);
    m_table->horizontalHeader()->setFont(f);
    m_table->verticalHeader()->setFont(f);
    m_table->setStyleSheet(QString());
    m_table->setStyleSheet(inactiveSelectionStyleSheet());
  };
  applyViewerAppearance();
  connect(&Settings::instance(), &Settings::settingsChanged, this,
          applyViewerAppearance);

  connect(m_encodingCombo, &QComboBox::currentTextChanged,
          this,             &CsvView::onEncodingComboChanged);
  connect(m_delimiterCombo, qOverload<int>(&QComboBox::currentIndexChanged),
          this,              &CsvView::onDelimiterComboChanged);
  connect(m_headerToggle, &QToolButton::toggled,
          this,            &CsvView::onHeaderToggleChanged);

  // 既定の表示設定 (Settings → Plugins → CSV/TSV Viewer)。combo / toggle を
  // 設定値に合わせる (slot が m_userDelimiter / model を更新する)。
  {
    Settings& s = Settings::instance();
    const QString d = s.csvViewerDelimiter();
    Delimiter def = Delimiter::Auto;
    if      (d == QLatin1String("comma"))     def = Delimiter::Comma;
    else if (d == QLatin1String("tab"))       def = Delimiter::Tab;
    else if (d == QLatin1String("semicolon")) def = Delimiter::Semicolon;
    const int idx = m_delimiterCombo->findData(int(def));
    if (idx >= 0) m_delimiterCombo->setCurrentIndex(idx);
    m_headerToggle->setChecked(s.csvViewerFirstRowAsHeader());
  }

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

  r.text = decodeBytes(r.data, r.actualEncoding);
  if (cancelled()) return r;

  // 区切り判定
  if (delimiter == Delimiter::Auto) {
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    r.actualDelimiter = detectDelimiter(r.text, suffix);
  } else {
    r.actualDelimiter = delimiterChar(delimiter);
  }
  if (cancelled()) return r;

  // 行オフセット index と最大列数を 1 パスで構築。実際の各セルへの
  // QString パースは表示時にモデル側で行う (遅延ロード)。
  // 巨大ファイルに対するこのループは prepareLoad で最も重いので、cancelToken
  // を奥まで渡して即時中断できるようにする。
  const bool indexed = buildRowIndex(r.text, r.actualDelimiter,
                                      &r.rowStarts, &r.columnCount,
                                      cancelToken);
  if (!indexed || cancelled()) return r;   // ok = false のまま返す
  r.ok = true;
  return r;
}

void CsvView::applyPreparedLoad(const PreparedLoad& r) {
  m_filePath        = r.filePath;
  m_data            = r.data;
  m_actualEncoding  = r.actualEncoding;
  m_actualDelimiter = r.actualDelimiter;
  m_totalSize       = r.totalSize;
  m_loadedSize      = r.loadedSize;

  m_model->setSource(r.text, r.actualDelimiter, r.rowStarts, r.columnCount,
                     m_headerToggle && m_headerToggle->isChecked());

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
  // bytes はそのまま、encoding / delimiter だけ作り直して index を再構築。
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
  QVector<int> rowStarts;
  int columnCount = 0;
  buildRowIndex(text, delim, &rowStarts, &columnCount);
  m_model->setSource(text, delim, rowStarts, columnCount,
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
  m_currentMatchIndex = -1;
  if (m_findEdit)   m_findEdit->clear();
  if (m_findStatus) m_findStatus->clear();
}

// ---------- 検索 ----------

void CsvView::onFindTextChanged(const QString& text) {
  if (!m_model) return;
  const bool cs = m_findCsButton && m_findCsButton->isChecked();
  m_model->setSearchString(text, cs);
  m_currentMatchIndex = -1;
  updateFindStatus();
  // 入力中はジャンプしない (= 入力する度にスクロールがガタガタするのを防ぐ)。
  // ユーザーが Enter / 前次ボタンを押したタイミングで初めて飛ぶ。
}

void CsvView::runSearchAndJump() {
  if (!m_model || !m_findEdit) return;
  const bool cs = m_findCsButton && m_findCsButton->isChecked();
  m_model->setSearchString(m_findEdit->text(), cs);
  m_currentMatchIndex = -1;
  if (m_model->matchCount() > 0) {
    m_currentMatchIndex = 0;
    jumpToMatch(0);
  } else {
    updateFindStatus();
  }
}

void CsvView::findNext() {
  if (!m_model) return;
  if (m_model->searchString().isEmpty()
      || m_model->searchString() != (m_findEdit ? m_findEdit->text() : QString())) {
    runSearchAndJump();
    return;
  }
  const int total = m_model->matchCount();
  if (total <= 0) { updateFindStatus(); return; }
  m_currentMatchIndex = (m_currentMatchIndex + 1) % total;
  jumpToMatch(m_currentMatchIndex);
}

void CsvView::findPrevious() {
  if (!m_model) return;
  if (m_model->searchString().isEmpty()
      || m_model->searchString() != (m_findEdit ? m_findEdit->text() : QString())) {
    runSearchAndJump();
    return;
  }
  const int total = m_model->matchCount();
  if (total <= 0) { updateFindStatus(); return; }
  m_currentMatchIndex = (m_currentMatchIndex <= 0)
                          ? (total - 1)
                          : (m_currentMatchIndex - 1);
  jumpToMatch(m_currentMatchIndex);
}

void CsvView::jumpToMatch(int matchIndex) {
  if (!m_model || !m_table) return;
  const auto rc = m_model->matchAt(matchIndex);
  if (rc.first < 0) return;
  const QModelIndex idx = m_model->index(rc.first, rc.second);
  m_table->setCurrentIndex(idx);
  m_table->scrollTo(idx, QAbstractItemView::PositionAtCenter);
  updateFindStatus();
}

void CsvView::updateFindStatus() {
  if (!m_findStatus || !m_model || !m_findEdit) return;
  if (m_findEdit->text().isEmpty()) {
    m_findStatus->clear();
    return;
  }
  const int total = m_model->matchCount();
  if (total <= 0) {
    m_findStatus->setText(tr("Not found"));
  } else if (m_currentMatchIndex < 0) {
    m_findStatus->setText(tr("%1 matches").arg(total));
  } else {
    m_findStatus->setText(tr("%1 / %2")
                             .arg(m_currentMatchIndex + 1)
                             .arg(total));
  }
}

void CsvView::focusFindInput() {
  if (!m_findEdit) return;
  m_findEdit->setFocus(Qt::ShortcutFocusReason);
  m_findEdit->selectAll();
}

bool CsvView::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_findEdit && event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Escape) {
      if (m_table) m_table->setFocus(Qt::OtherFocusReason);
      return true;
    }
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
      if (ke->modifiers() & Qt::ShiftModifier) findPrevious();
      else                                     findNext();
      return true;
    }
  }
  if (event->type() == QEvent::ShortcutOverride
      || event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    const QKeySequence seq = ViewerKeyBindingManager::sequenceForEvent(ke);
    const QString cmd =
      ViewerKeyBindingManager::instance().commandForKey(QStringLiteral("csv"), seq);
    if (!cmd.isEmpty() && isVisible() && window() && window()->isActiveWindow()) {
      if (event->type() == QEvent::KeyPress) {
        dispatchViewerCommand(cmd);
      }
      event->accept();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void CsvView::dispatchViewerCommand(const QString& cmd) {
  if (cmd == QLatin1String("viewer.csv.find_focus")) {
    focusFindInput();
  } else if (cmd == QLatin1String("viewer.csv.encoding_focus")) {
    if (m_encodingCombo) m_encodingCombo->setFocus(Qt::ShortcutFocusReason);
  } else if (cmd == QLatin1String("viewer.csv.delimiter_focus")) {
    if (m_delimiterCombo) m_delimiterCombo->setFocus(Qt::ShortcutFocusReason);
  } else if (cmd == QLatin1String("viewer.csv.toggle_header")) {
    if (m_headerToggle) m_headerToggle->toggle();
  } else if (cmd == QLatin1String("viewer.csv.toggle_case")) {
    if (m_findCsButton) m_findCsButton->toggle();
  }
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
