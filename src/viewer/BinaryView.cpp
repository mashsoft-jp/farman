#include "BinaryView.h"
#include "settings/Settings.h"
#include "utils/EnterClickFilter.h"

#include <QAbstractTableModel>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMap>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QValidator>
#include <QStringDecoder>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>
// uchardet: "Auto" のエンコード判定に使う (TextView と同じ)。
#include <uchardet.h>
#include <QTextCodec>

#include <cstring>

namespace Farman {

namespace {

constexpr int  kBytesPerLine = 16;
constexpr char kHexDigits[]  = "0123456789abcdef";
// "Auto" 判定に読む先頭サンプルサイズ。大ファイル全体を uchardet に渡さない。
constexpr qint64 kEncodingSampleBytes = 64 * 1024;

// アドレス入力欄用のバリデータ。16 進数のみ許可し、さらに値が上限
// (*m_maxPtr = 最終バイトのオフセット) を超える入力を Invalid として弾く。
// 上限はファイルごとに変わるので、View 側の qint64 メンバを指すポインタを持ち、
// 常に最新の上限を参照する。
class HexAddressValidator : public QValidator {
public:
  HexAddressValidator(const qint64* maxPtr, QObject* parent)
    : QValidator(parent), m_maxPtr(maxPtr) {}

  State validate(QString& input, int& /*pos*/) const override {
    QString t = input.trimmed();
    // 先頭の "0x" / "0X" は許可 (プレフィックスのみは入力途中とみなす)。
    if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
      t = t.mid(2);
    }
    if (t.isEmpty()) {
      return Intermediate;  // 空 / プレフィックスのみは入力途中
    }
    // 16 進以外の文字が混じっていたら拒否。
    for (const QChar c : t) {
      if (!isHexDigit(c)) {
        return Invalid;
      }
    }
    const qint64 maxAddr = m_maxPtr ? *m_maxPtr : -1;
    if (maxAddr < 0) {
      return Invalid;  // 空ファイル等、指定可能なアドレスが無い
    }
    bool ok = false;
    const qint64 value = t.toLongLong(&ok, 16);
    if (!ok) {
      // 桁が多すぎて 64bit を超える等 → 上限超過として拒否。
      return Invalid;
    }
    if (value > maxAddr) {
      return Invalid;  // 範囲外は受け付けない
    }
    return Acceptable;
  }

private:
  static bool isHexDigit(QChar c) {
    return (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
        || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
        || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
  }
  const qint64* m_maxPtr = nullptr;
};

void appendUnitHex(QString& out, const unsigned char* valueBytes, int unitBytes,
                   BinaryViewerEndian endian) {
  if (endian == BinaryViewerEndian::Big) {
    for (int i = 0; i < unitBytes; ++i) {
      out.append(QLatin1Char(kHexDigits[valueBytes[i] >> 4]));
      out.append(QLatin1Char(kHexDigits[valueBytes[i] & 0xF]));
    }
  } else {
    for (int i = unitBytes - 1; i >= 0; --i) {
      out.append(QLatin1Char(kHexDigits[valueBytes[i] >> 4]));
      out.append(QLatin1Char(kHexDigits[valueBytes[i] & 0xF]));
    }
  }
}

QString resolveEncoding(const QByteArray& data, const QString& userEncoding) {
  if (userEncoding.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) != 0) {
    return userEncoding;
  }
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

QString decodeStringColumn(const QByteArray& chunk, const QString& encoding) {
  QString        decoded;
  QStringDecoder decoder(encoding.toUtf8().constData());
  if (decoder.isValid()) {
    decoded = decoder.decode(chunk);
  } else if (QTextCodec* codec = QTextCodec::codecForName(encoding.toUtf8())) {
    decoded = codec->toUnicode(chunk);
  } else {
    decoded = QStringDecoder(QStringDecoder::Utf8).decode(chunk);
  }
  QString out;
  out.reserve(decoded.size());
  for (QChar ch : decoded) {
    if (ch == QChar(0xFFFD) || !ch.isPrint() || ch.isSpace()) {
      out.append(QLatin1Char('.'));
    } else {
      out.append(ch);
    }
  }
  return out;
}

// mmap (or seek+read フォールバック) でファイルバイトを供給する。表示中の行の
// バイトだけを読むので、全体を読み込まない。
class HexDataSource {
public:
  ~HexDataSource() { close(); }

