#pragma once

#include <QByteArray>
#include <QString>
#include <QWidget>

#include <atomic>

class QTextBrowser;
class QToolBar;
class QToolButton;

namespace Farman {

// Markdown ファイル表示用ウィジェット (Phase 0: CommonMark + GFM)。
//
//   - 内部は QTextBrowser + QTextDocument::setMarkdown() で整形描画
//   - Qt 6 標準で CommonMark + GitHub Flavored Markdown のサブセットに対応
//     (見出し / リスト / 表 / タスクリスト / 取り消し線 / 自動リンク /
//      画像 / コードブロック (色なし monospace))
//   - 数式 (KaTeX) / Mermaid / シンタックスハイライトは非対応 (将来課題)
//   - 相対パス画像 (`![alt](path)`) は Markdown ファイルのディレクトリを
//     setSearchPaths でベースに設定して解決する
//   - ツールバー「ソース表示」トグルで、整形 / 生 Markdown を切り替え可能
class MarkdownView : public QWidget {
  Q_OBJECT
public:
  // 非同期ロード用の中間表現 (TextView と同じ作法)。
  // ワーカースレッドで生成して main へ渡せるよう、QWidget には触らない。
  struct PreparedLoad {
    bool       ok = false;
    QString    filePath;
    QByteArray data;
    QString    actualEncoding;
    QString    text;       // デコード済み Markdown ソース
    qint64     totalSize  = 0;
    qint64     loadedSize = 0;
  };

  explicit MarkdownView(QWidget* parent = nullptr);

  bool loadFile(const QString& filePath);

  // ワーカースレッド対応のロード関数。TextView と同じく cancelToken /
  // maxBytes を持つ (プレビューモードのデバウンス + truncate に追従)。
  static PreparedLoad prepareLoad(const QString& filePath,
                                  const QString& userEncoding,
                                  const std::atomic<bool>* cancelToken = nullptr,
                                  qint64 maxBytes = -1);
  void applyPreparedLoad(const PreparedLoad& result);

  QString currentUserEncoding() const { return m_encoding; }

  // 表示中ファイルをクリア
  void clearContent();

  // ステータスバー表示用の要約 ("1234 lines · UTF-8 · 56 KB" 等)
  QString statusInfo() const;

private slots:
  void onToggleRawSource(bool rawSource);

private:
  void setupUi();
  void renderCurrent();  // m_text を m_rawSource フラグで整形 or 生表示する

  QToolBar*     m_toolbar      = nullptr;
  QToolButton*  m_rawSourceButton = nullptr;
  QTextBrowser* m_browser      = nullptr;

  QString    m_filePath;
  QByteArray m_data;
  QString    m_actualEncoding;
  QString    m_text;          // 元の Markdown ソース (= decode 後の生文字列)
  qint64     m_totalSize  = 0;
  qint64     m_loadedSize = 0;

  // ユーザーが選択中のエンコード (UI 上書き、Settings 由来)。
  QString    m_encoding = QStringLiteral("Auto");

  // false = setMarkdown で整形表示 (デフォルト)、true = setPlainText で生 Markdown 表示
  bool       m_rawSource = false;
};

} // namespace Farman
