#include "TextView.h"
#include "settings/Settings.h"
#include "viewer/ViewerShortcutMap.h"
#include "viewer/ViewerHints.h"
#include "keybinding/ViewerCommands.h"

#include <QToolBar>
#include <QToolButton>
#include "utils/EnterClickFilter.h"
#include <QComboBox>
#include <QFile>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QApplication>
#include <QSignalBlocker>
#include <QLocale>
#include <QStringDecoder>
#include <QTextEdit>
// uchardet のインストール先によって <uchardet.h> 直下にあるか
// <uchardet/uchardet.h> にあるかが分かれる。CMake 側で両方の include パスを
// 通しているため、ここではシンプルな名前で参照する。
#include <uchardet.h>
#include <QTextBlock>
#include <QTextCodec>
#include <QVBoxLayout>

namespace Farman {

// 行番号付きの読み取り専用エディタ本体。TextView から駆動される。
class TextEditArea : public QPlainTextEdit {
public:
  explicit TextEditArea(TextView* host)
    : QPlainTextEdit(host)
    , m_host(host)
  {
    setReadOnly(true);
    m_lineNumberArea = new QWidget(this);
    m_lineNumberArea->installEventFilter(nullptr);  // 自前 paintEvent は emit
    m_lineNumberArea->setAttribute(Qt::WA_OpaquePaintEvent);

    connect(this, &QPlainTextEdit::blockCountChanged,
            this, [this](int) { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
            [this](const QRect& rect, int dy) {
              if (dy) {
                m_lineNumberArea->scroll(0, dy);
              } else {
                m_lineNumberArea->update(0, rect.y(),
                                          m_lineNumberArea->width(), rect.height());
              }
            });

    // 行番号エリアは小ウィジェットでイベントフィルタ経由で paint する
    m_lineNumberArea->installEventFilter(this);
    updateLineNumberAreaWidth();
  }

  // TextView::syncFromSettings から呼ばれる。エディタ全体の見た目を再適用する。
  void applyAppearance() {
    const Settings& s = Settings::instance();
    setFont(s.textViewerFont());

    // 本体が適用済みの qApp テーマパレットを起点にし、明示指定 (有効色) の role
    // だけ上書きする。widget の現在パレット (palette()) を起点にすると、前回
    // setPalette した明示値が残り、未設定 (無効色) の role がテーマ変更に追従
    // しない (ライト→ダーク→ライトで背景がダークのまま等) 不具合になる。
    QPalette pal = QApplication::palette();
    if (s.textViewerNormalForeground().isValid())
      pal.setColor(QPalette::Text, s.textViewerNormalForeground());
    if (s.textViewerNormalBackground().isValid())
      pal.setColor(QPalette::Base, s.textViewerNormalBackground());
    if (s.textViewerSelectedForeground().isValid())
      pal.setColor(QPalette::HighlightedText, s.textViewerSelectedForeground());
    if (s.textViewerSelectedBackground().isValid())
      pal.setColor(QPalette::Highlight, s.textViewerSelectedBackground());
    setPalette(pal);
  }

  // ローカルで設定変更 (line numbers / word wrap) 後に呼ばれる。
  void applyLocalLayout(bool wordWrap, bool showLineNumbers) {
    setLineWrapMode(wordWrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    m_lineNumberArea->setVisible(showLineNumbers);
    updateLineNumberAreaWidth();
    m_lineNumberArea->update();
  }

  int lineNumberAreaWidth() const {
    if (!m_host->showLineNumbers()) return 0;
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    return 8 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + 8;
  }

protected:
  void resizeEvent(QResizeEvent* event) override {
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(),
                                         lineNumberAreaWidth(), cr.height()));
  }

  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == m_lineNumberArea && event->type() == QEvent::Paint) {
      paintLineNumbers(static_cast<QPaintEvent*>(event));
      return true;
    }
    return QPlainTextEdit::eventFilter(watched, event);
  }

