#include "MarkdownView.h"

#include "settings/Settings.h"
#include "utils/EnterClickFilter.h"

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStringDecoder>
#include <QStringList>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

// uchardet のインストール先によって <uchardet.h> 直下にあるか
// <uchardet/uchardet.h> にあるかが分かれる。CMake 側で両方の include パスを
// 通しているので、ここではよりポータブルな <uchardet.h> を使う。
#include <uchardet.h>

namespace Farman {

namespace {
QString decodeBytes(const QByteArray& data, const QString& encoding) {
  QStringDecoder decoder(encoding.toUtf8().constData());
  if (decoder.isValid()) {
    return decoder.decode(data);
  }
  return QString::fromUtf8(data);  // フォールバック
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
} // namespace

MarkdownView::MarkdownView(QWidget* parent) : QWidget(parent) {
  setupUi();
}

void MarkdownView::setupUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  m_toolbar = new QToolBar(this);
  m_toolbar->setMovable(false);
  m_toolbar->setFloatable(false);
  m_toolbar->setIconSize(QSize(20, 20));
  m_toolbar->setStyleSheet(toolbarStyleSheet());

  // 「ソース表示」トグル: ON = 生 Markdown (= テキストエディタ風)、
  //                      OFF = 整形表示 (= QTextDocument::setMarkdown)。
  // アイコンは流用が無いので簡易にラベル表示。
  // 既定の表示モード (Settings → Plugins → Markdown Viewer)。connect 前に
  // 設定するので slot は走らない。実描画は loadFile → renderCurrent が行う。
  m_rawSource = Settings::instance().markdownViewerShowSource();

  m_rawSourceButton = new QToolButton(m_toolbar);
  m_rawSourceButton->setText(tr("Source"));
  m_rawSourceButton->setCheckable(true);
  m_rawSourceButton->setChecked(m_rawSource);
  m_rawSourceButton->setToolTip(
    tr("Show raw Markdown source (off = rendered HTML)"));
  m_rawSourceButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_rawSourceButton, &QToolButton::toggled,
          this,              &MarkdownView::onToggleRawSource);
  m_toolbar->addWidget(m_rawSourceButton);

  m_toolbar->addSeparator();

  // ───── 検索 (TextView と同じレイアウト) ─────
  m_toolbar->addWidget(new QLabel(tr("Find:"), m_toolbar));

  const QString findShortcutText =
    QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText);

  m_findEdit = new QLineEdit(m_toolbar);
  m_findEdit->setPlaceholderText(tr("Search text  (%1)").arg(findShortcutText));
  m_findEdit->setToolTip(tr("Search text in this Markdown (%1)").arg(findShortcutText));
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
  connect(m_findPrevBtn, &QToolButton::clicked,
          this,           &MarkdownView::findPrevious);
  m_toolbar->addWidget(m_findPrevBtn);

  m_findNextBtn = new QToolButton(m_toolbar);
  m_findNextBtn->setText(QStringLiteral("▶"));
  m_findNextBtn->setToolTip(tr("Next match (Enter)"));
  m_findNextBtn->setFocusPolicy(Qt::StrongFocus);
  connect(m_findNextBtn, &QToolButton::clicked,
          this,           &MarkdownView::findNext);
  m_toolbar->addWidget(m_findNextBtn);

  // 大文字小文字区別トグル
  m_findCsButton = new QToolButton(m_toolbar);
  m_findCsButton->setCheckable(true);
  m_findCsButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/case-sensitive.svg")));
  m_findCsButton->setToolTip(tr("Case sensitive search"));
  m_findCsButton->setFocusPolicy(Qt::StrongFocus);
  m_toolbar->addWidget(m_findCsButton);

  m_findStatus = new QLabel(m_toolbar);
  m_findStatus->setMinimumWidth(80);
  m_toolbar->addWidget(m_findStatus);

  connect(m_findEdit, &QLineEdit::textChanged, this, [this](const QString&) {
    if (m_browser) m_browser->setExtraSelections({});
    if (m_findStatus) m_findStatus->clear();
  });
  connect(m_findCsButton, &QToolButton::toggled, this, [this](bool) {
    if (!m_findEdit || m_findEdit->text().isEmpty()) return;
    refreshMatchHighlights();
  });

  // Cmd/Ctrl+F のグローバルキャプチャ
  qApp->installEventFilter(this);

  // Enter キーでクリック扱いにするフィルタ (他ビュアーと同じ作法)。
  // m_findEdit には別途 eventFilter を install するのでこちらは届かない
  // (= 検索の Enter 動作は壊さない)。
  auto* clickFilter = new EnterClickFilter(this);
  clickFilter->installOnButtonsIn(m_toolbar);

  root->addWidget(m_toolbar);

  m_browser = new QTextBrowser(this);
  m_browser->setOpenExternalLinks(true);  // リンクはクリックで OS 既定ブラウザ
  m_browser->setReadOnly(true);
  root->addWidget(m_browser, /*stretch*/ 1);

  setFocusProxy(m_browser);
}

