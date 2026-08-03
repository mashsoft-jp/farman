#pragma once

// 外部プラグインが createViewer() で返す QWidget を、External (独立ウィンドウ) モードの
// トップレベルウィンドウとして表示するためのラッパ。内蔵の各 *ViewerWindow
// (ImageViewerWindow など) と挙動を揃えるためのもの:
//   - Esc / Enter / Return で閉じる (フォーカス子が消費しなかったキーが伝播してくる)
//   - 初回の既定サイズ (プラグイン QWidget は自前で resize しないため)
//   - ステータスバーにプラグイン名を表示
// これが無いと、生の QWidget をそのままトップレベル化することになり、閉じるキーが
// 効かず・小さいサイズで開き・ステータスバーが無い、と内蔵ビュアーと挙動がずれる。
//
// signals/slots は持たないため Q_OBJECT は不要 (仮想関数 override のみ)。

#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QStatusBar>
#include <QString>
#include <QWidget>

namespace Farman {

class ExternalPluginViewerWindow : public QMainWindow {
public:
  ExternalPluginViewerWindow(QWidget* inner, const QString& title,
                             const QString& statusText, QWidget* parent = nullptr)
      : QMainWindow(parent) {
    setCentralWidget(inner);
    setWindowTitle(title);
    if (!statusText.isEmpty()) {
      statusBar()->addPermanentWidget(new QLabel(statusText, this));
    }
    resize(880, 640);
  }

protected:
  void keyPressEvent(QKeyEvent* e) override {
    if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Return ||
        e->key() == Qt::Key_Enter) {
      close();
      return;
    }
    QMainWindow::keyPressEvent(e);
  }
};

} // namespace Farman