  bool open(const QString& path) {
    close();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
      return false;
    }
    m_size = m_file.size();
    // 0 バイトや mmap 失敗時は m_map=nullptr のまま seek+read にフォールバック。
    m_map = (m_size > 0) ? m_file.map(0, m_size) : nullptr;
    return true;
  }

  void close() {
    if (m_map) {
      m_file.unmap(m_map);
      m_map = nullptr;
    }
    if (m_file.isOpen()) {
      m_file.close();
    }
    m_size = 0;
  }

  qint64 size() const { return m_size; }

  // off から最大 len バイトを out へ読み、実際に読めたバイト数を返す。
  int read(qint64 off, char* out, int len) {
    if (off < 0 || off >= m_size) {
      return 0;
    }
    const int n = static_cast<int>(qMin<qint64>(len, m_size - off));
    if (m_map) {
      std::memcpy(out, m_map + off, static_cast<size_t>(n));
      return n;
    }
    if (!m_file.seek(off)) {
      return 0;
    }
    return static_cast<int>(m_file.read(out, n));
  }

private:
  QFile   m_file;
  uchar*  m_map  = nullptr;
  qint64  m_size = 0;
};

// セルのテキストを最小マージンで描画するデリゲート。既定のアイテムデリゲートは
// セル左右に固定マージンを取るため、列を詰めると "00" が "…" に省略され、省略を
// 避けようと列を広げると単位間の隙間が大きくなる。普通のバイナリエディタのように
// 1 文字ぶんだけ空けるため、背景 (選択 / BackgroundRole) は既定スタイルで描き、
// テキストだけ 2px の左マージンで自前描画する。ForegroundRole (アドレス色 /
// カーソル行) と選択配色も尊重する。
class TightCellDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const QString text     = opt.text;
    const bool    selected = (opt.state & QStyle::State_Selected);

    // 1) まずセル全体を通常背景で塗り直す。ビューが選択セルを全幅で塗る下地を
    //    打ち消し、次の「テキスト密着塗り」がはみ出さないようにする。
    painter->fillRect(opt.rect, opt.palette.brush(QPalette::Base));

    // 2) 選択 or モデル背景 (カーソル行のアドレス等) を「テキストに密着した幅」で
    //    上塗り。左寄せなので右の隙間には塗らない (右にはみ出さない)。
    QBrush hl(Qt::NoBrush);
    if (selected) {
      hl = opt.palette.brush(QPalette::Highlight);
    } else {
      const QVariant b = index.data(Qt::BackgroundRole);
      if (b.canConvert<QBrush>()) {
        hl = qvariant_cast<QBrush>(b);
      }
    }
    if (hl.style() != Qt::NoBrush) {
      const int  textW = QFontMetrics(opt.font).horizontalAdvance(text);
      const int  w     = qMin(textW + 2, opt.rect.width());
      painter->fillRect(QRect(opt.rect.left(), opt.rect.top(), w, opt.rect.height()), hl);
    }

    // 3) テキスト。
    QColor color;
    if (selected) {
      color = opt.palette.color(QPalette::HighlightedText);
    } else {
      const QVariant fg = index.data(Qt::ForegroundRole);
      color = fg.canConvert<QColor>() ? qvariant_cast<QColor>(fg)
                                      : opt.palette.color(QPalette::Text);
    }
    painter->save();
    painter->setFont(opt.font);
    painter->setPen(color);
    painter->drawText(opt.rect.adjusted(1, 0, -1, 0),
                      Qt::AlignVCenter | Qt::AlignLeft, text);
    painter->restore();
  }
};

} // namespace