private:
  void updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
  }

  void paintLineNumbers(QPaintEvent* event) {
    if (!m_host->showLineNumbers()) return;

    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), m_host->lineNumberBackground());

    QTextBlock block = firstVisibleBlock();
    int blockNumber  = block.blockNumber();
    int top          = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom       = top + qRound(blockBoundingRect(block).height());

    painter.setPen(m_host->lineNumberForeground());

    while (block.isValid() && top <= event->rect().bottom()) {
      if (block.isVisible() && bottom >= event->rect().top()) {
        const QString num = QString::number(blockNumber + 1);
        painter.drawText(0, top, m_lineNumberArea->width() - 6,
                         fontMetrics().height(), Qt::AlignRight, num);
      }
      block       = block.next();
      top         = bottom;
      bottom      = top + qRound(blockBoundingRect(block).height());
      ++blockNumber;
    }
  }

  TextView* m_host;
  QWidget*  m_lineNumberArea;
};

// ---------- TextView ----------

TextView::TextView(QWidget* parent)
  : QWidget(parent)
{
  setupUi();
  syncFromSettings();
  applyEditAreaSettings();

  connect(&Settings::instance(), &Settings::settingsChanged, this, [this] {
    syncFromSettings();
    applyEditAreaSettings();
    if (!m_data.isEmpty()) {
      reloadFromBuffer();
    }
  });
}

