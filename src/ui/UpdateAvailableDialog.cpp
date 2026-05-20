#include "UpdateAvailableDialog.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace Farman {

UpdateAvailableDialog::UpdateAvailableDialog(const QString& currentVersion,
                                             const ReleaseInfo& release,
                                             QWidget* parent)
  : QDialog(parent), m_release(release) {
  setWindowTitle(tr("Update available — farman %1").arg(release.version));
  // モードレスにすると裏でユーザーが farman を触れるが、Phase B では modal
  // (起動直後 1 回ポップアップ + ユーザーの明示選択) の方が混乱が少ないので
  // modal にしておく。SPEC.md はモードレスを示唆しているが、Update Now が
  // ブラウザ遷移 / ダウンロード起動なので modal でも UX 上問題なし。
  setModal(true);
  resize(560, 420);

  auto* outer = new QVBoxLayout(this);

  // ── ヘッダ: 現在版 / 最新版 / Release page リンク ──
  auto* header = new QLabel(this);
  header->setTextFormat(Qt::RichText);
  header->setOpenExternalLinks(true);
  header->setText(tr(
    "<b>A new version of farman is available.</b><br><br>"
    "Current: <b>%1</b><br>"
    "Latest:  <b>%2</b><br><br>"
    "Release page: <a href=\"%3\">%3</a>")
      .arg(currentVersion.toHtmlEscaped(),
           release.version.toHtmlEscaped(),
           release.htmlUrl.toHtmlEscaped()));
  outer->addWidget(header);

  // ── リリースノート (Markdown を QTextBrowser でレンダリング) ──
  m_notesView = new QTextBrowser(this);
  m_notesView->setOpenExternalLinks(true);
  if (!release.body.isEmpty()) {
    m_notesView->setMarkdown(release.body);
  } else {
    m_notesView->setPlainText(tr("(No release notes provided.)"));
  }
  outer->addWidget(m_notesView, /*stretch=*/1);

  // ── ボタン: Update Now / Remind Me Later / Skip This Version ──
  auto* buttons = new QHBoxLayout;
  buttons->addStretch();
  auto* skipBtn   = new QPushButton(tr("Skip This Version"), this);
  auto* remindBtn = new QPushButton(tr("Remind Me Later"),   this);
  auto* updateBtn = new QPushButton(tr("Update Now"),        this);
  updateBtn->setDefault(true);
  buttons->addWidget(skipBtn);
  buttons->addWidget(remindBtn);
  buttons->addWidget(updateBtn);
  outer->addLayout(buttons);

  connect(updateBtn, &QPushButton::clicked, this, [this]() {
    m_action = Action::UpdateNow;
    accept();
  });
  connect(remindBtn, &QPushButton::clicked, this, [this]() {
    m_action = Action::RemindLater;
    reject();
  });
  connect(skipBtn, &QPushButton::clicked, this, [this]() {
    m_action = Action::Skip;
    reject();
  });
}

} // namespace Farman