// ── 仮想化された 16 進ダンプモデル ──────────────────────────────
// rowCount = ceil(fileSize / 16)。表示された行だけ data() が呼ばれ、その行の
// バイトを読み取って整形する。
// 列構成: [0]=Address, [1..units]=各 unit の 16 進, [units+1]=ASCII。
// unit ごとに列を分けることで、セルカーソルが「単位」単位で動く。
class BinaryHexModel : public QAbstractTableModel {
public:
  explicit BinaryHexModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

  bool open(const QString& path) {
    beginResetModel();
    const bool ok = m_src.open(path);
    // 32bit rowCount の上限を超えないようにクランプ (約 32GB 相当)。
    const qint64 rows = (m_src.size() + kBytesPerLine - 1) / kBytesPerLine;
    m_rows       = static_cast<int>(qMin<qint64>(rows, 0x7fffffff));
    m_currentRow = -1;
    endResetModel();
    return ok;
  }

  void close() {
    beginResetModel();
    m_src.close();
    m_rows       = 0;
    m_currentRow = -1;
    endResetModel();
  }

  qint64 fileSize() const { return m_src.size(); }

  int unitsPerLine()    const { return kBytesPerLine / binaryViewerUnitToBytes(m_unit); }
  int firstUnitColumn() const { return 1; }
  int asciiColumn()     const { return 1 + unitsPerLine(); }

  void setFormat(BinaryViewerUnit unit, BinaryViewerEndian endian, const QString& encoding) {
    const bool unitChanged = (unit != m_unit);
    m_unit     = unit;
    m_endian   = endian;
    m_encoding = encoding;
    if (unitChanged) {
      // 列数 (unitsPerLine) が変わるのでリセットが必要。
      beginResetModel();
      endResetModel();
    } else if (m_rows > 0) {
      emit dataChanged(index(0, 0), index(m_rows - 1, columnCount() - 1), { Qt::DisplayRole });
    }
  }

  void setAddressColor(const QColor& c) {
    m_addrColor = c;
    if (m_rows > 0) {
      emit dataChanged(index(0, 0), index(m_rows - 1, 0), { Qt::ForegroundRole });
    }
  }

  // カーソル行のアドレスを「選択中」と同じ配色で見せるための色。
  void setSelectionColors(const QColor& bg, const QColor& fg) {
    m_selBg = bg;
    m_selFg = fg;
  }

  // 現在行 (セルカーソルのある行) を記録し、アドレス列を再描画させる。
  void setCurrentRow(int row) {
    if (row == m_currentRow) return;
    const int old = m_currentRow;
    m_currentRow  = row;
    auto notify = [this](int r) {
      if (r >= 0 && r < m_rows) {
        const QModelIndex i = index(r, 0);
        emit dataChanged(i, i, { Qt::BackgroundRole, Qt::ForegroundRole });
      }
    };
    notify(old);
    notify(m_currentRow);
  }

