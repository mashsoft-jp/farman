#include "BinaryView.h"
#include "settings/Settings.h"
#include "utils/EnterClickFilter.h"

#include <QComboBox>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QToolBar>
#include <QLocale>
#include <QLabel>
#include <QPalette>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStringDecoder>
// uchardet: TextView の Auto と同じく、エンコード自動判定に使う。
// ライブラリヘッダの場所は環境によって <uchardet.h> 直下か
// <uchardet/uchardet.h> かが分かれるが、CMake で両方の include パスを
// 通しているのでシンプルな名前で参照する。
#include <uchardet.h>
#include <QSyntaxHighlighter>
#include <QTextCodec>
#include <QVBoxLayout>

namespace Farman {

// 各行先頭 8 桁の 16 進アドレスをハイライトする。色は Settings から都度取得する。
class AddressHighlighter : public QSyntaxHighlighter {
public:
  using QSyntaxHighlighter::QSyntaxHighlighter;

protected:
  void highlightBlock(const QString& text) override {
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{8}"));
    const auto m = re.match(text);
    if (!m.hasMatch()) return;
    const Settings& s = Settings::instance();
    QTextCharFormat fmt;
    fmt.setForeground(s.binaryViewerAddressForeground());
    if (s.binaryViewerAddressBackground().isValid()) {
      fmt.setBackground(s.binaryViewerAddressBackground());
    }
    setFormat(m.capturedStart(), m.capturedLength(), fmt);
  }
};

namespace {

constexpr int kBytesPerLine = 16;
constexpr char kHexDigits[] = "0123456789abcdef";

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

// "Auto" 指定時に uchardet で実エンコードを判定する。判定不能なら "UTF-8"
// にフォールバック (TextView と同じ流儀)。userEncoding が "Auto" 以外なら
// そのまま返す。
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
  // Qt6 ネイティブの QStringDecoder は UTF-* / Latin1 / System のみ。
  // Shift_JIS / EUC-JP 等は Qt5Compat の QTextCodec にフォールバック。
  QString decoded;
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

QString formatHexDump(const QByteArray& data, BinaryViewerUnit unit,
                      BinaryViewerEndian endian, const QString& encoding,
                      const std::atomic<bool>* cancelToken = nullptr) {
  const int unitBytes  = binaryViewerUnitToBytes(unit);
  const int unitsPerLine = kBytesPerLine / unitBytes;
  const int midUnit    = unitsPerLine / 2;
  const int lineCount  = (data.size() + kBytesPerLine - 1) / kBytesPerLine;

  QString out;
  out.reserve(lineCount * (8 + 2 + unitsPerLine * (unitBytes * 2 + 1) + 2 + kBytesPerLine + 1));

  // 8 MB の hex 整形は数秒オーダで走るので、256 行 (= 4 KB データ) おきに
  // cancelToken を確認して早期 return できるようにする。
  for (int line = 0; line < lineCount; ++line) {
    if (cancelToken && (line & 0xFF) == 0
        && cancelToken->load(std::memory_order_acquire)) {
      return QString();
    }
    const int off  = line * kBytesPerLine;
    const qint64 addr = static_cast<qint64>(off);

    char addrBuf[9];
    for (int i = 7; i >= 0; --i) {
      addrBuf[i] = kHexDigits[(addr >> ((7 - i) * 4)) & 0xF];
    }
    addrBuf[8] = '\0';
    out.append(QLatin1String(addrBuf, 8));
    out.append(QLatin1String("  "));

    for (int u = 0; u < unitsPerLine; ++u) {
      if (u == midUnit && midUnit > 0) {
        out.append(QLatin1Char(' '));
      }
      const int unitOff = off + u * unitBytes;
      const int avail = qMin(unitBytes, static_cast<int>(data.size()) - unitOff);
      if (avail >= unitBytes) {
        appendUnitHex(out, reinterpret_cast<const unsigned char*>(data.constData() + unitOff),
                      unitBytes, endian);
      } else if (avail > 0) {
        for (int i = 0; i < avail; ++i) {
          const unsigned char b = static_cast<unsigned char>(data[unitOff + i]);
          out.append(QLatin1Char(kHexDigits[b >> 4]));
          out.append(QLatin1Char(kHexDigits[b & 0xF]));
        }
        for (int i = avail; i < unitBytes; ++i) {
          out.append(QLatin1String("  "));
        }
      } else {
        for (int i = 0; i < unitBytes; ++i) {
          out.append(QLatin1String("  "));
        }
      }
      out.append(QLatin1Char(' '));
    }

    out.append(QLatin1Char(' '));

    const int chunkLen = qMin(kBytesPerLine, static_cast<int>(data.size()) - off);
    if (chunkLen > 0) {
      const QByteArray chunk(data.constData() + off, chunkLen);
      out.append(decodeStringColumn(chunk, encoding));
    }

    out.append(QLatin1Char('\n'));
  }

  return out;
}

} // namespace

BinaryView::BinaryView(QWidget* parent)
  : QWidget(parent)
{
  setupUi();
  syncFromSettings();

  // グローバル設定が変更されたらローカル上書きをリセット
  connect(&Settings::instance(), &Settings::settingsChanged, this, [this] {
    syncFromSettings();
    if (!m_filePath.isEmpty()) {
      render();
    }
  });
}

void BinaryView::setupUi() {
  QVBoxLayout* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // ローカル設定オーバーレイ (メインウィンドウのツールバーと同じ QToolBar)
  QToolBar* toolbar = new QToolBar(this);
  toolbar->setMovable(false);
  toolbar->setFloatable(false);
  toolbar->setIconSize(QSize(20, 20));
  // フォーカス枠 + checkable の押下状態 + ホバー。共通スタイル。
  // 現状 BinaryView のヘッダは QComboBox のみだが、将来トグルボタンが
  // 追加される可能性も見越して同じスタイルを適用しておく。
  toolbar->setStyleSheet(toolbarStyleSheet());

  toolbar->addWidget(new QLabel(tr("Unit:"), toolbar));
  m_unitCombo = new QComboBox(toolbar);
  m_unitCombo->addItem(tr("1 Byte"), 1);
  m_unitCombo->addItem(tr("2 Byte"), 2);
  m_unitCombo->addItem(tr("4 Byte"), 4);
  m_unitCombo->addItem(tr("8 Byte"), 8);
  // macOS の「キーボードナビゲーション」設定に依存せず Tab で巡回させる
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
  // TextView 同様、自由入力 (= 候補外の名前を打つ) は意味が無いので
  // editable にしない。
  m_encodingCombo->setFocusPolicy(Qt::StrongFocus);
  rebuildEncodingItems();
  toolbar->addWidget(m_encodingCombo);

  root->addWidget(toolbar);

  // ヘッダにボタンが増えたとき用に、QAbstractButton 子孫で Enter 押下を
  // クリック扱いにするフィルタを install しておく (現状 QComboBox のみなので
  // 実質 no-op だが、UI を増やしたときに毎回書き直さないよう先に配線する)。
  auto* clickFilter = new EnterClickFilter(this);
  clickFilter->installOnButtonsIn(toolbar);

  // 16 進ダンプ本体 (フォントは syncFromSettings で適用)
  m_textArea = new QPlainTextEdit(this);
  m_textArea->setReadOnly(true);
  m_textArea->setLineWrapMode(QPlainTextEdit::NoWrap);
  // 行頭 8 桁のアドレスを別色で塗るシンタックスハイライタ
  m_addressHighlighter = new AddressHighlighter(m_textArea->document());
  root->addWidget(m_textArea, /*stretch*/ 1);

  // ViewerPanel / BinaryViewerWindow からは BinaryView 全体に setFocus される。
  // それを本体のテキストエリアに転送する。これがないと、子ウィジェット作成順
  // 通りに最初のコンボ (Unit) にフォーカスが入ってしまう。
  // (TextView / ImageView も同様に setFocusProxy で本体ウィジェットへ委譲)
  setFocusProxy(m_textArea);

  // ローカルでの変更は Settings に保存しない (render のみ)
  connect(m_unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
    m_unit = bytesToBinaryViewerUnit(m_unitCombo->currentData().toInt());
    if (!m_filePath.isEmpty()) render();
  });
  connect(m_endianCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
    m_endian = static_cast<BinaryViewerEndian>(m_endianCombo->currentData().toInt());
    if (!m_filePath.isEmpty()) render();
  });
  connect(m_encodingCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;
    m_encoding = trimmed;
    if (!m_filePath.isEmpty()) render();
  });
}