void TextView::setupUi() {
  QVBoxLayout* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // ツールバー (メインウィンドウのツールバーと同じ QToolBar)
  QToolBar* toolbar = new QToolBar(this);
  toolbar->setMovable(false);
  toolbar->setFloatable(false);
  toolbar->setIconSize(QSize(20, 20));
  // フォーカス枠 + checkable の押下状態 + ホバー。共通スタイル。
  applyToolbarStyle(toolbar);

  // ショートカット表記は現在の割り当てに追随させる (ViewerHints)。tag しておくと
  // applyShortcutBindings → refresh で再割り当て時に自動更新される。
  QLabel* encodingLabel = new QLabel(tr("Encoding:"), toolbar);
  ViewerHints::tag(encodingLabel, QStringLiteral("viewer.text.encoding_focus"),
    tr("Character encoding used to decode this file (%1)"));
  toolbar->addWidget(encodingLabel);
  m_encodingCombo = new QComboBox(toolbar);
  ViewerHints::tag(m_encodingCombo, QStringLiteral("viewer.text.encoding_focus"),
    tr("Character encoding used to decode this file, Auto detects automatically (%1)"));
  // 自由入力できても認識されない名前は意味が無く、誤入力で読み込み失敗する
  // だけなので、選択リストからの固定候補に限定する。
  m_encodingCombo->addItem(QStringLiteral("Auto"));
  m_encodingCombo->addItem(QStringLiteral("UTF-8"));
  m_encodingCombo->addItem(QStringLiteral("UTF-16LE"));
  m_encodingCombo->addItem(QStringLiteral("UTF-16BE"));
  m_encodingCombo->addItem(QStringLiteral("Shift_JIS"));
  m_encodingCombo->addItem(QStringLiteral("EUC-JP"));
  m_encodingCombo->addItem(QStringLiteral("ISO-8859-1"));
  m_encodingCombo->setFocusPolicy(Qt::StrongFocus);
  toolbar->addWidget(m_encodingCombo);

  // 行番号 / ワードラップは ON/OFF のトグル。アイコン QToolButton に変更
  // (メインツールバー / 画像ビュアーの統一感のため)。
  m_lineNumbersButton = new QToolButton(toolbar);
  m_lineNumbersButton->setCheckable(true);
  m_lineNumbersButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/line-numbers.svg")));
  ViewerHints::tag(m_lineNumbersButton, QStringLiteral("viewer.text.toggle_line_numbers"),
    tr("Toggle line numbers in the left margin (%1)"));
  m_lineNumbersButton->setFocusPolicy(Qt::StrongFocus);
  toolbar->addWidget(m_lineNumbersButton);

  m_wordWrapButton = new QToolButton(toolbar);
  m_wordWrapButton->setCheckable(true);
  m_wordWrapButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/word-wrap.svg")));
  ViewerHints::tag(m_wordWrapButton, QStringLiteral("viewer.text.toggle_word_wrap"),
    tr("Toggle word wrap for long lines (%1)"));
  m_wordWrapButton->setFocusPolicy(Qt::StrongFocus);
  toolbar->addWidget(m_wordWrapButton);

  // 検索コントロール群 (常設)。Tab で順に辿れる位置に置く。
  toolbar->addSeparator();

  QLabel* findLabel = new QLabel(tr("Find:"), toolbar);
  ViewerHints::tag(findLabel, QStringLiteral("viewer.text.find_focus"),
    tr("Find text (%1 to focus, Enter=next, Shift+Enter=prev, Esc=back)"));
  toolbar->addWidget(findLabel);

  m_findEdit = new QLineEdit(toolbar);
  ViewerHints::tag(m_findEdit, QStringLiteral("viewer.text.find_focus"),
    tr("Search text  (%1)"), /*placeholder=*/true);
  ViewerHints::tag(m_findEdit, QStringLiteral("viewer.text.find_focus"),
    tr("Find text (%1 to focus, Enter=next, Shift+Enter=prev, Esc=back)"));
  m_findEdit->setClearButtonEnabled(true);
  m_findEdit->setFocusPolicy(Qt::StrongFocus);
  m_findEdit->setMinimumWidth(180);
  // QToolBar には addWidget の stretch が無いので、ウィジェット側のサイズ
  // ポリシーで横に伸びるよう指示する。
  m_findEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_findEdit->installEventFilter(this);
  toolbar->addWidget(m_findEdit);

  // 大文字小文字区別もアイコンのトグル化 (Aa)。
  m_findCsButton = new QToolButton(toolbar);
  m_findCsButton->setCheckable(true);
  m_findCsButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/case-sensitive.svg")));
  ViewerHints::tag(m_findCsButton, QStringLiteral("viewer.text.toggle_case"),
    tr("Toggle case-sensitive search (%1)"));
  m_findCsButton->setFocusPolicy(Qt::StrongFocus);
  toolbar->addWidget(m_findCsButton);

  m_findStatus = new QLabel(toolbar);
  m_findStatus->setMinimumWidth(80);
  toolbar->addWidget(m_findStatus);

  root->addWidget(toolbar);

  // フォーカス中ボタンで Enter を押されたら、そのボタンを click 扱いにする。
  // 検索 QLineEdit には別途 eventFilter が install されているのでこちらの
  // フィルタは届かない (= 検索の Enter 動作は壊さない)。
  auto* clickFilter = new EnterClickFilter(this);
  clickFilter->installOnButtonsIn(toolbar);

  // 本体
  m_editArea = new TextEditArea(this);
  root->addWidget(m_editArea, /*stretch*/ 1);
  // ViewerPanel など外側から setFocus されたとき、フォーカスを本体に流す
  setFocusProxy(m_editArea);

  // 検索イベント結線
  // - Enter: 次のヒットへ移動 + 全件ハイライト更新 (eventFilter で処理)
  // - Shift+Enter: 前のヒットへ (eventFilter で処理)
  // - Esc: フォーカスを本体に戻す (eventFilter で処理)
  // - 入力テキストが変わったらハイライトはクリア (Enter で再走査される)
  connect(m_findEdit, &QLineEdit::textChanged, this, [this](const QString&) {
    if (m_editArea) m_editArea->setExtraSelections({});
    if (m_findStatus) m_findStatus->clear();
  });
  connect(m_findCsButton, &QToolButton::toggled, this, [this](bool) {
    if (!m_findEdit || m_findEdit->text().isEmpty()) return;
    refreshMatchHighlights();
  });

  // Cmd/Ctrl + F: QApplication レベルの eventFilter で確実に拾う。
  // QShortcut / QAction では上位で潰されるケースがあるため、最終的に
  // ここで保険をかける。TextView が表示されている (isVisible) ときだけ
  // 検索入力欄にフォーカスを移す。
  qApp->installEventFilter(this);

  // ローカル変更: Settings には保存しない
  connect(m_encodingCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;
    m_encoding = trimmed;
    if (!m_data.isEmpty()) reloadFromBuffer();
  });
  connect(m_lineNumbersButton, &QToolButton::toggled, this, [this](bool on) {
    m_showLineNumbers = on;
    applyEditAreaSettings();
  });
  connect(m_wordWrapButton, &QToolButton::toggled, this, [this](bool on) {
    m_wordWrap = on;
    applyEditAreaSettings();
  });
}

