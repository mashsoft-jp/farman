#include "BinaryView.h"
#include "settings/Settings.h"
#include "utils/EnterClickFilter.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QButtonGroup>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QList>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QRadioButton>
#include <QStyle>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QValidator>
#include <QStringConverter>
#include <QStringDecoder>
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

// ASCII 列を、選択反転できるように「表示文字ごとに元バイトの開始位置」を添えて
// デコードする。1 バイトずつデコーダに与え、文字が確定した時点で、その文字が
// どのバイトから始まったかを記録する (マルチバイト文字にも対応)。非表示文字
// (制御文字 / 空白 / 不正シーケンス) は '.' に置換する。
void decodeAsciiCells(const unsigned char* buf, int n, const QString& encoding,
                      QString& outText, QList<int>& outByteStart) {
  outText.clear();
  outByteStart.clear();
  QStringDecoder dec(encoding.toUtf8().constData());
  if (!dec.isValid()) {
    dec = QStringDecoder(QStringDecoder::Utf8);
  }
  int start = 0;  // 生成中の文字が始まったバイト位置
  for (int i = 0; i < n; ++i) {
    const char one = static_cast<char>(buf[i]);
    const QString piece = dec.decode(QByteArray(&one, 1));
    if (!piece.isEmpty()) {
      for (const QChar ch : piece) {
        if (ch == QChar(0xFFFD) || !ch.isPrint() || ch.isSpace()) {
          outText.append(QLatin1Char('.'));
        } else {
          outText.append(ch);
        }
        outByteStart.append(start);
      }
      start = i + 1;
    }
  }
}

// 検索文字列を、指定エンコーディングでバイト列に変換する (ASCII 列の表示と対称)。
QByteArray encodeStringColumn(const QString& text, const QString& encoding) {
  QStringEncoder enc(encoding.toUtf8().constData());
  if (enc.isValid()) {
    return QByteArray(enc.encode(text));
  }
  if (QTextCodec* codec = QTextCodec::codecForName(encoding.toUtf8())) {
    return codec->fromUnicode(text);
  }
  return text.toUtf8();
}

