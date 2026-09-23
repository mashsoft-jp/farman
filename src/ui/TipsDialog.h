#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QCheckBox;
class QLabel;
class QPushButton;
class QTextBrowser;

namespace Farman {

// 起動時 TIPS のダイアログ (仕様は SPEC.md「起動時 TIPS」)。
//
// 本文は言語別 Markdown をリソースに同梱 (":/tips/tips_<lang>.md")。"## " 見出し
// 1 つ = TIPS 1 件で、見出しがタイトル、続く本文が内容。開いたとき (起動時 / ヘルプ)
// に出す 1 件はランダムに選び、「前へ」「次へ」は並び順に前後へ進む (端では反対側に回る)。
//
// 左下の「起動時に TIPS を表示する」は Settings::showTipsOnStartup と同じ値で、
// 切り替えるとその場で保存する。起動時に出すかどうかの判断 (1 日 1 回) は
// MainWindow::maybeShowTips が行う。
class TipsDialog : public QDialog {
  Q_OBJECT

public:
  explicit TipsDialog(QWidget* parent = nullptr);

  // 同梱の TIPS が 1 件も読めなければ false (呼出側は表示をスキップ)。
  bool hasTips() const { return !m_tips.isEmpty(); }

  // 言語設定に応じた同梱 TIPS を読み込んで分割する (テスト用に公開)。
  struct Tip {
    QString title;
    QString body;   // Markdown
  };
  static QList<Tip> loadBundledTips();
  static QList<Tip> parseTips(const QString& markdown);
  // 本文中の "{key:<command id>}" を、そのコマンドの現在のキー表記に置き換える。
  static QString expandKeys(const QString& markdown);

private:
  void showRandomTip();   // 開いたときの 1 件
  void showPreviousTip(); // 「前へ」: 並び順で前、1 なら最後へ
  void showNextTip();     // 「次へ」: 並び順で次、最後なら 1 へ
  void showTip(int index);

  QList<Tip>    m_tips;
  int           m_current = -1;
  QLabel*       m_titleLabel = nullptr;
  QTextBrowser* m_bodyView = nullptr;
  QCheckBox*    m_showOnStartupCheck = nullptr;
  QPushButton*  m_prevButton = nullptr;
  QPushButton*  m_nextButton = nullptr;
  QPushButton*  m_closeButton = nullptr;
};

} // namespace Farman
