#include "MarkdownView.h"

#include "settings/Settings.h"
#include "utils/EnterClickFilter.h"

#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QScrollBar>
#include <QStringDecoder>
#include <QTextBrowser>
#include <QTextDocument>
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
  m_rawSourceButton = new QToolButton(m_toolbar);
  m_rawSourceButton->setText(tr("Source"));
  m_rawSourceButton->setCheckable(true);
  m_rawSourceButton->setChecked(false);
  m_rawSourceButton->setToolTip(
    tr("Show raw Markdown source (off = rendered HTML)"));
  m_rawSourceButton->setFocusPolicy(Qt::StrongFocus);
  connect(m_rawSourceButton, &QToolButton::toggled,
          this,              &MarkdownView::onToggleRawSource);
  m_toolbar->addWidget(m_rawSourceButton);

  // Enter キーでクリック扱いにするフィルタ (他ビュアーと同じ作法)
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

void MarkdownView::renderCurrent() {
  if (!m_browser) return;
  if (m_rawSource) {
    m_browser->setPlainText(m_text);
  } else {
    // CommonMark + GFM 拡張 (テーブル / タスクリスト / 取り消し線 / autolink)
    m_browser->document()->setMarkdown(
      m_text, QTextDocument::MarkdownDialectGitHub);
  }
  // 先頭にスクロールを戻す
  m_browser->moveCursor(QTextCursor::Start);
  m_browser->verticalScrollBar()->setValue(0);
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
  if (m_browser) m_browser->clear();
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
