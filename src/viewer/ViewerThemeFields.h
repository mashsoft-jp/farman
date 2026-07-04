#pragma once

// プラグインの設定ページ (IPluginSettingsPage) で、テーマ (ColorScheme) 依存の
// フォント / 配色を編集するための小さな表示ヘルパ。色ボタン / フォントボタンの
// 外観を現在値に合わせて更新するだけの純粋関数で、各公式ビュアーの設定ページが
// 共有する (テキスト / バイナリ / 画像)。
//
// 実際の Light/Dark 切替 (シャドースキームの保持と読み書き) は各ページが
// Settings::scheme() / setScheme() を使って行う。

#include <QColor>
#include <QFont>
#include <QObject>
#include <QPushButton>
#include <QString>

namespace Farman {

// 色ボタンの外観を現在値に合わせて更新する。
//  - c が有効色     : 背景をその色で塗り、ラベルに RGB (#RRGGBB) を表示。
//  - c が無効 (未設定): fallback (= その色を空欄にしたときに実際に使われる
//                       テーマ既定色) で背景を塗り、ラベルは "(none)"。
//                       fallback も無効なら無地 + "(none)"。
// これにより「未設定なら既定色、設定済みならその色」がボタン背景に出る。
inline void styleThemeColorButton(QPushButton* btn, const QColor& c,
                                  const QColor& fallback = QColor()) {
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

// フォントボタンのラベルを "Family, Npt" に更新する。
inline void styleThemeFontButton(QPushButton* btn, const QFont& f) {
  if (!btn) return;
  btn->setText(QStringLiteral("%1, %2pt").arg(f.family()).arg(f.pointSize()));
}

} // namespace Farman