  int rowCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() ? 0 : m_rows;
  }
  int columnCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() ? 0 : (unitsPerLine() + 2);  // Address + units + ASCII
  }

  QVariant data(const QModelIndex& idx, int role = Qt::DisplayRole) const override {
    if (!idx.isValid()) {
      return {};
    }
    const int    col     = idx.column();
    const qint64 lineOff = static_cast<qint64>(idx.row()) * kBytesPerLine;

    // アドレス列。
    if (col == 0) {
      if (role == Qt::DisplayRole) {
        return QString::asprintf("%08llx", static_cast<unsigned long long>(lineOff));
      }
      if (idx.row() == m_currentRow) {
        // カーソル行のアドレスは選択中と同じ配色で強調する。
        if (role == Qt::BackgroundRole) {
          return m_selBg.isValid() ? m_selBg
                                   : QApplication::palette().color(QPalette::Highlight);
        }
        if (role == Qt::ForegroundRole) {
          return m_selFg.isValid() ? m_selFg
                                   : QApplication::palette().color(QPalette::HighlightedText);
        }
      } else if (role == Qt::ForegroundRole && m_addrColor.isValid()) {
        return m_addrColor;
      }
      return {};
    }

    if (role != Qt::DisplayRole) {
      return {};
    }

    // ASCII 列 (1 セルに 16 文字)。
    if (col == asciiColumn()) {
      unsigned char buf[kBytesPerLine];
      const int     n = const_cast<HexDataSource&>(m_src).read(
                          lineOff, reinterpret_cast<char*>(buf), kBytesPerLine);
      return decodeStringColumn(QByteArray(reinterpret_cast<const char*>(buf), n), m_encoding);
    }

    // 単位セル (col = 1 .. units)。
    const int    unitBytes = binaryViewerUnitToBytes(m_unit);
    const int    u         = col - 1;
    const qint64 unitOff   = lineOff + static_cast<qint64>(u) * unitBytes;
    unsigned char ub[8];
    const int n = const_cast<HexDataSource&>(m_src).read(unitOff, reinterpret_cast<char*>(ub),
                                                         unitBytes);
    if (n <= 0) {
      return QString();
    }
    QString out;
    if (n >= unitBytes) {
      appendUnitHex(out, ub, unitBytes, m_endian);
    } else {
      // 端数 (最終行) はあるバイトだけ表示。
      for (int i = 0; i < n; ++i) {
        out.append(QLatin1Char(kHexDigits[ub[i] >> 4]));
        out.append(QLatin1Char(kHexDigits[ub[i] & 0xF]));
      }
    }
    return out;
  }

private:
  HexDataSource      m_src;
  int                m_rows       = 0;
  int                m_currentRow = -1;
  BinaryViewerUnit   m_unit       = BinaryViewerUnit::Byte1;
  BinaryViewerEndian m_endian     = BinaryViewerEndian::Little;
  QString            m_encoding   = QStringLiteral("UTF-8");
  QColor             m_addrColor;
  QColor             m_selBg;
  QColor             m_selFg;
};

BinaryView::BinaryView(QWidget* parent) : QWidget(parent) {
  setupUi();
  syncFromSettings();

  connect(&Settings::instance(), &Settings::settingsChanged, this, [this] {
    syncFromSettings();
  });
}

BinaryView::~BinaryView() = default;