bool MarkdownView::loadFile(const QString& filePath) {
  PreparedLoad p = prepareLoad(filePath, m_encoding);
  if (!p.ok) return false;
  applyPreparedLoad(p);
  return true;
}

MarkdownView::PreparedLoad MarkdownView::prepareLoad(
    const QString& filePath,
    const QString& userEncoding,
    const std::atomic<bool>* cancelToken,
    qint64 maxBytes) {
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

  if (userEncoding.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0) {
    r.actualEncoding = detectEncoding(r.data);
  } else {
    r.actualEncoding = userEncoding;
  }

  if (cancelled()) return r;

  r.text = decodeBytes(r.data, r.actualEncoding);

  if (cancelled()) return r;

  // truncate された場合は末尾に注記 (生ソース末尾、整形表示には現れる)
  if (r.totalSize > r.loadedSize) {
    r.text.append(QStringLiteral("\n\n---\n_[truncated: showing first %1 of %2 bytes]_\n")
                    .arg(r.loadedSize).arg(r.totalSize));
  }

  r.ok = true;
  return r;
}

void MarkdownView::applyPreparedLoad(const PreparedLoad& r) {
  m_filePath       = r.filePath;
  m_data           = r.data;
  m_actualEncoding = r.actualEncoding;
  m_text           = r.text;
  m_totalSize      = r.totalSize;
  m_loadedSize     = r.loadedSize;

  // 相対パス画像 (`![alt](path)`) を Markdown ファイルのあるディレクトリ
  // 基準で解決できるよう、QTextBrowser に searchPaths を設定。
  if (!m_filePath.isEmpty()) {
    const QString dir = QFileInfo(m_filePath).absolutePath();
    m_browser->setSearchPaths({ dir });
  }

  renderCurrent();
}

namespace {

// Qt の setMarkdown (GitHub dialect) は raw HTML を通すため、コードに入れ忘れた
// 生の "<...>" が HTML 開始タグと解釈され、対応する閉じが無いと以降の本文が
// 飲み込まれて消える (例: SPEC.md の型名)。コードスパン / フェンスドコード
// ブロックの外にある '<' だけを "&lt;" にしてタグ解釈を無効化する。
// ('>' '&' は setMarkdown が正しく扱うので触らない。コード内も触らない。)
QString neutralizeRawHtml(const QString& src) {
  QString out;
  out.reserve(src.size() + 16);
  const QStringList lines = src.split(QLatin1Char('\n'));
  bool inFence = false;
  for (int li = 0; li < lines.size(); ++li) {
    const QString& line = lines.at(li);
    const QString trimmed = line.trimmed();
    const bool fenceLine = trimmed.startsWith(QLatin1String("```")) ||
                           trimmed.startsWith(QLatin1String("~~~"));
    if (fenceLine) {
      inFence = !inFence;
      out += line;  // フェンス行自体はそのまま
    } else if (inFence) {
      out += line;  // コードブロック内はそのまま
    } else {
      int i = 0;
      const int n = line.size();
      while (i < n) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('`')) {
          // バッククォートのラン長を測り、同じ長さの閉じまでをコードとして素通し
          int j = i;
          while (j < n && line.at(j) == QLatin1Char('`')) ++j;
          const int runLen = j - i;
          int k = j;
          int closeStart = -1;
          while (k < n) {
            if (line.at(k) == QLatin1Char('`')) {
              int m = k;
              while (m < n && line.at(m) == QLatin1Char('`')) ++m;
              if (m - k == runLen) { closeStart = k; break; }
              k = m;
            } else {
              ++k;
            }
          }
          if (closeStart >= 0) {
            out += line.mid(i, closeStart + runLen - i);
            i = closeStart + runLen;
            continue;
          }
          // 閉じが無ければ通常文字として扱う
        }
        if (c == QLatin1Char('<')) {
          out += QLatin1String("&lt;");
        } else {
          out += c;
        }
        ++i;
      }
    }
    if (li + 1 < lines.size()) {
      out += QLatin1Char('\n');
    }
  }
  return out;
}

} // namespace