void TextView::syncFromSettings() {
  const Settings& s = Settings::instance();
  m_encoding        = s.textViewerEncoding();
  m_showLineNumbers = s.textViewerShowLineNumbers();
  m_wordWrap        = s.textViewerWordWrap();

  {
    QSignalBlocker b(m_encodingCombo);
    m_encodingCombo->setCurrentText(m_encoding);
  }
  {
    QSignalBlocker b(m_lineNumbersButton);
    m_lineNumbersButton->setChecked(m_showLineNumbers);
  }
  {
    QSignalBlocker b(m_wordWrapButton);
    m_wordWrapButton->setChecked(m_wordWrap);
  }
}

void TextView::applyEditAreaSettings() {
  if (!m_editArea) return;
  m_editArea->applyAppearance();
  m_editArea->applyLocalLayout(m_wordWrap, m_showLineNumbers);
}

bool TextView::loadFile(const QString& filePath) {
  // 同期ロード経路: prepareLoad + applyPreparedLoad を続けて呼ぶだけ。
  // 非同期化したい呼び出し元 (ViewerPanel) は両者を別スレッドに振り分ける。
  PreparedLoad p = prepareLoad(filePath, m_encoding);
  if (!p.ok) return false;
  applyPreparedLoad(p);
  return true;
}

TextView::PreparedLoad TextView::prepareLoad(const QString& filePath,
                                             const QString& userEncoding,
                                             const std::atomic<bool>* cancelToken,
                                             qint64 maxBytes) {
  PreparedLoad r;
  r.filePath = filePath;

  // 各ステージの合間に cancelToken をチェックして早期 return する。
  // file.readAll() / decode の最中は中断できないが、開始前にチェックすれば
  // 「カーソル連打で生成されたジョブが順番待ちでスタックしているところを
  // まとめてスキップさせる」効果が得られる。
  auto cancelled = [&]() {
    return cancelToken && cancelToken->load(std::memory_order_acquire);
  };

  if (cancelled()) return r;

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return r;
  }
  r.totalSize = file.size();
  if (maxBytes > 0 && r.totalSize > maxBytes) {
    // 先頭 maxBytes バイトだけ読む。マルチバイト文字の途中で切れるおそれは
    // あるが QStringDecoder は replacement char に置き換えてくれるので致命傷
    // にはならない。プレビュー用途なので末尾 1〜2 文字が乱れる程度は許容する。
    r.data       = file.read(maxBytes);
    r.loadedSize = r.data.size();
  } else {
    r.data       = file.readAll();
    r.loadedSize = r.data.size();
  }
  file.close();

  if (cancelled()) return r;

  if (userEncoding.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0) {
    uchardet_t det = uchardet_new();
    QString detected;
    if (det) {
      if (uchardet_handle_data(det, r.data.constData(),
                               static_cast<size_t>(r.data.size())) == 0) {
        uchardet_data_end(det);
        detected = QString::fromLatin1(uchardet_get_charset(det));
      }
      uchardet_delete(det);
    }
    r.actualEncoding = detected.isEmpty() ? QStringLiteral("UTF-8") : detected;
  } else {
    r.actualEncoding = userEncoding;
  }

  if (cancelled()) return r;

  r.text = decodeBytes(r.data, r.actualEncoding);

  if (cancelled()) return r;

  // truncate された場合は末尾に注記を入れる (プレビューで「先頭だけ」と
  // ユーザーに伝えるため)。
  if (r.totalSize > r.loadedSize) {
    r.text.append(QStringLiteral("\n\n...\n[truncated: showing first %1 of %2 bytes]\n")
                    .arg(r.loadedSize)
                    .arg(r.totalSize));
  }

  r.ok = true;
  return r;
}

void TextView::applyPreparedLoad(const PreparedLoad& r) {
  m_filePath       = r.filePath;
  m_data           = r.data;
  m_actualEncoding = r.actualEncoding;
  m_editArea->setPlainText(r.text);
  m_editArea->moveCursor(QTextCursor::Start);
}

void TextView::clearContent() {
  m_filePath.clear();
  m_data.clear();
  m_editArea->clear();
}

