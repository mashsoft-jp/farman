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
// 1 つ = TIPS 1 件で、見出しがタイトル、続く本文が内容。表示する TIPS はランダムに
// 選び、「次の TIPS」でも直前と同じものは連続させない。
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

private:
  void showRandomTip();
  void showTip(int index);

  QList<Tip>    m_tips;
  int           m_current = -1;
  QLabel*       m_titleLabel = nullptr;
  QTextBrowser* m_bodyView = nullptr;
  QCheckBox*    m_showOnStartupCheck = nullptr;
  QPushButton*  m_nextButton = nullptr;
  QPushButton*  m_closeButton = nullptr;
};

} // namespace Farman
