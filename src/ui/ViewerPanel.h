#pragma once

#include <QString>
#include <QWidget>

class QStackedWidget;
class QLabel;
class QProgressBar;

namespace Farman {

class BinaryView;
class ImageView;
class MarkdownView;
class TextView;

class ViewerPanel : public QWidget {
  Q_OBJECT

public:
  // どのビュアーで開くかを呼び出し側から強制したい場合に使う。
  // Auto は拡張子・MIME ルーティングに従う通常動作。
  enum class ViewerKind { Auto, Text, Image, Binary, Markdown };

  explicit ViewerPanel(QWidget* parent = nullptr);
  ~ViewerPanel() override;

  // ファイルを開く（kind=Auto のとき Settings の対応表に従って自動判定）
  //   filePath:    実際に読み込むディスク上のファイルパス (= 実体)
  //   kind:        強制ビュアー種別 (Auto なら自動判定)
  //   displayPath: ステータスバー / シグナルに出す「ユーザーから見たパス」。
  //                空のときは filePath をそのまま使う。アーカイブ内のファイルを
  //                一時展開して開くケースで、ユーザーには "/path/x.zip!/inner"
  //                を見せ、内部の load は temp パスから行う、という用途に使う。
  bool openFile(const QString& filePath,
                ViewerKind     kind        = ViewerKind::Auto,
                const QString& displayPath = QString());

  // 拡張子 / MIME のルーティングだけを抜き出した静的ヘルパ。Auto を渡すと
  // 内部の判定で Text / Image / Binary のいずれかに解決して返す。
  // External モード (独立ウィンドウ) でも同じ振り分けを使うので、Inline
  // 専用にせず公開する。
  static ViewerKind resolveAuto(const QString& filePath);

  // 現在のファイルパス
  QString currentFilePath() const { return m_currentFilePath; }

  // ビューアをクリア
  void clear();

signals:
  void fileOpened(const QString& filePath);
  void fileClosed();
  // ステータスバー連携用: 表示中ファイルのパスと、ビュアー固有の要約。
  // 何も表示していなければ両方空文字列。
  void viewerStatusChanged(const QString& path, const QString& summary);

private:
  void setupUi();
  // 第二引数は「ステータス・currentFilePath として記録する表示用パス」。
  // load 自体は filePath (1 つ目) から行う。両者は通常同じ値だが、
  // アーカイブ内のファイルを一時展開して開く場合だけ別物になる。
  bool openTextFile(const QString& filePath, const QString& displayPath);
  bool openImageFile(const QString& filePath, const QString& displayPath);
  bool openBinaryFile(const QString& filePath, const QString& displayPath);
  bool openMarkdownFile(const QString& filePath, const QString& displayPath);
  // ロード中表示に切り替え、ファイル名・サイズを書き込んで再描画する。
  // 同期的なロードに入る前に呼ぶと、ユーザーには「読み込み中…」が見える。
  void showLoadingState(const QString& filePath);

  QStackedWidget* m_stack         = nullptr;
  TextView*       m_textView      = nullptr;
  ImageView*      m_imageView     = nullptr;
  BinaryView*     m_binaryView    = nullptr;
  MarkdownView*   m_markdownView  = nullptr;

  // ロード中に出すプレースホルダ。indeterminate な QProgressBar を持つ。
  QWidget*        m_loadingPage = nullptr;
  QLabel*         m_loadingTitle = nullptr;
  QLabel*         m_loadingDetail = nullptr;
  QProgressBar*   m_loadingBar  = nullptr;

  QString m_currentFilePath;
};

} // namespace Farman