void BinaryView::setupUi() {
  QVBoxLayout* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  QToolBar* toolbar = new QToolBar(this);
  toolbar->setMovable(false);
  toolbar->setFloatable(false);
  toolbar->setIconSize(QSize(20, 20));
  applyToolbarStyle(toolbar);

  toolbar->addWidget(new QLabel(tr("Unit:"), toolbar));
  m_unitCombo = new QComboBox(toolbar);
  m_unitCombo->addItem(tr("1 Byte"), 1);
  m_unitCombo->addItem(tr("2 Byte"), 2);
  m_unitCombo->addItem(tr("4 Byte"), 4);
  m_unitCombo->addItem(tr("8 Byte"), 8);
  m_unitCombo->setFocusPolicy(Qt::StrongFocus);
  toolbar->addWidget(m_unitCombo);

  toolbar->addWidget(new QLabel(tr("Endian:"), toolbar));
  m_endianCombo = new QComboBox(toolbar);
  m_endianCombo->addItem(tr("Little"), static_cast<int>(BinaryViewerEndian::Little));
  m_endianCombo->addItem(tr("Big"),    static_cast<int>(BinaryViewerEndian::Big));
  m_endianCombo->setFocusPolicy(Qt::StrongFocus);
  toolbar->addWidget(m_endianCombo);

  toolbar->addWidget(new QLabel(tr("Encoding:"), toolbar));
  m_encodingCombo = new QComboBox(toolbar);
  m_encodingCombo->setFocusPolicy(Qt::StrongFocus);
  rebuildEncodingItems();
  toolbar->addWidget(m_encodingCombo);

  // アドレスジャンプ (16 進オフセットへスクロール)。
  toolbar->addSeparator();
  toolbar->addWidget(new QLabel(tr("Address:"), toolbar));
  m_addressEdit = new QLineEdit(toolbar);
  m_addressEdit->setPlaceholderText(tr("hex e.g. 1a0"));
  m_addressEdit->setMaximumWidth(120);
  m_addressEdit->setFocusPolicy(Qt::StrongFocus);
  // 16 進数字のみ、かつ指定可能なアドレス上限 (m_maxAddress) を超える入力を弾く。
  m_addressEdit->setValidator(new HexAddressValidator(&m_maxAddress, m_addressEdit));
  toolbar->addWidget(m_addressEdit);
  // 指定可能なアドレスの上限を入力欄の右に表示 (ファイル読込時に更新)。
  m_addressMaxLabel = new QLabel(toolbar);
  m_addressMaxLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  toolbar->addWidget(m_addressMaxLabel);
  QPushButton* goButton = new QPushButton(tr("Go"), toolbar);
  goButton->setFocusPolicy(Qt::StrongFocus);
  toolbar->addWidget(goButton);

  root->addWidget(toolbar);

  auto* clickFilter = new EnterClickFilter(this);
  clickFilter->installOnButtonsIn(toolbar);

  // 16 進ダンプ本体 (QTableView + 仮想化モデル)。
  m_model = new BinaryHexModel(this);
  m_table = new QTableView(this);
  m_table->setModel(m_model);
  m_table->setShowGrid(false);
  m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
  m_table->setSelectionMode(QAbstractItemView::ContiguousSelection);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setWordWrap(false);
  m_table->setCornerButtonEnabled(false);
  m_table->horizontalHeader()->setVisible(false);
  m_table->verticalHeader()->setVisible(false);
  // 全行走査を避けるため列幅は自前で設定する (ResizeToContents は使わない)。
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  // セルのマージンを詰めて、単位間を 1 文字ぶんの隙間にする。
  m_table->setItemDelegate(new TightCellDelegate(m_table));
  root->addWidget(m_table, /*stretch*/ 1);

  // 巨大ファイル (数百万行の仮想化モデル) で、末尾付近までスクロールしてから
  // 上下に少し動かすと最終行が再描画されずに残ることがある (QTableView の
  // ScrollPerItem での既知の描画ムラ)。ScrollPerItem のままにしつつ (ScrollPerPixel
  // は行数×行高が int を溢れてスクロールが壊れる)、スクロール値が変わるたびに
  // viewport 全体を更新して確実に最終行まで描き直す。表示中の行数だけの再描画
  // なので軽い。
  connect(m_table->verticalScrollBar(), &QScrollBar::valueChanged,
          m_table->viewport(), QOverload<>::of(&QWidget::update));

  // Ctrl+C / Cmd+C: 選択セルをテキストとしてコピー。QShortcut だとアプリ側の
  // Copy コマンド (ファイルパスのコピー等) に先取りされてしまうため、テキスト
  // エディットと同様に ShortcutOverride で先取りして eventFilter で処理する。
  m_table->installEventFilter(this);

  // ViewerPanel / BinaryViewerWindow からの setFocus をテーブルへ転送。
  setFocusProxy(m_table);

  connect(m_unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
    m_unit = bytesToBinaryViewerUnit(m_unitCombo->currentData().toInt());
    applyModelFormat();
  });
  connect(m_endianCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
    m_endian = static_cast<BinaryViewerEndian>(m_endianCombo->currentData().toInt());
    applyModelFormat();
  });
  connect(m_encodingCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;
    m_encoding = trimmed;
    applyModelFormat();
  });
  connect(goButton, &QPushButton::clicked, this, &BinaryView::jumpToAddress);
  // アドレス入力欄の Enter は eventFilter で消費してジャンプにする
  // (returnPressed だとキーイベントが親へ伝播してビュアーが閉じてしまう)。
  m_addressEdit->installEventFilter(this);

  // セルカーソル (現在セル) の行が変わったら、モデルにも伝えてアドレス列を
  // 「選択中」配色で強調させる。
  connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged, this,
          [this](const QModelIndex& cur, const QModelIndex&) {
            m_model->setCurrentRow(cur.row());
          });
}

