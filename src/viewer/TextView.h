#pragma once

#include <QByteArray>
#include <QString>
#include <QWidget>

#include <atomic>

class QComboBox;
class QToolButton;
class QLineEdit;
class QLabel;

namespace Farman {

class TextEditArea;

// テキストファイル表示用ウィジェット。
//   - 上部のツールバーで Encoding / Line Numbers / Word Wrap をその場で上書き可能
//     (上書きは Settings に保存されない)
//   - フォント / カラー / 行番号エリアの色は Settings から取得する
//   - Settings::settingsChanged でローカルの上書きはリセットされる
class TextView : public QWidget {
  Q_OBJECT

public:
  // 非同期ロード用の中間表現。prepareLoad の戻り値で、UI を触らないため
  // ワーカースレッドで生成して main へ渡せる。
  struct PreparedLoad {
    bool       ok = false;
    QString    filePath;
    QByteArray data;
    QString    actualEncoding;
    QString    text;
  };

  explicit TextView(QWidget* parent = nullptr);

  // 失敗時 (open エラー) は false。
  bool loadFile(const QString& filePath);

  // ワーカースレッドで実行可能なロード処理 (UI 非依存)。
  // QtConcurrent::run などから呼び、戻り値を applyPreparedLoad に渡す。
  //
  // cancelToken を渡すと、各 stage (open / read / encoding detect / decode)
  // の合間に true を確認した時点で早期 return して PreparedLoad{ ok=false }
  // を返す。プレビューモードでカーソル移動が起きたとき、進行中のロードを
  // 「中断」したいときに使う (旧 API 互換のため nullptr 既定)。
  static PreparedLoad prepareLoad(const QString& filePath,
                                  const QString& userEncoding,
                                  const std::atomic<bool>* cancelToken = nullptr);
  // ワーカーの結果を UI に反映する。必ずメインスレッドから呼ぶこと。
  void applyPreparedLoad(const PreparedLoad& result);

  // 現在ユーザーが選んでいるエンコード (m_encoding)。
  // ViewerPanel から prepareLoad に渡す引数を取得するために使う。
  QString currentUserEncoding() const { return m_encoding; }

  // 表示中ファイルをクリア。
  void clearContent();

  // ステータスバー表示用の要約 ("1234 lines · UTF-8 · 56 KB" 等)。
  QString statusInfo() const;

  // 内部 TextEditArea が paintEvent から呼び出すアクセサ群。
  bool   showLineNumbers() const { return m_showLineNumbers; }
  QColor lineNumberForeground() const;
  QColor lineNumberBackground() const;

protected:
  // 検索入力欄の Shift+Enter / Esc を拾うため。
  bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
  // 検索入力欄にフォーカスを移し、テキストを全選択する (Cmd/Ctrl+F)。
  void focusFindInput();
  void findNext();
  void findPrevious();

private:
  void setupUi();
  void syncFromSettings();
  void applyEditAreaSettings();
  void reloadFromBuffer();
  // m_findEdit のテキストを使って双方向検索する。
  void findInDirection(bool backward);
  // ヒット箇所すべてを ExtraSelection で強調表示する。空文字列ならクリア。
  void refreshMatchHighlights();
  static QString decodeBytes(const QByteArray& data, const QString& encoding);

  TextEditArea* m_editArea  = nullptr;
  QComboBox*    m_encodingCombo     = nullptr;
  // 行番号 / ワードラップは ON/OFF の二値なのでアイコンのチェック付きトグル。
  QToolButton*  m_lineNumbersButton = nullptr;
  QToolButton*  m_wordWrapButton    = nullptr;

  // 検索コントロール群 (ヘッダ常設)
  QLineEdit*    m_findEdit     = nullptr;
  // 「Aa」アイコンの大文字小文字区別トグル。
  QToolButton*  m_findCsButton = nullptr;
  QLabel*       m_findStatus   = nullptr;

  QString    m_filePath;
  QByteArray m_data;

  // 表示に使う実効設定 (ローカル上書き)。Settings 変更で再同期される。
  // m_encoding は「ユーザーが選択したエンコード」("Auto" or 具体名)、
  // m_actualEncoding は実際にデコードに使った具体名 (Auto の場合は uchardet
  // で検出した結果が入る)。ステータスバー表示にも m_actualEncoding を使う。
  QString m_encoding         = QStringLiteral("UTF-8");
  QString m_actualEncoding   = QStringLiteral("UTF-8");
  bool    m_showLineNumbers  = true;
  bool    m_wordWrap         = false;
};

} // namespace Farman