void MarkdownView::renderCurrent() {
  if (!m_browser) return;
  if (m_rawSource) {
    m_browser->setPlainText(m_text);
  } else {
    // CommonMark + GFM 拡張 (テーブル / タスクリスト / 取り消し線 / autolink)
    m_browser->document()->setMarkdown(
      neutralizeRawHtml(m_text), QTextDocument::MarkdownDialectGitHub);
  }
  // 先頭にスクロールを戻す
  m_browser->moveCursor(QTextCursor::Start);
  m_browser->verticalScrollBar()->setValue(0);
  // 再描画後にハイライトを掛け直す (検索文字列が入っていれば)
  refreshMatchHighlights();
}

void MarkdownView::onToggleRawSource(bool rawSource) {
  m_rawSource = rawSource;
  renderCurrent();
}

void MarkdownView::clearContent() {
  m_filePath.clear();
  m_data.clear();
  m_text.clear();
  m_totalSize = 0;
  m_loadedSize = 0;
  if (m_browser) {
    m_browser->setExtraSelections({});
    m_browser->clear();
  }
  if (m_findEdit)   m_findEdit->clear();
  if (m_findStatus) m_findStatus->clear();
}

// ---------- 検索 ----------

void MarkdownView::focusFindInput() {
  if (!m_findEdit) return;
  if (m_browser) {
    // ブラウザ側で選択中のテキストがあれば検索ボックスに流し込む。
    const QString sel = m_browser->textCursor().selectedText();
    if (!sel.isEmpty() && !sel.contains(QChar(0x2029))) {
      m_findEdit->setText(sel);
    }
  }
  m_findEdit->setFocus(Qt::ShortcutFocusReason);
  m_findEdit->selectAll();
}

void MarkdownView::findNext()     { findInDirection(false); }
void MarkdownView::findPrevious() { findInDirection(true); }

void MarkdownView::findInDirection(bool backward) {
  if (!m_browser || !m_findEdit) return;
  const QString needle = m_findEdit->text();
  if (needle.isEmpty()) {
    m_browser->setExtraSelections({});
    if (m_findStatus) m_findStatus->clear();
    return;
  }
  refreshMatchHighlights();

  QTextDocument::FindFlags flags;
  if (backward) flags |= QTextDocument::FindBackward;
  if (m_findCsButton && m_findCsButton->isChecked()) {
    flags |= QTextDocument::FindCaseSensitively;
  }

  // QTextBrowser::find は現在カーソル位置から検索して見つけたら true。
  // 末端まで来たら反対端から検索しなおして wrap-around。
  if (!m_browser->find(needle, flags)) {
    QTextCursor cur = m_browser->textCursor();
    cur.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    m_browser->setTextCursor(cur);
    m_browser->find(needle, flags);
  }
}

void MarkdownView::refreshMatchHighlights() {
  if (!m_browser || !m_findEdit) return;
  const QString needle = m_findEdit->text();
  QList<QTextEdit::ExtraSelection> selections;
  if (needle.isEmpty()) {
    m_browser->setExtraSelections(selections);
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

  QTextDocument* doc = m_browser->document();
  QTextCursor cur(doc);
  while (true) {
    cur = doc->find(needle, cur, flags);
    if (cur.isNull()) break;
    QTextEdit::ExtraSelection sel;
    sel.cursor = cur;
    sel.format = fmt;
    selections.append(sel);
  }
  m_browser->setExtraSelections(selections);
  if (m_findStatus) {
    if (selections.isEmpty()) m_findStatus->setText(tr("Not found"));
    else                      m_findStatus->setText(tr("%1 matches").arg(selections.size()));
  }
}

bool MarkdownView::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_findEdit && event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Escape) {
      if (m_browser) m_browser->setFocus(Qt::OtherFocusReason);
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
    const bool isFindKey =
      ke->key() == Qt::Key_F &&
      (ke->modifiers() & Qt::ControlModifier);
    if (isFindKey && isVisible() && window() && window()->isActiveWindow()) {
      if (event->type() == QEvent::KeyPress) focusFindInput();
      event->accept();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

QString MarkdownView::statusInfo() const {
  if (m_filePath.isEmpty()) return QString();
  const qint64 lines = m_text.count(QLatin1Char('\n')) + 1;
  const QFileInfo fi(m_filePath);
  QString s = tr("%1 lines · %2 · %3")
                .arg(QLocale().toString(lines))
                .arg(m_actualEncoding.isEmpty()
                       ? QStringLiteral("?")
                       : m_actualEncoding)
                .arg(QLocale(QLocale::English).formattedDataSize(fi.size()));
  if (m_totalSize > m_loadedSize) {
    s += QStringLiteral(" · [truncated]");
  }
  return s;
}

} // namespace Farman