void BinaryView::rebuildEncodingItems() {
  m_encodingCombo->clear();
  // "Auto" は uchardet で自動判定 (TextView と同じ仕組み)。判別不能時は
  // UTF-8 にフォールバック。
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
  m_textArea->setFont(s.binaryViewerFont());

  // 通常 / 選択カラーは QPalette 経由で適用
  QPalette pal = m_textArea->palette();
  pal.setColor(QPalette::Text, s.binaryViewerNormalForeground());
  if (s.binaryViewerNormalBackground().isValid()) {
    pal.setColor(QPalette::Base, s.binaryViewerNormalBackground());
  }
  pal.setColor(QPalette::HighlightedText, s.binaryViewerSelectedForeground());
  pal.setColor(QPalette::Highlight,       s.binaryViewerSelectedBackground());
  m_textArea->setPalette(pal);

  // アドレス列の色は AddressHighlighter が描画時に Settings から直接読むので
  // 明示的に再ハイライトを要求して反映させる
  if (m_addressHighlighter) m_addressHighlighter->rehighlight();

  // コンボの再選択 (シグナルを抑止して二重 render を防ぐ)
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
}

bool BinaryView::loadFile(const QString& filePath) {
  // 同期ロード経路: prepareLoad + applyPreparedLoad を続けて呼ぶだけ。
  // 非同期化したい呼び出し元 (ViewerPanel) は両者を別スレッドに振り分ける。
  PreparedLoad p = prepareLoad(filePath, m_unit, m_endian, m_encoding);
  if (!p.ok) return false;
  applyPreparedLoad(p);
  return true;
}

