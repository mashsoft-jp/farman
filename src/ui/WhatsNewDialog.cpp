#include "WhatsNewDialog.h"

#include "settings/Settings.h"
#include "utils/MarkdownSanitize.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLocale>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace Farman {

WhatsNewDialog::WhatsNewDialog(const QString& version, const QString& markdown,
                               QWidget* parent)
  : QDialog(parent) {
  setWindowTitle(tr("What's New — farman %1").arg(version));
  setModal(true);
  resize(560, 420);

  auto* outer = new QVBoxLayout(this);

  m_notesView = new QTextBrowser(this);
  m_notesView->setOpenExternalLinks(true);
  // 生の "<...>" が HTML タグ扱いされて以降の本文が消えるのを防ぐ
  // (MarkdownSanitize.h のコメント参照)。
  m_notesView->setMarkdown(MarkdownSanitize::neutralizeRawHtml(markdown));
  outer->addWidget(m_notesView, /*stretch=*/1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  outer->addWidget(buttons);
}

QString WhatsNewDialog::loadBundledNotes() {
  // main.cpp の翻訳ロードと同じ言語解決 ("ja_JP" → "ja")。
  QString lang;
  switch (Settings::instance().language()) {
    case LanguageMode::English:  lang = QStringLiteral("en"); break;
    case LanguageMode::Japanese: lang = QStringLiteral("ja"); break;
    case LanguageMode::Auto:     lang = QLocale::system().name(); break;
  }
  const QString shortLang = lang.section('_', 0, 0);

  const QStringList candidates = {
    QStringLiteral(":/whatsnew/whatsnew_") + shortLang + QStringLiteral(".md"),
    QStringLiteral(":/whatsnew/whatsnew_en.md"),
  };
  for (const QString& path : candidates) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      return QString::fromUtf8(file.readAll());
    }
  }
  return {};
}

} // namespace Farman
