#include "MarkdownView.h"

#include "settings/Settings.h"
#include "viewer/ViewerShortcutMap.h"
#include "viewer/ViewerHints.h"
#include "keybinding/ViewerCommands.h"
#include "utils/EnterClickFilter.h"
#include "utils/MarkdownSanitize.h"

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
#include <QPalette>
#include <QAbstractTextDocumentLayout>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStringDecoder>
#include <QStringList>
#include <QSplitter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
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
  applyToolbarStyle(m_toolbar);

  // 「ソース表示」トグル: ON = 生 Markdown (= テキストエディタ風)、
  //                      OFF = 整形表示 (= QTextDocument::setMarkdown)。
  // アイコンは流用が無いので簡易にラベル表示。
  // 既定の表示モード (Settings → Plugins → Markdown Viewer)。connect 前に
  // 設定するので slot は走らない。実描画は loadFile → renderCurrent が行う。
  m_rawSource = Settings::instance().markdownViewerShowSource();

  m_rawSourceButton = new QToolButton(m_toolbar);
  m_rawSourceButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/code.svg")));
  m_rawSourceButton->setCheckable(true);
  m_rawSourceButton->setChecked(m_rawSource);
  ViewerHints::tag(m_rawSourceButton, QStringLiteral("viewer.markdown.toggle_source"),
                   tr("Show raw Markdown source instead of rendered HTML (%1)"));
  m_rawSourceButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_rawSourceButton, &QToolButton::toggled,
          this,              &MarkdownView::onToggleRawSource);
  m_toolbar->addWidget(m_rawSourceButton);

  m_tocButton = new QToolButton(m_toolbar);
  m_tocButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/list.svg")));
  m_tocButton->setCheckable(true);
  m_tocButton->setEnabled(false);  // 見出しのある文書を整形表示しているときだけ有効
  ViewerHints::tag(m_tocButton, QStringLiteral("viewer.markdown.toggle_toc"),
                   tr("Show the table of contents sidebar (%1)"));
  m_tocButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_tocButton, &QToolButton::toggled, this, &MarkdownView::onTocToggled);
  m_toolbar->addWidget(m_tocButton);

  m_toolbar->addSeparator();

  // ───── 検索 (TextView と同じレイアウト) ─────
  auto* findLabel = new QLabel(tr("Find:"), m_toolbar);
  ViewerHints::tag(findLabel, QStringLiteral("viewer.markdown.find_focus"),
                   tr("Search text in this Markdown (%1)"));
  m_toolbar->addWidget(findLabel);

  m_findEdit = new QLineEdit(m_toolbar);
  ViewerHints::tag(m_findEdit, QStringLiteral("viewer.markdown.find_focus"),
                   tr("Search text  (%1)"), /*placeholder=*/true);
  ViewerHints::tag(m_findEdit, QStringLiteral("viewer.markdown.find_focus"),
                   tr("Search text in this Markdown (%1)"));
  m_findEdit->setClearButtonEnabled(true);
  m_findEdit->setFocusPolicy(Qt::StrongFocus);
  m_findEdit->setMinimumWidth(160);
  m_findEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_findEdit->installEventFilter(this);
  m_toolbar->addWidget(m_findEdit);

  m_findPrevBtn = new QToolButton(m_toolbar);
  m_findPrevBtn->setIcon(QIcon(QStringLiteral(":/icons/toolbar/chevron-up.svg")));
  m_findPrevBtn->setToolTip(tr("Previous match (Shift+Enter)"));
  m_findPrevBtn->setFocusPolicy(Qt::StrongFocus);
  connect(m_findPrevBtn, &QToolButton::clicked,
          this,           &MarkdownView::findPrevious);
  m_toolbar->addWidget(m_findPrevBtn);

  m_findNextBtn = new QToolButton(m_toolbar);
  m_findNextBtn->setIcon(QIcon(QStringLiteral(":/icons/toolbar/chevron-down.svg")));
  m_findNextBtn->setToolTip(tr("Next match (Enter)"));
  m_findNextBtn->setFocusPolicy(Qt::StrongFocus);
  connect(m_findNextBtn, &QToolButton::clicked,
          this,           &MarkdownView::findNext);
  m_toolbar->addWidget(m_findNextBtn);

  // 大文字小文字区別トグル
  m_findCsButton = new QToolButton(m_toolbar);
  m_findCsButton->setCheckable(true);
  m_findCsButton->setIcon(QIcon(QStringLiteral(":/icons/toolbar/case-sensitive.svg")));
  ViewerHints::tag(m_findCsButton, QStringLiteral("viewer.markdown.toggle_case"),
                   tr("Toggle case-sensitive search (%1)"));
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

  // 左: 目次サイドバー (既定は非表示) / 右: 本文。
  m_splitter = new QSplitter(Qt::Horizontal, this);
  m_splitter->setChildrenCollapsible(false);

  m_tocTree = new QTreeWidget(m_splitter);
  m_tocTree->setHeaderHidden(true);
  m_tocTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_tocTree->setMinimumWidth(160);
  m_tocTree->hide();
  m_tocTree->installEventFilter(this);
  connect(m_tocTree, &QTreeWidget::itemActivated, this, &MarkdownView::onTocActivated);
  connect(m_tocTree, &QTreeWidget::itemClicked,   this, &MarkdownView::onTocActivated);

  m_browser = new QTextBrowser(m_splitter);
  m_browser->setOpenExternalLinks(true);  // リンクはクリックで OS 既定ブラウザ
  m_browser->setReadOnly(true);

  m_splitter->setStretchFactor(0, 0);
  m_splitter->setStretchFactor(1, 1);
  root->addWidget(m_splitter, /*stretch*/ 1);

  setFocusProxy(m_browser);

  // 表示フォント + 本文 文字色 / 背景色 (テーマ依存: Settings → Plugins →
  // Markdown Viewer)。QTextDocument の既定フォントが本文の基準フォントになり、
  // 見出し等は相対サイズで描かれる。文字色 / 背景色はビューアのパレット
  // (Text / Base) に反映する。無効色のときはテーマ既定パレットに追従する。
  applyViewerAppearance();
  connect(&Settings::instance(), &Settings::settingsChanged, this, [this]() {
    applyViewerAppearance();
    renderCurrent();
  });
}