BinaryView::PreparedLoad BinaryView::prepareLoad(const QString&     filePath,
                                                 BinaryViewerUnit   unit,
                                                 BinaryViewerEndian endian,
                                                 const QString&     encoding,
                                                 const std::atomic<bool>* cancelToken,
                                                 qint64             maxBytes) {
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
  // 上限: 引数 maxBytes > 0 ならそれ、未指定なら内蔵 kMaxBytes (8 MB)。
  const qint64 effectiveMax = (maxBytes > 0) ? maxBytes : kMaxBytes;
  r.totalSize  = file.size();
  r.loadedSize = qMin<qint64>(r.totalSize, effectiveMax);
  r.data       = file.read(r.loadedSize);
  file.close();

  if (cancelled()) return r;

  // "Auto" 指定なら uchardet で実エンコードを判定。それ以外は素通し。
  r.actualEncoding = resolveEncoding(r.data, encoding);

  if (cancelled()) return r;

  // 重い hex 整形をワーカースレッドで先に済ませる。actualEncoding を使う。
  // formatHexDump は cancelToken を見ながら 256 行おきに途中で中断できる。
  r.text = formatHexDump(r.data, unit, endian, r.actualEncoding, cancelToken);

  if (cancelled()) {
    // キャンセルされた場合のみ未完成 (ok=false) のまま返す。
    return r;
  }
  // 0 バイトファイル等で r.text が空でも正常。空のまま ok=true で表示する。

  if (r.totalSize > r.loadedSize) {
    r.text.append(QStringLiteral("...\n[truncated: showing first %1 of %2 bytes]\n")
                    .arg(r.loadedSize)
                    .arg(r.totalSize));
  }
  r.ok = true;
  return r;
}

void BinaryView::applyPreparedLoad(const PreparedLoad& r) {
  m_filePath        = r.filePath;
  m_data            = r.data;
  m_totalSize       = r.totalSize;
  m_loadedSize      = r.loadedSize;
  m_actualEncoding  = r.actualEncoding;

  // ハイライタを一旦切り離す。さもないと setPlainText の contentsChange を
  // 起点に全行 (8MB なら ~500K 行) で highlightBlock が同期実行され、
  // メインスレッドが長時間ブロックされて (プログレスバーが止まって) しまう。
  // 流し込みが終わったあと再アタッチすれば、可視ブロックだけが再ハイライト
  // されるので大幅に短縮できる。
  if (m_addressHighlighter) {
    m_addressHighlighter->setDocument(nullptr);
  }
  m_textArea->setPlainText(r.text);
  if (m_addressHighlighter) {
    m_addressHighlighter->setDocument(m_textArea->document());
  }
  m_textArea->moveCursor(QTextCursor::Start);
}

void BinaryView::clearContent() {
  m_filePath.clear();
  m_data.clear();
  m_totalSize  = 0;
  m_loadedSize = 0;
  m_textArea->clear();
}

QString BinaryView::statusInfo() const {
  if (m_filePath.isEmpty()) return QString();
  QString s = QLocale(QLocale::English).formattedDataSize(m_totalSize);
  if (m_totalSize > m_loadedSize) {
    s += tr("  ·  truncated to first %1")
           .arg(QLocale(QLocale::English).formattedDataSize(m_loadedSize));
  }
  // Auto 検出のときに何と判定されたかを末尾に表示する (TextView と同じ流儀)。
  // 具体名を選んでいる場合は冗長なので出さない。
  if (m_encoding.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0
      && !m_actualEncoding.isEmpty()) {
    s += QStringLiteral("  ·  %1").arg(m_actualEncoding);
  }
  return s;
}

void BinaryView::render() {
  // "Auto" のときは毎回 uchardet で再判定する。データが同じでも、
  // unit/endian の変更で再 render が走るときに encoding を再評価しても
  // 結果は同じだが、コストは小さく明示的で分かりやすい。
  m_actualEncoding = resolveEncoding(m_data, m_encoding);
  QString text = formatHexDump(m_data, m_unit, m_endian, m_actualEncoding);
  if (m_totalSize > m_loadedSize) {
    text.append(QStringLiteral("...\n[truncated: showing first %1 of %2 bytes]\n")
                  .arg(m_loadedSize)
                  .arg(m_totalSize));
  }
  m_textArea->setPlainText(text);
  m_textArea->moveCursor(QTextCursor::Start);
}

} // namespace Farman