// 16 進検索の入力を、表示単位 (unitBytes) とエンディアンに従ってバイト列に変換
// する。入力中の空白はすべて無視して連結し、単位ぶん (unitBytes*2 桁) ずつに
// グループ化する。各グループを 1 単位の値とみなし、表示と同じバイト順で並べる
// (例: 1 バイト "334455" → 33 44 55 / 2 バイト・リトル "1234" → 34 12)。
// 連結後の桁数が単位 (unitBytes*2) の倍数でない (桁不足 / 端数) 場合や 16 進
// 以外が混じる場合は ok=false で空を返す。formatted != nullptr のときは、単位
// ごとに空白で区切った整形済み文字列 ("33 44 55") を返す。
QByteArray parseHexSearchInput(const QString& text, int unitBytes,
                               BinaryViewerEndian endian, bool& ok,
                               QString* formatted = nullptr) {
  ok = false;
  if (formatted) {
    formatted->clear();
  }
  // 空白を除去して連結。先頭の "0x" は許可。
  QString h;
  h.reserve(text.size());
  for (const QChar c : text) {
    if (!c.isSpace()) {
      h.append(c);
    }
  }
  if (h.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
    h = h.mid(2);
  }
  if (h.isEmpty()) {
    return {};
  }
  for (const QChar c : h) {
    const bool hex = (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                  || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
                  || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
    if (!hex) {
      return {};
    }
  }
  const int groupLen = unitBytes * 2;
  if (h.size() % groupLen != 0) {
    return {};  // 桁不足 / 端数
  }
  QByteArray  pat;
  QStringList groups;
  for (int i = 0; i < h.size(); i += groupLen) {
    const QString g = h.mid(i, groupLen);
    groups << g;
    bool gOk = false;
    const qulonglong v = g.toULongLong(&gOk, 16);
    if (!gOk) {
      return {};
    }
    unsigned char le[8];
    for (int k = 0; k < unitBytes; ++k) {
      le[k] = static_cast<unsigned char>((v >> (8 * k)) & 0xFF);
    }
    if (endian == BinaryViewerEndian::Little) {
      for (int k = 0; k < unitBytes; ++k) {
        pat.append(static_cast<char>(le[k]));
      }
    } else {
      for (int k = unitBytes - 1; k >= 0; --k) {
        pat.append(static_cast<char>(le[k]));
      }
    }
  }
  if (formatted) {
    *formatted = groups.join(QLatin1Char(' '));
  }
  ok = true;
  return pat;
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

} // namespace

// 16 進ダンプを自前描画するビュー (QAbstractScrollArea)。
//
// QTableView は「1 行 = 1 セクション」でその累積ピクセル位置 (行番号 × 行高) を
// int で計算するため、GB 級ファイル (数億行) では行番号 × 行高が INT_MAX を超えて
// 座標が破綻し、途中から描画されなくなる。ここでは行番号などの絶対座標を持たず、
// 「画面に見えている行だけ」を HexDataSource (mmap) から読んで描画するので、
// ファイルサイズに依存せず破綻しない。縦スクロールは行単位 (超巨大ファイルでは
// スクロールバーを整数レンジに収めるためスケール) で管理する。
//
// 対応: スクロール (バー / ホイール / キーボード)、カーソル移動 (矢印 / Page /
// Home / End)、範囲選択 (Shift + 矢印 / マウスドラッグ)、選択の 16 進コピー
// (Ctrl/Cmd+C)、アドレスジャンプ、カーソル行アドレスの強調、単位/エンディアン/
// エンコーディング/配色の反映。
class HexView : public QAbstractScrollArea {
public:
  explicit HexView(QWidget* parent = nullptr) : QAbstractScrollArea(parent) {
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(false);
    horizontalScrollBar()->setSingleStep(8);
  }

  bool openFile(const QString& path) {
    m_src.close();
    const bool ok = m_src.open(path);
    m_totalSize  = m_src.size();
    m_totalLines = (m_totalSize + kBytesPerLine - 1) / kBytesPerLine;
    m_topLine = 0;
    m_cursor = 0;
    m_selAnchor = 0;
    m_hasSel = false;
    recompute();
    return ok;
  }
  void closeFile() {
    m_src.close();
    m_totalSize = 0;
    m_totalLines = 0;
    m_topLine = 0;
    m_cursor = 0;
    m_hasSel = false;
    recompute();
  }
  qint64 fileSize() const { return m_src.size(); }
  qint64 totalSize() const { return m_totalSize; }
  qint64 cursorOffset() const { return m_cursor; }
  // 現在の選択 (無ければカーソル) の先頭 / 末尾オフセット。検索の基準に使う。
  qint64 selectionStart() const {
    return m_hasSel ? qMin(m_selAnchor, m_cursor) : m_cursor;
  }
  qint64 selectionEnd() const {
    return m_hasSel ? qMax(m_selAnchor, m_cursor) : m_cursor;
  }

  // pat を from バイト目から前方 (forward) / 後方に探し、見つかった先頭オフセット
  // を返す (無ければ -1)。mmap / seek のどちらでも動くよう 1MB チャンクで走査し、
  // 境界をまたぐ一致のためにチャンクを pat 長 - 1 だけ重ねる。
  qint64 find(const QByteArray& pat, qint64 from, bool forward) const {
    if (pat.isEmpty() || m_totalSize <= 0) {
      return -1;
    }
    const int    m     = pat.size();
    const qint64 chunk = 1 << 20;
    HexDataSource& src = const_cast<HexDataSource&>(m_src);
    QByteArray buf;
    if (forward) {
      qint64 pos = qMax<qint64>(0, from);
      while (pos < m_totalSize) {
        const qint64 want = qMin<qint64>(chunk + m - 1, m_totalSize - pos);
        buf.resize(static_cast<int>(want));
        const int n = src.read(pos, buf.data(), static_cast<int>(want));
        if (n <= 0) {
          break;
        }
        const QByteArray view = QByteArray::fromRawData(buf.constData(), n);
        const int idx = view.indexOf(pat);
        if (idx >= 0) {
          return pos + idx;
        }
        if (pos + want >= m_totalSize) {
          break;  // 最終ウィンドウ
        }
        pos += chunk;
      }
    } else {
      if (from < 0) {
        return -1;
      }
      qint64 end = qMin<qint64>(m_totalSize, from + m);  // 探索領域 [0, end)
      while (end > 0) {
        const qint64 want = qMin<qint64>(chunk + m - 1, end);
        const qint64 pos  = end - want;
        buf.resize(static_cast<int>(want));
        const int n = src.read(pos, buf.data(), static_cast<int>(want));
        if (n <= 0) {
          break;
        }
        const QByteArray view = QByteArray::fromRawData(buf.constData(), n);
        // start <= from となる最大 index までを後方検索する。
        int limit = n - 1;
        if (pos + limit > from) {
          limit = static_cast<int>(from - pos);
        }
        if (limit >= 0) {
          const int idx = view.lastIndexOf(pat, limit);
          if (idx >= 0) {
            return pos + idx;
          }
        }
        if (pos <= 0) {
          break;
        }
        end = pos + (m - 1);  // 境界重なり
      }
    }
    return -1;
  }

  // off から len バイトを一致範囲として選択し、画面中央付近へスクロールする。
  void selectMatch(qint64 off, int len) {
    if (m_totalSize <= 0 || off < 0) {
      return;
    }
    off = qBound<qint64>(0, off, m_totalSize - 1);
    m_selAnchor = off;
    if (len > 1) {
      m_hasSel = true;
      m_cursor = qMin(m_totalSize - 1, off + len - 1);
    } else {
      m_hasSel = false;
      m_cursor = off;
    }
    const int visLines = qMax(1, viewport()->height() / rowH());
    m_topLine = qMax<qint64>(0, off / kBytesPerLine - visLines / 2);
    clampTop();
    syncVBar();
    viewport()->update();
  }

  void applyFont(const QFont& f) {
    QAbstractScrollArea::setFont(f);
    viewport()->setFont(f);
    recompute();
  }
  void setUnit(BinaryViewerUnit u) {
    if (u == m_unit) {
      return;
    }
    m_unit = u;
    m_cursor -= m_cursor % binaryViewerUnitToBytes(m_unit);
    recompute();
  }
  void setEndian(BinaryViewerEndian e) {
    m_endian = e;
    viewport()->update();
  }
  void setEncoding(const QString& enc) {
    m_encoding = enc;
    viewport()->update();
  }
  void setColors(const QColor& addr, const QColor& normFg, const QColor& normBg,
                 const QColor& selFg, const QColor& selBg) {
    m_addrColor = addr;
    m_normFg = normFg;
    m_normBg = normBg;
    m_selFg = selFg;
    m_selBg = selBg;
    QPalette pal = viewport()->palette();
    if (normBg.isValid()) {
      pal.setColor(QPalette::Base, normBg);
      pal.setColor(QPalette::Window, normBg);
    }
    viewport()->setPalette(pal);
    viewport()->setAutoFillBackground(true);
    viewport()->update();
  }

  // 指定アドレスへカーソルを移動し、画面中央付近に表示する。
  void jumpTo(qint64 addr) {
    if (m_totalSize <= 0) {
      return;
    }
    addr = qBound<qint64>(0, addr, m_totalSize - 1);
    const int ub = binaryViewerUnitToBytes(m_unit);
    m_cursor = addr - addr % ub;
    m_hasSel = false;
    m_selAnchor = m_cursor;
    const int visLines = qMax(1, viewport()->height() / rowH());
    m_topLine = qMax<qint64>(0, m_cursor / kBytesPerLine - visLines / 2);
    clampTop();
    syncVBar();
    viewport()->update();
  }

  // 選択範囲 (無ければカーソル単位) を 16 進テキストとしてクリップボードへ。
  void copySelectionToClipboard() {
    if (m_totalSize <= 0) {
      return;
    }
    const int ub = binaryViewerUnitToBytes(m_unit);
    qint64 lo = 0;
    qint64 hi = 0;
    if (m_hasSel) {
      lo = qMin(m_selAnchor, m_cursor);
      hi = qMax(m_selAnchor, m_cursor);
    } else {
      lo = m_cursor - m_cursor % ub;
      hi = qMin(m_totalSize - 1, lo + ub - 1);
    }
    lo -= lo % ub;
    // 巨大選択でメモリ / クリップボードを溢れさせないため上限を設ける。
    const qint64 kMaxBytes = 4 * 1024 * 1024;
    if (hi - lo + 1 > kMaxBytes) {
      hi = lo + kMaxBytes - 1;
    }
    QString out;
    unsigned char tmp[8];
    for (qint64 off = lo; off <= hi; off += ub) {
      const int n = m_src.read(off, reinterpret_cast<char*>(tmp), ub);
      if (n <= 0) {
        break;
      }
      QString hx;
      if (n >= ub) {
        appendUnitHex(hx, tmp, ub, m_endian);
      } else {
        for (int k = 0; k < n; ++k) {
          hx.append(QLatin1Char(kHexDigits[tmp[k] >> 4]));
          hx.append(QLatin1Char(kHexDigits[tmp[k] & 0xF]));
        }
      }
      if (!out.isEmpty()) {
        out.append((off % kBytesPerLine == 0) ? QLatin1Char('\n') : QLatin1Char(' '));
      }
      out.append(hx);
    }
    QGuiApplication::clipboard()->setText(out);
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(viewport());
    const int hoff = horizontalScrollBar()->value();
    const QColor normBg = m_normBg.isValid() ? m_normBg : palette().color(QPalette::Base);
    p.fillRect(viewport()->rect(), normBg);
    if (m_totalSize <= 0) {
      return;
    }
    p.setFont(font());
    const QFontMetrics fm(font());
    const int ascent   = fm.ascent();
    const int rh       = rowH();
    const int visLines = viewport()->height() / rh + 2;
    const int unitBytes = binaryViewerUnitToBytes(m_unit);
    const int units     = kBytesPerLine / unitBytes;

    qint64 selLo = -1;
    qint64 selHi = -1;
    if (m_hasSel) {
      selLo = qMin(m_selAnchor, m_cursor);
      selHi = qMax(m_selAnchor, m_cursor);
    }
    const QColor normFg = m_normFg.isValid() ? m_normFg : palette().color(QPalette::Text);
    const QColor addrFg = m_addrColor.isValid() ? m_addrColor : normFg;
    const QColor selBg  = m_selBg.isValid() ? m_selBg : palette().color(QPalette::Highlight);
    const QColor selFg  = m_selFg.isValid() ? m_selFg : palette().color(QPalette::HighlightedText);

    unsigned char buf[kBytesPerLine];
    for (int i = 0; i < visLines; ++i) {
      const qint64 line = m_topLine + i;
      if (line >= m_totalLines) {
        break;
      }
      const qint64 lineOff = line * kBytesPerLine;
      const int y     = i * rh;
      const int textY = y + ascent + 1;
      const int n = m_src.read(lineOff, reinterpret_cast<char*>(buf), kBytesPerLine);

      // アドレス (カーソル行は選択配色で強調)。
      const bool cursorLine = (m_cursor >= lineOff && m_cursor < lineOff + kBytesPerLine);
      const QString addr =
        QString::asprintf("%0*llx", m_addrDigits,
                          static_cast<unsigned long long>(lineOff));
      const int ax = m_addrX - hoff;
      if (cursorLine) {
        p.fillRect(QRect(ax, y, m_addrDigits * m_charW + 2, rh), selBg);
        p.setPen(selFg);
      } else {
        p.setPen(addrFg);
      }
      p.drawText(ax, textY, addr);

      // 16 進 (単位ごと)。
      for (int u = 0; u < units; ++u) {
        const int avail = qBound(0, n - u * unitBytes, unitBytes);
        if (avail <= 0) {
          continue;
        }
        const qint64 uOff = lineOff + static_cast<qint64>(u) * unitBytes;
        const int    ux   = m_hexX + u * m_unitW - hoff;
        QString hx;
        if (avail >= unitBytes) {
          appendUnitHex(hx, buf + u * unitBytes, unitBytes, m_endian);
        } else {
          for (int k = 0; k < avail; ++k) {
            hx.append(QLatin1Char(kHexDigits[buf[u * unitBytes + k] >> 4]));
            hx.append(QLatin1Char(kHexDigits[buf[u * unitBytes + k] & 0xF]));
          }
        }
        const bool sel =
          m_hasSel && !(uOff + unitBytes - 1 < selLo || uOff > selHi);
        if (sel) {
          const int tw = fm.horizontalAdvance(hx);
          p.fillRect(QRect(ux, y, tw + 2, rh), selBg);
          p.setPen(selFg);
        } else {
          p.setPen(normFg);
        }
        p.drawText(ux + 1, textY, hx);
      }

      // ASCII (エンコーディングでデコード)。文字ごとに元バイトを対応づけ、選択
      // 範囲に重なる文字は反転表示する (16 進側とあわせて文字列側も強調)。
      QString    asc;
      QList<int> byteStart;
      decodeAsciiCells(buf, n, m_encoding, asc, byteStart);
      int ax2 = m_asciiX - hoff + 1;
      for (int j = 0; j < asc.size(); ++j) {
        const int    bs  = byteStart.at(j);
        const int    be  = (j + 1 < byteStart.size()) ? byteStart.at(j + 1) - 1 : n - 1;
        const QString one(asc.at(j));
        const int     cw = fm.horizontalAdvance(one);
        const bool    sel =
          m_hasSel && !(lineOff + be < selLo || lineOff + bs > selHi);
        if (sel) {
          p.fillRect(QRect(ax2, y, cw, rh), selBg);
          p.setPen(selFg);
        } else {
          p.setPen(normFg);
        }
        p.drawText(ax2, textY, one);
        ax2 += cw;
      }
    }
  }

  void resizeEvent(QResizeEvent* e) override {
    QAbstractScrollArea::resizeEvent(e);
    updateScrollbars();
  }

  void scrollContentsBy(int dx, int dy) override {
    if (dy != 0) {
      m_topLine = static_cast<qint64>(verticalScrollBar()->value()) * m_lineScale;
      clampTop();
    }
    Q_UNUSED(dx);
    viewport()->update();
  }

  void keyPressEvent(QKeyEvent* e) override {
    if (m_totalSize <= 0) {
      QAbstractScrollArea::keyPressEvent(e);
      return;
    }
    if (e->matches(QKeySequence::Copy)) {
      copySelectionToClipboard();
      e->accept();
      return;
    }
    const int  unitBytes = binaryViewerUnitToBytes(m_unit);
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const bool ctrl  = e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
    const int  visLines = qMax(1, viewport()->height() / rowH());
    qint64 nc = m_cursor;
    switch (e->key()) {
      case Qt::Key_Left:     nc -= unitBytes; break;
      case Qt::Key_Right:    nc += unitBytes; break;
      case Qt::Key_Up:       nc -= kBytesPerLine; break;
      case Qt::Key_Down:     nc += kBytesPerLine; break;
      case Qt::Key_PageUp:   nc -= static_cast<qint64>(visLines) * kBytesPerLine; break;
      case Qt::Key_PageDown: nc += static_cast<qint64>(visLines) * kBytesPerLine; break;
      case Qt::Key_Home:
        nc = ctrl ? 0 : (m_cursor - m_cursor % kBytesPerLine);
        break;
      case Qt::Key_End:
        nc = ctrl ? (m_totalSize - 1)
                  : (m_cursor - m_cursor % kBytesPerLine + kBytesPerLine - 1);
        break;
      default:
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }
    nc = qBound<qint64>(0, nc, m_totalSize - 1);
    nc -= nc % unitBytes;
    moveCursor(nc, shift);
    e->accept();
  }

  void mousePressEvent(QMouseEvent* e) override {
    if (e->button() != Qt::LeftButton) {
      QAbstractScrollArea::mousePressEvent(e);
      return;
    }
    setFocus();
    const qint64 b = byteAtPos(e->pos());
    if (b < 0) {
      return;
    }
    const int ub = binaryViewerUnitToBytes(m_unit);
    moveCursor(b - b % ub, e->modifiers() & Qt::ShiftModifier);
    m_dragging = true;
  }
  void mouseMoveEvent(QMouseEvent* e) override {
    if (!m_dragging) {
      return;
    }
    const qint64 b = byteAtPos(e->pos());
    if (b < 0) {
      return;
    }
    const int ub = binaryViewerUnitToBytes(m_unit);
    moveCursor(b - b % ub, true);
  }
  void mouseReleaseEvent(QMouseEvent*) override { m_dragging = false; }

  // アプリ側の Copy コマンド (ファイルパスのコピー等) より先に Ctrl/Cmd+C を
  // 受け取るため、ShortcutOverride を accept して自分に回す。
  bool event(QEvent* e) override {
    if (e->type() == QEvent::ShortcutOverride) {
      auto* ke = static_cast<QKeyEvent*>(e);
      if (ke->matches(QKeySequence::Copy)) {
        e->accept();
        return true;
      }
    }
    return QAbstractScrollArea::event(e);
  }

private:
  int rowH() const { return qMax(1, QFontMetrics(font()).height() + 2); }

  void recompute() {
    const QFontMetrics fm(font());
    m_charW = qMax(1, fm.horizontalAdvance(QLatin1Char('0')));
    // アドレス桁数: 最終オフセットが収まる桁数 (最低 8)。
    int digits = 1;
    qint64 v = m_totalSize > 0 ? m_totalSize - 1 : 0;
    while (v >= 16) {
      v >>= 4;
      ++digits;
    }
    m_addrDigits = qMax(8, digits);

    const int cellPad    = qMax(3, m_charW / 2);
    const int sectionGap = fm.averageCharWidth() * 2;
    const int unitBytes  = binaryViewerUnitToBytes(m_unit);
    const int units      = kBytesPerLine / unitBytes;

    m_addrX = 6;
    const int addrW = m_addrDigits * m_charW;
    m_hexX  = m_addrX + addrW + sectionGap;
    m_unitW = unitBytes * 2 * m_charW + cellPad;
    const int hexW = units * m_unitW;
    m_asciiX = m_hexX + hexW + sectionGap;
    m_asciiW = kBytesPerLine * m_charW + cellPad;
    m_contentW = m_asciiX + m_asciiW + 6;

    updateScrollbars();
    viewport()->update();
  }

  void updateScrollbars() {
    const int rh       = rowH();
    const int visLines = rh > 0 ? viewport()->height() / rh : 0;
    const qint64 maxTop = qMax<qint64>(0, m_totalLines - qMax(1, visLines));
    // 縦スクロールバーは int レンジ。行数が大きい場合はスケールして収める
    // (m_topLine 自体は qint64 で保持し、キーボードでは 1 行単位で動かせる)。
    m_lineScale = 1;
    while (maxTop / m_lineScale > 1000000000LL) {
      m_lineScale *= 2;
    }
    {
      QSignalBlocker b(verticalScrollBar());
      verticalScrollBar()->setRange(0, static_cast<int>(maxTop / m_lineScale));
      verticalScrollBar()->setPageStep(
        qMax(1, static_cast<int>(qMax<qint64>(1, visLines / m_lineScale))));
      verticalScrollBar()->setSingleStep(1);
      clampTop();
      verticalScrollBar()->setValue(static_cast<int>(m_topLine / m_lineScale));
    }
    const int vw   = viewport()->width();
    const int hmax = qMax(0, m_contentW - vw);
    {
      QSignalBlocker b(horizontalScrollBar());
      horizontalScrollBar()->setRange(0, hmax);
      horizontalScrollBar()->setPageStep(vw);
    }
  }

  void clampTop() {
    const int rh       = rowH();
    const int visLines = qMax(1, rh > 0 ? viewport()->height() / rh : 1);
    const qint64 maxTop = qMax<qint64>(0, m_totalLines - visLines);
    m_topLine = qBound<qint64>(0, m_topLine, maxTop);
  }
  void syncVBar() {
    QSignalBlocker b(verticalScrollBar());
    verticalScrollBar()->setValue(static_cast<int>(m_topLine / m_lineScale));
  }

  void ensureVisible(qint64 line) {
    const int visLines = qMax(1, viewport()->height() / rowH());
    if (line < m_topLine) {
      m_topLine = line;
    } else if (line >= m_topLine + visLines) {
      m_topLine = line - visLines + 1;
    }
    clampTop();
    syncVBar();
  }

  void moveCursor(qint64 off, bool extend) {
    if (extend) {
      if (!m_hasSel) {
        m_selAnchor = m_cursor;
      }
      m_hasSel = true;
    } else {
      m_hasSel = false;
      m_selAnchor = off;
    }
    m_cursor = off;
    ensureVisible(m_cursor / kBytesPerLine);
    viewport()->update();
  }

  qint64 byteAtPos(const QPoint& pos) const {
    const int rh = rowH();
    if (rh <= 0) {
      return -1;
    }
    const int    hoff = horizontalScrollBar()->value();
    const int    rx   = pos.x() + hoff;
    const qint64 line = m_topLine + pos.y() / rh;
    if (line < 0 || line >= m_totalLines) {
      return -1;
    }
    const int unitBytes = binaryViewerUnitToBytes(m_unit);
    const int units     = kBytesPerLine / unitBytes;
    qint64 off = line * kBytesPerLine;
    if (rx >= m_asciiX) {
      int ci = (rx - m_asciiX) / m_charW;
      ci = qBound(0, ci, kBytesPerLine - 1);
      off += ci;
    } else if (rx >= m_hexX) {
      int u = (rx - m_hexX) / m_unitW;
      u = qBound(0, u, units - 1);
      off += static_cast<qint64>(u) * unitBytes;
    }
    return qBound<qint64>(0, off, m_totalSize - 1);
  }

  HexDataSource m_src;
  qint64 m_totalSize  = 0;
  qint64 m_totalLines = 0;
  qint64 m_topLine    = 0;   // 画面最上行の行番号 (qint64)
  qint64 m_lineScale  = 1;   // 縦スクロールバーのスケール (超巨大ファイル用)
  qint64 m_cursor     = 0;   // カーソルのバイトオフセット
  qint64 m_selAnchor  = 0;   // 選択の起点
  bool   m_hasSel     = false;
  bool   m_dragging   = false;

  BinaryViewerUnit   m_unit     = BinaryViewerUnit::Byte1;
  BinaryViewerEndian m_endian   = BinaryViewerEndian::Little;
  QString            m_encoding = QStringLiteral("UTF-8");
  QColor m_addrColor, m_normFg, m_normBg, m_selFg, m_selBg;

  // レイアウト (px)。recompute() で算出。
  int m_charW = 8;
  int m_addrDigits = 8;
  int m_addrX = 6;
  int m_hexX = 0;
  int m_unitW = 0;
  int m_asciiX = 0;
  int m_asciiW = 0;
  int m_contentW = 0;
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
  m_unitCombo->setToolTip(tr("Unit (%1)")
    .arg(QKeySequence(Qt::CTRL | Qt::Key_1).toString(QKeySequence::NativeText)
         + QStringLiteral("..")
         + QKeySequence(Qt::CTRL | Qt::Key_4).toString(QKeySequence::NativeText)));
  toolbar->addWidget(m_unitCombo);

  toolbar->addWidget(new QLabel(tr("Endian:"), toolbar));
  m_endianCombo = new QComboBox(toolbar);
  m_endianCombo->addItem(tr("Little"), static_cast<int>(BinaryViewerEndian::Little));
  m_endianCombo->addItem(tr("Big"),    static_cast<int>(BinaryViewerEndian::Big));
  m_endianCombo->setFocusPolicy(Qt::StrongFocus);
  m_endianCombo->setToolTip(tr("Endian (%1)")
    .arg(QKeySequence(Qt::CTRL | Qt::Key_E).toString(QKeySequence::NativeText)));
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

  // ── 検索バー ──────────────────────────────────
  // 16 進 / 文字列をラジオで切替え、Enter または Next/Prev ボタンで検索する。
  QToolBar* searchBar = new QToolBar(this);
  searchBar->setMovable(false);
  searchBar->setFloatable(false);
  searchBar->setIconSize(QSize(20, 20));
  applyToolbarStyle(searchBar);

  searchBar->addWidget(new QLabel(tr("Search:"), searchBar));
  m_searchHexRadio  = new QRadioButton(tr("Hex"), searchBar);
  m_searchTextRadio = new QRadioButton(tr("Text"), searchBar);
  m_searchHexRadio->setChecked(true);
  m_searchHexRadio->setFocusPolicy(Qt::StrongFocus);
  m_searchTextRadio->setFocusPolicy(Qt::StrongFocus);
  auto* searchGroup = new QButtonGroup(this);  // 排他選択
  searchGroup->addButton(m_searchHexRadio);
  searchGroup->addButton(m_searchTextRadio);
  searchBar->addWidget(m_searchHexRadio);
  searchBar->addWidget(m_searchTextRadio);

  m_searchEdit = new QLineEdit(searchBar);
  m_searchEdit->setMinimumWidth(220);
  m_searchEdit->setClearButtonEnabled(true);
  m_searchEdit->setFocusPolicy(Qt::StrongFocus);
  searchBar->addWidget(m_searchEdit);

  m_searchEdit->setToolTip(
    tr("Search (%1 to focus)").arg(QKeySequence(QKeySequence::Find)
                                     .toString(QKeySequence::NativeText)));
  QPushButton* findPrevBtn = new QPushButton(tr("Prev"), searchBar);
  QPushButton* findNextBtn = new QPushButton(tr("Next"), searchBar);
  findPrevBtn->setFocusPolicy(Qt::StrongFocus);
  findNextBtn->setFocusPolicy(Qt::StrongFocus);
  findNextBtn->setToolTip(QKeySequence(QKeySequence::FindNext)
                            .toString(QKeySequence::NativeText));
  findPrevBtn->setToolTip(QKeySequence(QKeySequence::FindPrevious)
                            .toString(QKeySequence::NativeText));
  searchBar->addWidget(findPrevBtn);
  searchBar->addWidget(findNextBtn);

  m_searchStatus = new QLabel(searchBar);
  searchBar->addWidget(m_searchStatus);

  root->addWidget(searchBar);
  {
    auto* sf = new EnterClickFilter(this);
    sf->installOnButtonsIn(searchBar);
  }

  connect(findNextBtn, &QPushButton::clicked, this, [this] { doSearch(true); });
  connect(findPrevBtn, &QPushButton::clicked, this, [this] { doSearch(false); });
  connect(m_searchHexRadio, &QRadioButton::toggled, this,
          [this](bool) { updateSearchPlaceholder(); });
  // Enter でジャンプ / 前後検索。親へ伝播させない (ビュアーが閉じるのを防ぐ)。
  m_searchEdit->installEventFilter(this);
  updateSearchPlaceholder();

  // 16 進ダンプ本体 (自前描画の HexView)。QTableView は行番号×行高の int 座標が
  // GB 級ファイルで溢れて途中から描画されないため、見えている行だけを描く自前
  // ビューにする。Ctrl/Cmd+C は HexView 内部で ShortcutOverride を横取りして
  // 選択の 16 進コピーを行う。
  m_hex = new HexView(this);
  root->addWidget(m_hex, /*stretch*/ 1);

  // ViewerPanel / BinaryViewerWindow からの setFocus を HexView へ転送。
  setFocusProxy(m_hex);

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

  // ローカルショートカット (Cmd/Ctrl+F 等) は eventFilter で処理する。本体
  // (HexView) 上でも効くように、HexView にも event filter を張る。
  m_hex->installEventFilter(this);
}

bool BinaryView::eventFilter(QObject* watched, QEvent* event) {
  // ── ローカルショートカット (検索欄 / 本体 / アドレス欄のどれにフォーカスが
  //    あっても効く) ──
  //   Cmd/Ctrl+F          : 検索欄へフォーカス (全選択)
  //   Cmd+G / F3          : 次を検索
  //   Cmd+Shift+G / S+F3  : 前を検索
  //   Cmd/Ctrl+J          : アドレスジャンプ欄へフォーカス
  //   Cmd/Ctrl+1〜4       : 単位を 1 / 2 / 4 / 8 バイトに切替
  //   Cmd/Ctrl+E          : エンディアン切替 (Little ⇄ Big)
  // アプリ側の Copy コマンドや表示モード (Cmd+1〜4) 等より先に取りたいので、
  // ShortcutOverride を accept してから KeyPress で処理する (Copy 横取りと同じ)。
  if ((watched == m_hex || watched == m_searchEdit || watched == m_addressEdit)
      && (event->type() == QEvent::ShortcutOverride
          || event->type() == QEvent::KeyPress)) {
    auto* ke = static_cast<QKeyEvent*>(event);
    const bool find     = ke->matches(QKeySequence::Find);
    const bool findNext = ke->matches(QKeySequence::FindNext);
    const bool findPrev = ke->matches(QKeySequence::FindPrevious);
    const bool ctrlOnly = (ke->modifiers() == Qt::ControlModifier);  // mac は Cmd
    const bool toAddr       = ctrlOnly && ke->key() == Qt::Key_J;
    const bool toggleEndian = ctrlOnly && ke->key() == Qt::Key_E;
    int unitBytes = 0;
    if (ctrlOnly) {
      switch (ke->key()) {
        case Qt::Key_1: unitBytes = 1; break;
        case Qt::Key_2: unitBytes = 2; break;
        case Qt::Key_3: unitBytes = 4; break;
        case Qt::Key_4: unitBytes = 8; break;
        default: break;
      }
    }
    if (find || findNext || findPrev || toAddr || toggleEndian || unitBytes) {
      if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
      }
      if (find) {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
      } else if (findNext) {
        doSearch(true);
      } else if (findPrev) {
        doSearch(false);
      } else if (toAddr) {
        m_addressEdit->setFocus();
        m_addressEdit->selectAll();
      } else if (unitBytes) {
        // コンボの選択を変えると currentIndexChanged 経由で単位が反映される。
        for (int i = 0; i < m_unitCombo->count(); ++i) {
          if (m_unitCombo->itemData(i).toInt() == unitBytes) {
            m_unitCombo->setCurrentIndex(i);
            break;
          }
        }
      } else if (toggleEndian) {
        m_endianCombo->setCurrentIndex(m_endianCombo->currentIndex() == 0 ? 1 : 0);
      }
      return true;
    }
  }

  if (watched == m_addressEdit && event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
      jumpToAddress();
      return true;  // 親へ伝播させない (ビュアーを閉じさせない)
    }
  }
  // 検索欄の Enter で次を、Shift+Enter で前を検索する。
  if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
      doSearch(!(ke->modifiers() & Qt::ShiftModifier));
      return true;
    }
  }
  // 本体 (HexView) 上の Copy は HexView が ShortcutOverride を横取りして処理する。
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
  m_hex->applyFont(s.binaryViewerFont());
  m_hex->setColors(s.binaryViewerAddressForeground(),
                   s.binaryViewerNormalForeground(),
                   s.binaryViewerNormalBackground(),
                   s.binaryViewerSelectedForeground(),
                   s.binaryViewerSelectedBackground());

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
  m_hex->setEndian(m_endian);
  m_hex->setEncoding(m_actualEncoding);
  m_hex->setUnit(m_unit);  // 単位変更はレイアウト再計算 (unit 未変更なら no-op)
  updateSearchPlaceholder();  // 16 進検索の入力例は単位に依存する
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
  m_hex->jumpTo(addr);
  m_hex->setFocus();
}

