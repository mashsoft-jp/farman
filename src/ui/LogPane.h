#pragma once

#include <QPlainTextEdit>

namespace Farman {

// 操作ログ表示用の読み取り専用テキストペイン。`Logger::entryAppended` を購読して
// 新規行を追記する。構築時に Logger の現バッファを流し込んで履歴を再現する。
//
// フォーカスがあるときは Cmd(Ctrl)+C で選択範囲をクリップボードへコピーし、
// Cmd(Ctrl)+A ですべて選択する。どちらのキーもファイルマネージャー側のコマンド
// (パスのコピー / すべて選択) に割り当てられており、パネル内の子ウィジェットでも
// 効くショートカットになっているため、ログ側で先に受け取る (event() 参照)。
class LogPane : public QPlainTextEdit {
  Q_OBJECT

public:
  explicit LogPane(QWidget* parent = nullptr);

protected:
  bool event(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
};

} // namespace Farman
