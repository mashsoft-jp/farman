#include "TipsDialog.h"

#include "settings/Settings.h"
#include "utils/Dialogs.h"
#include "utils/EnterClickFilter.h"
#include "utils/MarkdownSanitize.h"

#include <QCheckBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace Farman {

TipsDialog::TipsDialog(QWidget* parent)
  : QDialog(parent) {
  setWindowTitle(tr("Tips"));
  setModal(true);
  resize(520, 360);
  m_tips = loadBundledTips();

  auto* outer = new QVBoxLayout(this);

  m_titleLabel = new QLabel(this);
  QFont titleFont = m_titleLabel->font();
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
  titleFont.setBold(true);
  m_titleLabel->setFont(titleFont);
  m_titleLabel->setWordWrap(true);
  outer->addWidget(m_titleLabel);

  m_bodyView = new QTextBrowser(this);
  m_bodyView->setOpenExternalLinks(true);
  m_bodyView->setFrameShape(QFrame::NoFrame);
  outer->addWidget(m_bodyView, /*stretch=*/1);

  auto* bottom = new QHBoxLayout();
  // 設定 → 全般 → 起動設定の「起動時に TIPS を表示する」と同じ値。切り替えたら
  // その場で保存する (設定ダイアログを開き直せば反映されている)。
  m_showOnStartupCheck = new QCheckBox(tr("Show tips on startup"), this);
  applyAltShortcut(m_showOnStartupCheck, Qt::Key_S);
  m_showOnStartupCheck->setChecked(Settings::instance().showTipsOnStartup());
  connect(m_showOnStartupCheck, &QCheckBox::toggled, this, [](bool checked) {
    auto& s = Settings::instance();
    if (s.showTipsOnStartup() == checked) return;
    s.setShowTipsOnStartup(checked);
    s.save();
  });
  bottom->addWidget(m_showOnStartupCheck);
  bottom->addStretch(1);

  // 「次の TIPS」は Tab でフォーカスを当てて Enter でも押せるようにするが、既定
  // ボタンにはしない (Enter の既定は「閉じる」)。開いたときの 1 件はランダムだが、
  // こちらは並び順に次へ進む (最後の次は 1 に戻る)。
  m_nextButton = new QPushButton(tr("Next Tip"), this);
  applyAltShortcut(m_nextButton, Qt::Key_N);
  m_nextButton->setAutoDefault(false);
  m_nextButton->setEnabled(m_tips.size() > 1);
  connect(m_nextButton, &QPushButton::clicked, this, &TipsDialog::showNextTip);
  bottom->addWidget(m_nextButton);

  m_closeButton = new QPushButton(tr("Close"), this);
  applyAltShortcut(m_closeButton, Qt::Key_C);
  m_closeButton->setDefault(true);
  connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
  bottom->addWidget(m_closeButton);
  outer->addLayout(bottom);

  // ボタンにフォーカスがあるときの Enter は、そのボタンを押す (既定ボタンではなく)。
  auto* enterFilter = new EnterClickFilter(this);
  enterFilter->installOnButtonsIn(m_nextButton);
  enterFilter->installOnButtonsIn(m_closeButton);
  enterFilter->installOnButtonsIn(m_showOnStartupCheck);

  QWidget::setTabOrder(m_bodyView, m_showOnStartupCheck);
  QWidget::setTabOrder(m_showOnStartupCheck, m_nextButton);
  QWidget::setTabOrder(m_nextButton, m_closeButton);
  m_closeButton->setFocus();

  showRandomTip();
}

void TipsDialog::showRandomTip() {
  if (m_tips.isEmpty()) return;
  showTip(static_cast<int>(QRandomGenerator::global()->bounded(m_tips.size())));
}

void TipsDialog::showNextTip() {
  if (m_tips.isEmpty()) return;
  showTip((m_current + 1) % m_tips.size());
}

void TipsDialog::showTip(int index) {
  if (index < 0 || index >= m_tips.size()) return;
  m_current = index;
  const Tip& tip = m_tips[index];
  // 番号 / 全体数を添える (最初の 1 件はランダムなので、どこから始まったかの目印にもなる)。
  m_titleLabel->setText(
    tr("(%1 / %2)  %3").arg(index + 1).arg(m_tips.size()).arg(tip.title));
  // 生の "<...>" が HTML タグ扱いされて以降の本文が消えるのを防ぐ
  // (MarkdownSanitize.h のコメント参照)。
  m_bodyView->setMarkdown(MarkdownSanitize::neutralizeRawHtml(tip.body));
}

QList<TipsDialog::Tip> TipsDialog::loadBundledTips() {
  // main.cpp の翻訳ロードと同じ言語解決 ("ja_JP" → "ja")。
  QString lang;
  switch (Settings::instance().language()) {
    case LanguageMode::English:  lang = QStringLiteral("en"); break;
    case LanguageMode::Japanese: lang = QStringLiteral("ja"); break;
    case LanguageMode::Auto:     lang = QLocale::system().name(); break;
  }
  const QString shortLang = lang.section(QLatin1Char('_'), 0, 0);

  const QStringList candidates = {
    QStringLiteral(":/tips/tips_") + shortLang + QStringLiteral(".md"),
    QStringLiteral(":/tips/tips_en.md"),
  };
  for (const QString& path : candidates) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      const QList<Tip> tips = parseTips(QString::fromUtf8(file.readAll()));
      if (!tips.isEmpty()) return tips;
    }
  }
  return {};
}

QList<TipsDialog::Tip> TipsDialog::parseTips(const QString& markdown) {
  // "## " 見出し 1 つ = TIPS 1 件。見出しより前の本文は捨てる。
  QList<Tip> tips;
  Tip current;
  bool inTip = false;
  QStringList bodyLines;
  const auto flush = [&]() {
    if (!inTip) return;
    current.body = bodyLines.join(QLatin1Char('\n')).trimmed();
    if (!current.title.isEmpty() && !current.body.isEmpty()) {
      tips.append(current);
    }
    current = Tip();
    bodyLines.clear();
  };
  const QStringList lines = markdown.split(QLatin1Char('\n'));
  for (const QString& line : lines) {
    if (line.startsWith(QLatin1String("## "))) {
      flush();
      inTip = true;
      current.title = line.mid(3).trimmed();
    } else if (inTip) {
      bodyLines.append(line);
    }
  }
  flush();
  return tips;
}

} // namespace Farman