QByteArray BinaryView::buildSearchPattern(bool& ok, QString* formattedHex) const {
  ok = false;
  const QString text = m_searchEdit->text();
  if (text.trimmed().isEmpty()) {
    return {};
  }
  if (m_searchHexRadio->isChecked()) {
    return parseHexSearchInput(text, binaryViewerUnitToBytes(m_unit), m_endian, ok,
                               formattedHex);
  }
  const QByteArray b = encodeStringColumn(text, m_actualEncoding);
  ok = !b.isEmpty();
  return b;
}

void BinaryView::doSearch(bool forward) {
  if (m_totalSize <= 0 || !m_searchStatus) {
    return;
  }
  bool ok = false;
  QString formatted;
  const QByteArray pat = buildSearchPattern(ok, &formatted);
  if (!ok || pat.isEmpty()) {
    m_searchStatus->setText(tr("Invalid input"));
    return;
  }
  // 16 進検索は、区切り無し入力 ("334455") を単位ごと ("33 44 55") に整形して
  // 入力欄へ反映する。
  if (m_searchHexRadio->isChecked() && !formatted.isEmpty()
      && formatted != m_searchEdit->text()) {
    QSignalBlocker b(m_searchEdit);
    m_searchEdit->setText(formatted);
  }
  // 前方は現在の一致 (選択) の末尾+1 から、後方は先頭-1 から。こうしないと後方
  // 検索で今ヒットしている一致を再び拾ってしまう。
  const qint64 from = forward ? m_hex->selectionEnd() + 1
                              : m_hex->selectionStart() - 1;
  qint64 found   = m_hex->find(pat, from, forward);
  bool   wrapped = false;
  if (found < 0) {
    // 端まで無ければ反対側から一周だけ探す。
    const qint64 wrapFrom = forward ? 0 : (m_totalSize - 1);
    found   = m_hex->find(pat, wrapFrom, forward);
    wrapped = (found >= 0);
  }
  if (found < 0) {
    m_searchStatus->setText(tr("Not found"));
    return;
  }
  m_hex->selectMatch(found, pat.size());
  m_searchStatus->setText(wrapped ? tr("Wrapped") : QString());
  m_hex->setFocus();
}

