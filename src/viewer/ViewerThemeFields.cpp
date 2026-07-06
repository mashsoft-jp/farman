#include "ViewerThemeFields.h"

#include <QObject>
#include <QPushButton>

namespace Farman {

void styleThemeColorButton(QPushButton* btn, const QColor& c,
                           const QColor& fallback) {
  if (!btn) return;
  const QColor fill = c.isValid() ? c : fallback;
  if (!fill.isValid()) {
    btn->setStyleSheet(QString());
    btn->setText(QObject::tr("(none)"));
    return;
  }
  // macOS ではネイティブボタンが background-color を無視するため、border を
  // 併せて指定して styled 描画パスに切り替える (AppearanceTab と同じ作法)。
  // 文字色は背景輝度から自動選択する。
  const int luminance =
    (fill.red() * 299 + fill.green() * 587 + fill.blue() * 114) / 1000;
  const QString textColor = (luminance > 160) ? QStringLiteral("black")
                                              : QStringLiteral("white");
  btn->setStyleSheet(QStringLiteral(
      "QPushButton { background-color: %1; color: %2; border: 1px solid #888; "
                    "border-radius: 3px; padding: 2px 6px; }"
      "QPushButton:focus { border: 2px solid palette(highlight); padding: 1px 5px; }")
    .arg(fill.name(), textColor));
  btn->setText(c.isValid() ? c.name() : QObject::tr("(none)"));
}

QString fontFamilyLabel(const QFont& f) {
  const QString fam = f.family();
  if (fam.startsWith(QLatin1Char('.'))) return QObject::tr("System Font");
  return fam;
}

void styleThemeFontButton(QPushButton* btn, const QFont& f) {
  if (!btn) return;
  btn->setText(QStringLiteral("%1, %2pt").arg(fontFamilyLabel(f)).arg(f.pointSize()));
}

} // namespace Farman
