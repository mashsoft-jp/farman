#pragma once

#include <QDialog>
#include <QString>

class QTextBrowser;

namespace Farman {

// アップデート直後 (または初回起動時) に 1 回だけ「アップデート内容」を
// 表示するモーダルダイアログ。MainWindow が起動時に
// Settings::whatsNewShownVersion と現バージョンを比較して表示を判断する。
//
// 本文はリソース埋め込みの言語別 Markdown (":/whatsnew/whatsnew_<lang>.md")。
// 言語は Settings::language (Auto はシステムロケール) に従い、該当言語の
// ファイルが無ければ英語にフォールバックする。
class WhatsNewDialog : public QDialog {
  Q_OBJECT

public:
  // version はタイトル表示用 (FARMAN_VERSION)。markdown が本文。
  WhatsNewDialog(const QString& version, const QString& markdown,
                 QWidget* parent = nullptr);

  // 言語設定に応じた同梱 What's New 本文を読み込む。
  // どの言語のリソースも読めなければ空文字を返す (呼出側は表示をスキップ)。
  static QString loadBundledNotes();

private:
  QTextBrowser* m_notesView = nullptr;
};

} // namespace Farman
