#pragma once

#include <QString>
#include <QWidget>

#include <atomic>
#include <memory>

class QStackedWidget;
class QLabel;
class QProgressBar;
class QPushButton;

namespace Farman {

class BinaryView;
class CsvView;
class ImageView;
class IViewerPlugin;
class MarkdownView;
class PdfView;
class TextView;

class ViewerPanel : public QWidget {
  Q_OBJECT

public:
  // どのビュアーで開くかを呼び出し側から強制したい場合に使う。
  // Auto は拡張子・MIME ルーティングに従う通常動作。
  enum class ViewerKind { Auto, Text, Image, Binary, Markdown, Pdf, Csv };

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

  // pluginId から内蔵 ViewerKind を引く。media_viewer など内蔵種別を持たない
  // プラグインは false を返す (埋め込み経路で開く)。
  static bool viewerKindFromPluginId(const QString& pluginId, ViewerKind& kind);

  // 指定した pluginId のビュアーで明示的に開く。内蔵種別を持つものは
  // openFile(kind)、持たないもの (media 等 / 外部プラグイン) は埋め込み経路。
  bool openWithPlugin(const QString& filePath, const QString& pluginId,
                      const QString& displayPath = QString());

  // 現在のファイルパス
  QString currentFilePath() const { return m_currentFilePath; }
  // 現在表示中ビュアーのローカライズ名 (タイトルバー用)。未表示なら空。
  QString currentViewerName() const { return m_currentViewerName; }

  // ビューアをクリア
  void clear();

protected:
  // ロード中表示のときに Esc を「キャンセル」として扱うため。
  void keyPressEvent(QKeyEvent* event) override;

signals:
  void fileOpened(const QString& filePath);
  void fileClosed();
  // ステータスバー連携用: 表示中ファイルのパスと、ビュアー固有の要約。
  // 何も表示していなければ両方空文字列。
  void viewerStatusChanged(const QString& path, const QString& summary);

private slots:
  // statusInfoChanged(QString) を出すプラグインビュー (media 等) からの中継。
  // 本体ステータスバーへ現在ファイルの最新要約を流す。
  void onPluginStatusInfoChanged(const QString& info);

private:
  void setupUi();
  // 第二引数は「ステータス・currentFilePath として記録する表示用パス」。
  // load 自体は filePath (1 つ目) から行う。両者は通常同じ値だが、
  // アーカイブ内のファイルを一時展開して開く場合だけ別物になる。
  bool openTextFile(const QString& filePath, const QString& displayPath);
  bool openImageFile(const QString& filePath, const QString& displayPath);
  bool openBinaryFile(const QString& filePath, const QString& displayPath);
  bool openMarkdownFile(const QString& filePath, const QString& displayPath);
  bool openPdfFile(const QString& filePath, const QString& displayPath);
  bool openCsvFile(const QString& filePath, const QString& displayPath);
  bool openPluginFile(IViewerPlugin* plugin,
                      const QString& filePath,
                      const QString& displayPath);
  void clearPluginView();
  // 本体キーバインド設定の変更時に、開いている全ビュアーへ割り当てを再 push する
  // （一方向：本体→ビュアー）。組込み 6 ビュアーは常駐、プラグインビュアーは
  // 現在表示中のものだけが対象。
  void reapplyViewerShortcuts();
  // ロード中表示に切り替え、ファイル名・サイズを書き込んで再描画する。
  // 同期的なロードに入る前に呼ぶと、ユーザーには「読み込み中…」が見える。
  // ここで新しい cancelToken を発行し、Cancel ボタンにフォーカスを当てる
  // (Enter / Esc でキャンセルできる)。
  void showLoadingState(const QString& filePath);

  // ロード中のキャンセルボタン / Esc で立ち上がる中断要求。token を true に
  // セットし、prepareLoad が早期 return することで waitForFutureWithEventLoop
  // が抜ける。
  void cancelCurrentLoad();

  QStackedWidget* m_stack         = nullptr;
  TextView*       m_textView      = nullptr;
  ImageView*      m_imageView     = nullptr;
  BinaryView*     m_binaryView    = nullptr;
  MarkdownView*   m_markdownView  = nullptr;
  PdfView*        m_pdfView       = nullptr;
  CsvView*        m_csvView       = nullptr;
  QWidget*        m_pluginView    = nullptr;
  // m_pluginView を生成したプラグイン（media / 外部）。ショートカット再 push 用。
  IViewerPlugin*  m_pluginViewPlugin = nullptr;

  // ロード中に出すプレースホルダ。indeterminate な QProgressBar + Cancel
  // ボタンを持つ。Cancel ボタンには表示と同時にフォーカスを当てるので、
  // Enter で即キャンセルできる (Esc も keyPressEvent で同じ動作に転送)。
  QWidget*        m_loadingPage = nullptr;
  QLabel*         m_loadingTitle = nullptr;
  QLabel*         m_loadingDetail = nullptr;
  QProgressBar*   m_loadingBar  = nullptr;
  QPushButton*    m_loadingCancelButton = nullptr;

  // 現在進行中のロードに渡している cancel token。Cancel 操作で true に
  // セットすると、prepareLoad が早期 return する。
  std::shared_ptr<std::atomic<bool>> m_currentCancelToken;

  QString m_currentFilePath;
  QString m_currentViewerName;  // 現在表示中ビュアーのローカライズ名
};

} // namespace Farman