void BinaryView::updateSearchPlaceholder() {
  if (!m_searchEdit) {
    return;
  }
  if (m_searchHexRadio && m_searchHexRadio->isChecked()) {
    QString ex;
    switch (binaryViewerUnitToBytes(m_unit)) {
      case 1:  ex = QStringLiteral("12 34 56");           break;
      case 2:  ex = QStringLiteral("1234 5678");          break;
      case 4:  ex = QStringLiteral("12345678 9abcdef0");  break;
      case 8:  ex = QStringLiteral("0011223344556677");   break;
      default: ex = QStringLiteral("12 34");              break;
    }
    m_searchEdit->setPlaceholderText(tr("hex e.g. %1").arg(ex));
  } else {
    m_searchEdit->setPlaceholderText(tr("text to find"));
  }
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
  if (m_searchStatus) {
    m_searchStatus->clear();
  }

  // 表示フォーマットを反映してから mmap を開く (メインスレッド)。
  m_hex->setEndian(m_endian);
  m_hex->setEncoding(m_actualEncoding);
  m_hex->setUnit(m_unit);
  m_hex->openFile(r.filePath);
}

void BinaryView::clearContent() {
  m_filePath.clear();
  m_totalSize = 0;
  m_maxAddress = -1;
  if (m_addressMaxLabel) m_addressMaxLabel->clear();
  if (m_addressEdit) m_addressEdit->clear();
  if (m_hex) m_hex->closeFile();
}

QString BinaryView::statusInfo() const {
  if (m_filePath.isEmpty()) return QString();
  // GB/GiB ではなく正確なバイト数で表示する (バイナリビュアーなのでオフセットと
  // 対応が取りやすいように)。桁区切り付き。
  QString s = tr("%1 bytes").arg(QLocale(QLocale::English).toString(m_totalSize));
  if (m_encoding.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0
      && !m_actualEncoding.isEmpty()) {
    s += QStringLiteral("  ·  %1").arg(m_actualEncoding);
  }
  return s;
}

} // namespace Farman