bool BinaryView::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_addressEdit && event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
      jumpToAddress();
      return true;  // 親へ伝播させない (ビュアーを閉じさせない)
    }
  }
  // テーブル上の Copy (Ctrl+C / Cmd+C) を、アプリ側の Copy コマンドより先に
  // 横取りして選択セルをコピーする。まず ShortcutOverride を accept して自分に
  // キーを回し、続く KeyPress で実処理する。
  if (watched == m_table) {
    if (event->type() == QEvent::ShortcutOverride) {
      auto* ke = static_cast<QKeyEvent*>(event);
      if (ke->matches(QKeySequence::Copy)) {
        event->accept();
        return true;
      }
    } else if (event->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(event);
      if (ke->matches(QKeySequence::Copy)) {
        copySelection();
        return true;
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

void BinaryView::rebuildEncodingItems() {
  m_encodingCombo->clear();
  m_encodingCombo->addItem(QStringLiteral("Auto"));
  m_encodingCombo->addItem(QStringLiteral("UTF-8"));
  m_encodingCombo->addItem(QStringLiteral("UTF-16LE"));
  m_encodingCombo->addItem(QStringLiteral("UTF-16BE"));
  m_encodingCombo->addItem(QStringLiteral("Shift_JIS"));
  m_encodingCombo->addItem(QStringLiteral("EUC-JP"));
  m_encodingCombo->addItem(QStringLiteral("ISO-8859-1"));
}

void BinaryView::syncFromSettings() {
  const Settings& s = Settings::instance();
  m_unit     = s.binaryViewerUnit();
  m_endian   = s.binaryViewerEndian();
  m_encoding = s.binaryViewerEncoding();
  m_table->setFont(s.binaryViewerFont());

  // 通常 / 選択カラーを QPalette 経由で適用 (qApp テーマパレットを起点)。
  QPalette pal = QApplication::palette();
  if (s.binaryViewerNormalForeground().isValid())
    pal.setColor(QPalette::Text, s.binaryViewerNormalForeground());
  if (s.binaryViewerNormalBackground().isValid())
    pal.setColor(QPalette::Base, s.binaryViewerNormalBackground());
  if (s.binaryViewerSelectedForeground().isValid())
    pal.setColor(QPalette::HighlightedText, s.binaryViewerSelectedForeground());
  if (s.binaryViewerSelectedBackground().isValid())
    pal.setColor(QPalette::Highlight, s.binaryViewerSelectedBackground());
  m_table->setPalette(pal);

  // 行高はフォントに合わせてコンパクトに。
  m_table->verticalHeader()->setDefaultSectionSize(
    QFontMetrics(s.binaryViewerFont()).height() + 2);

  // コンボの再選択 (シグナル抑止)。
  {
    QSignalBlocker b(m_unitCombo);
    const int unitBytes = binaryViewerUnitToBytes(m_unit);
    for (int i = 0; i < m_unitCombo->count(); ++i) {
      if (m_unitCombo->itemData(i).toInt() == unitBytes) {
        m_unitCombo->setCurrentIndex(i);
        break;
      }
    }
  }
  {
    QSignalBlocker b(m_endianCombo);
    for (int i = 0; i < m_endianCombo->count(); ++i) {
      if (m_endianCombo->itemData(i).toInt() == static_cast<int>(m_endian)) {
        m_endianCombo->setCurrentIndex(i);
        break;
      }
    }
  }
  {
    QSignalBlocker b(m_encodingCombo);
    m_encodingCombo->setCurrentText(m_encoding);
  }

  // アドレス色 + カーソル行の選択配色 + フォーマットをモデルへ反映。
  m_model->setAddressColor(s.binaryViewerAddressForeground());
  m_model->setSelectionColors(s.binaryViewerSelectedBackground(),
                              s.binaryViewerSelectedForeground());
  applyModelFormat();
}

void BinaryView::applyModelFormat() {
  // "Auto" のときは先頭サンプルで判定。ファイルが開かれていなければ素通し。
  if (m_encoding.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0
      && !m_filePath.isEmpty()) {
    QFile f(m_filePath);
    QByteArray sample;
    if (f.open(QIODevice::ReadOnly)) {
      sample = f.read(kEncodingSampleBytes);
    }
    m_actualEncoding = resolveEncoding(sample, m_encoding);
  } else {
    m_actualEncoding = m_encoding;
  }
  m_model->setFormat(m_unit, m_endian, m_actualEncoding);
  updateColumnWidths();
}

void BinaryView::updateColumnWidths() {
  const QFontMetrics fm(m_table->font());
  const int unitBytes = binaryViewerUnitToBytes(m_unit);
  const int units     = kBytesPerLine / unitBytes;
  // セル幅 = テキスト幅 + cellPad。左寄せ描画なので単位間の隙間は概ね cellPad に
  // なる。従来は 1 文字ぶん ("FF FF" のスペース 1 個相当) にしていたが、Linux の
  // 既定 monospace (DejaVu Sans Mono) では隙間が広く見えるため、半文字ぶんに詰める
  // (全プラットフォーム共通。最低 3px は確保)。
  const int cellPad = qMax(3, fm.horizontalAdvance(QLatin1Char('0')) / 2);
  // アドレス / バイト列 / ASCII の 3 セクション間はもう少し広く空ける。
  // 従来 3 文字ぶんだったが、各セクション間を 1 文字ずつ詰めて 2 文字ぶんにする。
  const int sectionGap = fm.averageCharWidth() * 2;
  // Address: 8 桁 + 余白 + セクション間 (アドレスとバイト列の間を空ける)。
  m_table->setColumnWidth(0,
      fm.horizontalAdvance(QStringLiteral("00000000")) + cellPad + sectionGap);
  // 各 unit セル: 2*unitBytes 桁の 16 進 + 余白。最後の unit だけ後ろに
  // セクション間を足して、バイト列と ASCII の間を空ける。
  const QString unitSample(unitBytes * 2, QLatin1Char('F'));
  const int     unitW = fm.horizontalAdvance(unitSample) + cellPad;
  for (int u = 0; u < units; ++u) {
    m_table->setColumnWidth(1 + u, unitW + (u == units - 1 ? sectionGap : 0));
  }
  // ASCII: 16 文字ぶん (全角混在も想定して少し広め)。
  m_table->setColumnWidth(1 + units,
                          fm.horizontalAdvance(QStringLiteral("WWWWWWWWWWWWWWWW")) + cellPad);
}

void BinaryView::jumpToAddress() {
  QString t = m_addressEdit->text().trimmed();
  if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
    t = t.mid(2);
  }
  if (t.isEmpty()) {
    return;
  }
  bool         ok   = false;
  const qint64 addr = t.toLongLong(&ok, 16);
  if (!ok || addr < 0 || addr >= m_totalSize) {
    return;
  }
  const int row = static_cast<int>(addr / kBytesPerLine);
  // 該当バイトを含む unit 列へカーソルを置く。
  const int unitBytes = binaryViewerUnitToBytes(m_unit);
  const int unitCol   = m_model->firstUnitColumn()
                        + static_cast<int>((addr % kBytesPerLine) / unitBytes);
  const QModelIndex idx = m_model->index(row, unitCol);
  m_table->setCurrentIndex(idx);
  m_table->scrollTo(idx, QAbstractItemView::PositionAtCenter);
}

void BinaryView::copySelection() {
  const QModelIndexList sel = m_table->selectionModel()->selectedIndexes();
  if (sel.isEmpty()) {
    return;
  }
  // 行 → (列 → テキスト) に整理 (QMap でキー昇順 = 行/列順にソートされる)。
  QMap<int, QMap<int, QString>> byRow;
  for (const QModelIndex& i : sel) {
    byRow[i.row()][i.column()] = m_model->data(i, Qt::DisplayRole).toString();
  }
  QStringList lines;
  for (auto rit = byRow.constBegin(); rit != byRow.constEnd(); ++rit) {
    QStringList cells;
    for (auto cit = rit.value().constBegin(); cit != rit.value().constEnd(); ++cit) {
      cells << cit.value();
    }
    lines << cells.join(QLatin1Char(' '));
  }
  QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}

bool BinaryView::loadFile(const QString& filePath) {
  PreparedLoad p = prepareLoad(filePath, m_unit, m_endian, m_encoding);
  if (!p.ok) return false;
  applyPreparedLoad(p);
  return true;
}

BinaryView::PreparedLoad BinaryView::prepareLoad(const QString&     filePath,
                                                 BinaryViewerUnit   /*unit*/,
                                                 BinaryViewerEndian /*endian*/,
                                                 const QString&     encoding,
                                                 const std::atomic<bool>* cancelToken,
                                                 qint64             /*maxBytes*/) {
  PreparedLoad r;
  r.filePath = filePath;

  auto cancelled = [&]() {
    return cancelToken && cancelToken->load(std::memory_order_acquire);
  };
  if (cancelled()) return r;

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return r;
  }
  r.totalSize  = file.size();
  r.loadedSize = r.totalSize;  // 切り詰めない

  // "Auto" のときだけ先頭サンプルを読んでエンコード判定 (大ファイルでも軽量)。
  if (encoding.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0) {
    const QByteArray sample = file.read(kEncodingSampleBytes);
    r.actualEncoding = resolveEncoding(sample, encoding);
  } else {
    r.actualEncoding = encoding;
  }
  file.close();

  if (cancelled()) return r;
  r.ok = true;
  return r;
}

void BinaryView::applyPreparedLoad(const PreparedLoad& r) {
  m_filePath       = r.filePath;
  m_totalSize      = r.totalSize;
  m_actualEncoding = r.actualEncoding;

  // 指定可能なアドレスの上限 (= 最終バイトのオフセット) と、その表示ラベルを更新。
  m_maxAddress = m_totalSize > 0 ? m_totalSize - 1 : -1;
  if (m_addressMaxLabel) {
    m_addressMaxLabel->setText(
      m_maxAddress >= 0
        ? QStringLiteral("/ %1").arg(QString::number(m_maxAddress, 16).toUpper())
        : QString());
    m_addressMaxLabel->setToolTip(tr("Maximum address (last byte offset)"));
  }
  if (m_addressEdit) {
    m_addressEdit->clear();
  }

  // mmap を開いてモデルへ渡す (メインスレッド)。
  m_model->setFormat(m_unit, m_endian, m_actualEncoding);
  m_model->setAddressColor(Settings::instance().binaryViewerAddressForeground());
  m_model->open(r.filePath);
  updateColumnWidths();
  if (m_model->rowCount() > 0) {
    m_table->scrollToTop();
    m_table->setCurrentIndex(m_model->index(0, m_model->firstUnitColumn()));
  }
}

void BinaryView::clearContent() {
  m_filePath.clear();
  m_totalSize = 0;
  m_maxAddress = -1;
  if (m_addressMaxLabel) m_addressMaxLabel->clear();
  if (m_addressEdit) m_addressEdit->clear();
  if (m_model) m_model->close();
}

QString BinaryView::statusInfo() const {
  if (m_filePath.isEmpty()) return QString();
  QString s = QLocale(QLocale::English).formattedDataSize(m_totalSize);
  if (m_encoding.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0
      && !m_actualEncoding.isEmpty()) {
    s += QStringLiteral("  ·  %1").arg(m_actualEncoding);
  }
  return s;
}

} // namespace Farman