void TextView::reloadFromBuffer() {
  // m_encoding が "Auto" のときだけ uchardet で検出。それ以外はそのまま
  // ユーザーが選んだ具体名を使う。検出失敗時 (ファイルが小さすぎる等) は
  // UTF-8 を仮定する。
  if (m_encoding.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0) {
    uchardet_t det = uchardet_new();
    QString detected;
    if (det) {
      if (uchardet_handle_data(det, m_data.constData(),
                               static_cast<size_t>(m_data.size())) == 0) {
        uchardet_data_end(det);
        detected = QString::fromLatin1(uchardet_get_charset(det));
      }
      uchardet_delete(det);
    }
    m_actualEncoding = detected.isEmpty() ? QStringLiteral("UTF-8")
                                          : detected;
  } else {
    m_actualEncoding = m_encoding;
  }
  const QString text = decodeBytes(m_data, m_actualEncoding);
  m_editArea->setPlainText(text);
  m_editArea->moveCursor(QTextCursor::Start);
}

QString TextView::decodeBytes(const QByteArray& data, const QString& encoding) {
  QStringDecoder decoder(encoding.toUtf8().constData());
  if (decoder.isValid()) {
    return decoder.decode(data);
  }
  if (QTextCodec* codec = QTextCodec::codecForName(encoding.toUtf8())) {
    return codec->toUnicode(data);
  }
  return QStringDecoder(QStringDecoder::Utf8).decode(data);
}

QColor TextView::lineNumberForeground() const {
  return Settings::instance().textViewerLineNumberForeground();
}

QColor TextView::lineNumberBackground() const {
  return Settings::instance().textViewerLineNumberBackground();
}

// ---------- 検索 ----------

void TextView::focusFindInput() {
  if (!m_findEdit) return;
  // 現在の選択テキストがあれば検索ボックスに入れる
  if (m_editArea) {
    const QString sel = m_editArea->textCursor().selectedText();
    // QPlainTextEdit::selectedText は段落区切りに U+2029 を含むので、
    // 単一行の選択のときだけ採用する。
    if (!sel.isEmpty() && !sel.contains(QChar(0x2029))) {
      m_findEdit->setText(sel);
    }
  }
  m_findEdit->setFocus(Qt::ShortcutFocusReason);
  m_findEdit->selectAll();
}

void TextView::findNext() {
  findInDirection(false);
}

void TextView::findPrevious() {
  findInDirection(true);
}

void TextView::findInDirection(bool backward) {
  if (!m_editArea || !m_findEdit) return;
  const QString needle = m_findEdit->text();
  if (needle.isEmpty()) {
    m_editArea->setExtraSelections({});
    if (m_findStatus) m_findStatus->clear();
    return;
  }

  // 全件ハイライトを更新 (件数 / Not found の表示もここで行う)
  refreshMatchHighlights();

  QTextDocument::FindFlags flags;
  if (backward) flags |= QTextDocument::FindBackward;
  if (m_findCsButton && m_findCsButton->isChecked()) {
    flags |= QTextDocument::FindCaseSensitively;
  }

  bool found = m_editArea->find(needle, flags);
  if (!found) {
    // 末尾 (or 先頭) まで来たので、反対端から検索しなおす
    QTextCursor cur = m_editArea->textCursor();
    cur.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    m_editArea->setTextCursor(cur);
    found = m_editArea->find(needle, flags);
    // ステータスは件数表示を優先する。Not found は refreshMatchHighlights が
    // すでに埋めているのでそのまま。
  }
}

void TextView::refreshMatchHighlights() {
  if (!m_editArea || !m_findEdit) return;
  const QString needle = m_findEdit->text();
  QList<QTextEdit::ExtraSelection> selections;
  if (needle.isEmpty()) {
    m_editArea->setExtraSelections(selections);
    if (m_findStatus) m_findStatus->clear();
    return;
  }

  QTextDocument::FindFlags flags;
  if (m_findCsButton && m_findCsButton->isChecked()) {
    flags |= QTextDocument::FindCaseSensitively;
  }

  QTextCharFormat fmt;
  fmt.setBackground(QColor(255, 235, 100));   // 黄色系のハイライト
  fmt.setForeground(QColor(0, 0, 0));

  QTextDocument* doc = m_editArea->document();
  QTextCursor cur(doc);
  while (true) {
    cur = doc->find(needle, cur, flags);
    if (cur.isNull()) break;
    QTextEdit::ExtraSelection sel;
    sel.cursor = cur;
    sel.format = fmt;
    selections.append(sel);
  }
  m_editArea->setExtraSelections(selections);
  if (m_findStatus) {
    if (selections.isEmpty()) {
      m_findStatus->setText(tr("Not found"));
    } else {
      m_findStatus->setText(tr("%1 matches").arg(selections.size()));
    }
  }
}