void MarkdownView::applyViewerAppearance() {
  Settings& s = Settings::instance();
  m_browser->document()->setDefaultFont(s.markdownViewerFont());

  // テーマ既定パレットを起点に、明示指定された色だけ上書きする。
  QPalette pal = qApp->palette(m_browser);
  const QColor fg = s.markdownViewerForeground();
  const QColor bg = s.markdownViewerBackground();
  if (bg.isValid()) {
    pal.setColor(QPalette::Base,   bg);
    pal.setColor(QPalette::Window, bg);
  }
  if (fg.isValid()) {
    pal.setColor(QPalette::Text,       fg);
    pal.setColor(QPalette::WindowText, fg);
  }
  const QColor link = s.markdownViewerLinkColor();
  if (link.isValid()) {
    pal.setColor(QPalette::Link, link);
  }
  m_browser->setPalette(pal);
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

void MarkdownView::renderCurrent() {
  if (!m_browser) return;
  if (m_rawSource) {
    m_browser->setPlainText(m_text);
  } else {
    // CommonMark + GFM 拡張 (テーブル / タスクリスト / 取り消し線 / autolink)
    m_browser->document()->setMarkdown(
      MarkdownSanitize::neutralizeRawHtml(m_text),
      QTextDocument::MarkdownDialectGitHub);
  }
  // 先頭にスクロールを戻す
  m_browser->moveCursor(QTextCursor::Start);
  m_browser->verticalScrollBar()->setValue(0);
  // 再描画後にハイライトを掛け直す (検索文字列が入っていれば)
  refreshMatchHighlights();
  rebuildToc();
}

void MarkdownView::rebuildToc() {
  if (!m_tocTree || !m_browser) return;
  m_tocTree->clear();

  // 整形表示のときだけ見出しを拾う (生ソース表示は plain text で見出し情報が無い)。
  if (!m_rawSource) {
    // 階層は見出しレベルの上がり下がりで組む。h1 を飛ばして h2 から始まる文書や
    // h2 → h4 のような飛びがあっても、直近の「より浅い見出し」の子にする。
    QList<QTreeWidgetItem*> stack;
    QList<int>              levels;
    for (QTextBlock b = m_browser->document()->begin(); b.isValid(); b = b.next()) {
      const int level = b.blockFormat().headingLevel();
      const QString title = b.text().trimmed();
      if (level < 1 || title.isEmpty()) continue;

      while (!levels.isEmpty() && levels.last() >= level) {
        levels.removeLast();
        stack.removeLast();
      }
      auto* item = stack.isEmpty() ? new QTreeWidgetItem(m_tocTree)
                                   : new QTreeWidgetItem(stack.last());
      item->setText(0, title);
      item->setData(0, Qt::UserRole, b.blockNumber());
      stack.append(item);
      levels.append(level);
    }
    m_tocTree->expandAll();
  }

  const bool has = m_tocTree->topLevelItemCount() > 0;
  if (m_tocButton) {
    // 使えなくなるとき (見出し無し / 生ソース表示) は、開いているパネルも畳む。
    if (!has && m_tocButton->isChecked()) m_tocButton->setChecked(false);
    m_tocButton->setEnabled(has);
  }
}

void MarkdownView::onTocToggled(bool visible) {
  if (!m_tocTree || !m_splitter) return;
  m_tocTree->setVisible(visible);
  if (visible && !m_tocSized) {
    m_tocSized = true;
    const int total  = m_splitter->width();
    const int panelW = qMin(240, total / 3);
    m_splitter->setSizes({panelW, total - panelW});
  }
  // 開いたらツリーにフォーカス (矢印 + Enter で移動できる)。閉じたら本文へ戻す。
  if (visible) {
    m_tocTree->setFocus();
    if (!m_tocTree->currentItem() && m_tocTree->topLevelItemCount() > 0) {
      m_tocTree->setCurrentItem(m_tocTree->topLevelItem(0));
    }
  } else if (m_browser) {
    m_browser->setFocus();
  }
}

void MarkdownView::onTocActivated(QTreeWidgetItem* item) {
  if (!item || !m_browser) return;
  const QTextBlock block =
    m_browser->document()->findBlockByNumber(item->data(0, Qt::UserRole).toInt());
  if (!block.isValid()) return;
  // 見出しが本文ビューの先頭に来るようにスクロールする。
  const QRectF rect =
    m_browser->document()->documentLayout()->blockBoundingRect(block);
  m_browser->verticalScrollBar()->setValue(qRound(rect.top()));
  QTextCursor c(block);
  m_browser->setTextCursor(c);
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
  rebuildToc();
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
  // 目次ツリーの Enter: 項目へジャンプして吸収する (QTreeWidget は Enter を
  // activated で通知したあと event を ignore するため、親に伝わってビュアーが閉じる)。
  if (watched == m_tocTree && event->type() == QEvent::KeyPress) {
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
      onTocActivated(m_tocTree->currentItem());
      return true;
    }
  }
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

void MarkdownView::applyShortcutBindings(const QVariantMap& bindings) {
  m_shortcuts.applyBindings(bindings);
  ViewerHints::refresh(this, m_shortcuts);
}

void MarkdownView::dispatchViewerCommand(const QString& cmd) {
  if (cmd == QLatin1String("viewer.markdown.find_focus")) {
    focusFindInput();
  } else if (cmd == QLatin1String("viewer.markdown.toggle_source")) {
    if (m_rawSourceButton) m_rawSourceButton->toggle();
  } else if (cmd == QLatin1String("viewer.markdown.toggle_toc")) {
    if (m_tocButton && m_tocButton->isEnabled()) m_tocButton->toggle();
  } else if (cmd == QLatin1String("viewer.markdown.toggle_case")) {
    if (m_findCsButton) m_findCsButton->toggle();
  }
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
