#pragma once

#include <QFont>
#include <QIcon>
#include <QToolButton>
#include <QtGlobal>

namespace Farman {

// テキスト / Markdown ビュアーの拡大縮小で共有する定数とヘルパ。
// 倍率ではなく「設定フォントからの pt 差 (ステップ数)」で持つので、設定変更で
// フォントが再適用されても同じステップ数を載せ直せる。
namespace ViewerZoom {

constexpr int    kMinSteps  = -20;
constexpr int    kMaxSteps  = 60;
constexpr qreal  kMinPoints = 5.0;

inline int clampSteps(int steps) { return qBound(kMinSteps, steps, kMaxSteps); }

inline QFont zoomedFont(QFont base, int steps) {
  if (base.pointSizeF() > 0) {
    base.setPointSizeF(qMax(kMinPoints, base.pointSizeF() + steps));
  } else if (base.pixelSize() > 0) {
    base.setPixelSize(qMax(int(kMinPoints), base.pixelSize() + steps));
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

}  // namespace ViewerZoom
}  // namespace Farman