bool TextView::eventFilter(QObject* watched, QEvent* event) {
  // 検索入力欄の Esc / Enter / Shift+Enter
  if (watched == m_findEdit && event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Escape) {
      if (m_editArea) m_editArea->setFocus(Qt::OtherFocusReason);
      return true;
    }
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
      if (ke->modifiers() & Qt::ShiftModifier) {
        findPrevious();
      } else {
        findNext();
      }
      return true;
    }
  }

  // ビュアーのショートカット (設定で再割り当て可能) を QApplication レベルで捕捉。
  // QShortcut / QAction では上位レイヤ (macOS のメニュー、QPlainTextEdit、
  // 他のショートカット) と衝突して発火しないケースがあるため、最終手段として
  // アプリ全体のイベントフィルタで受ける。TextView が画面に出ている (= ビュアーが
  // 表示されている) ときだけ反応する。ShortcutOverride も accept しないと
  // QPlainTextEdit 等の標準処理が先に走る可能性があるので、両方とも食う。
  if (event->type() == QEvent::ShortcutOverride
      || event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    const QKeySequence seq = ViewerShortcutMap::sequenceForEvent(ke);
    const QString cmd =
      m_shortcuts.commandForSeq(seq);
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

void TextView::applyShortcutBindings(const QVariantMap& bindings) {
  m_shortcuts.applyBindings(bindings);
  ViewerHints::refresh(this, m_shortcuts);  // ツールチップ / プレースホルダを追随
}

void TextView::dispatchViewerCommand(const QString& cmd) {
  if (cmd == QLatin1String("viewer.text.find_focus")) {
    focusFindInput();
  } else if (cmd == QLatin1String("viewer.text.encoding_focus")) {
    if (m_encodingCombo) m_encodingCombo->setFocus(Qt::ShortcutFocusReason);
  } else if (cmd == QLatin1String("viewer.text.toggle_line_numbers")) {
    if (m_lineNumbersButton) m_lineNumbersButton->toggle();
  } else if (cmd == QLatin1String("viewer.text.toggle_word_wrap")) {
    if (m_wordWrapButton) m_wordWrapButton->toggle();
  } else if (cmd == QLatin1String("viewer.text.toggle_case")) {
    if (m_findCsButton) m_findCsButton->toggle();
  } else if (cmd == QLatin1String("viewer.text.copy")) {
    // コピーはフォーカスのあるウィジェットから行う (検索欄にフォーカスがある間は
    // 検索文字列を、それ以外は本文の選択をコピー)。app 全体フィルタで捕捉するため
    // 文脈に応じて対象を切り替える。
    QWidget* fw = QApplication::focusWidget();
    if (m_findEdit && fw == m_findEdit) {
      m_findEdit->copy();
    } else if (m_editArea) {
      m_editArea->copy();
    }
  }
}

// ---------- ステータス ----------

QString TextView::statusInfo() const {
  if (m_filePath.isEmpty()) return QString();
  const int lines = m_editArea ? m_editArea->blockCount() : 0;
  // ユーザーが Auto を選んでいる場合は実際の検出結果を見せる:
  //   "UTF-8 (auto)  ·  5.4 KB  ·  123 lines"
  const bool isAuto = (m_encoding.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0);
  const QString encStr = isAuto
    ? QStringLiteral("%1 (auto)").arg(m_actualEncoding)
    : m_actualEncoding;
  return QStringLiteral("%1  ·  %2  ·  %3 lines")
    .arg(encStr)
    .arg(QLocale(QLocale::English).formattedDataSize(static_cast<qint64>(m_data.size())))
    .arg(lines);
}

} // namespace Farman
