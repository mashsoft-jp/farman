#pragma once

#include <QComboBox>
#include <QFont>
#include <QIcon>
#include <QSignalBlocker>
#include <QToolButton>
#include <QString>
#include <QtGlobal>

namespace Farman {

// テキスト / Markdown ビュアーの拡大縮小で共有する定数とヘルパ。
// 倍率 (%) で持つので、設定変更でフォントが再適用されても同じ倍率を載せ直せる。
namespace ViewerZoom {

constexpr int    kMinPercent = 30;
constexpr int    kMaxPercent = 500;
constexpr int    kStepPercent = 10;
constexpr int    kDefaultPercent = 100;
constexpr qreal  kMinPoints  = 5.0;

inline int clampPercent(int percent) { return qBound(kMinPercent, percent, kMaxPercent); }

// 10% 刻みのグリッドにスナップして 1 段階拡大 / 縮小した倍率を返す
// (100→110→120…、または 100→90→80…)。中途半端な値からでも 10 の倍数に揃う。
inline int stepPercent(int percent, bool zoomIn) {
  const int next = zoomIn ? (percent / kStepPercent + 1) * kStepPercent
                          : (percent - 1) / kStepPercent * kStepPercent;
  return clampPercent(next);
}

inline QFont zoomedFont(QFont base, int percent) {
  if (base.pointSizeF() > 0) {
    base.setPointSizeF(qMax(kMinPoints, base.pointSizeF() * percent / 100.0));
  } else if (base.pixelSize() > 0) {
    base.setPixelSize(qMax(int(kMinPoints), qRound(base.pixelSize() * percent / 100.0)));
  }
  return base;
}

// 虫眼鏡に +/- のアイコンを持つツールバーボタン (PDF / 画像ビュアーと共通の見た目)。
inline QToolButton* makeButton(QWidget* parent, bool zoomIn) {
  auto* b = new QToolButton(parent);
  b->setIcon(QIcon(zoomIn ? QStringLiteral(":/icons/toolbar/zoom-in.svg")
                          : QStringLiteral(":/icons/toolbar/zoom-out.svg")));
  b->setFocusPolicy(Qt::StrongFocus);
  return b;
}

// 倍率を 100% に戻すボタン (虫眼鏡に「1」)。
inline QToolButton* makeResetButton(QWidget* parent) {
  auto* b = new QToolButton(parent);
  b->setIcon(QIcon(QStringLiteral(":/icons/toolbar/zoom-reset.svg")));
  b->setFocusPolicy(Qt::StrongFocus);
  return b;
}

// 倍率入力コンボ (画像ビュアーと同じ「NN%」表記、直接入力も可)。
inline QComboBox* makeCombo(QWidget* parent) {
  auto* c = new QComboBox(parent);
  c->setEditable(true);
  c->setInsertPolicy(QComboBox::NoInsert);
  for (int p : { 50, 75, 100, 125, 150, 200, 300 }) {
    c->addItem(QString::number(p) + QLatin1Char('%'), p);
  }
  c->setCurrentText(QStringLiteral("100%"));
  c->setFocusPolicy(Qt::StrongFocus);
  return c;
}

// コンボの表示を倍率に合わせる (シグナルは発火させない)。
inline void setComboPercent(QComboBox* c, int percent) {
  if (!c) return;
  QSignalBlocker b(c);
  c->setCurrentText(QString::number(percent) + QLatin1Char('%'));
}

// 「150」「150%」形式の入力を倍率にする。解釈できなければ 0。
inline int parsePercent(QString text) {
  text = text.trimmed();
  if (text.endsWith(QLatin1Char('%'))) text.chop(1);
  bool ok = false;
  const int v = text.trimmed().toInt(&ok);
  return ok ? clampPercent(v) : 0;
}

}  // namespace ViewerZoom
}  // namespace Farman
