#pragma once

#include <QAbstractTableModel>
#include <QByteArray>
#include <QChar>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <atomic>

class QComboBox;
class QLabel;
class QLineEdit;
class QTableView;
class QToolBar;
class QToolButton;

namespace Farman {

// CSV / TSV ビュアー。
//
//   - 内部は QTableView + 自前 QAbstractTableModel (CsvTableModel)。
//   - RFC 4180 準拠の quoted-field パース ("a","b,c", エスケープ "" 対応)。
//   - 区切り文字は拡張子と先頭サンプルから自動判定 (',', '\t', ';')。
//     ツールバーから手動切替も可能。
//   - 1 行目を「列ヘッダ」として扱うかを切り替えるトグル。
//   - **遅延ロード**: ワーカースレッドではテキスト全体のデコードと
//     行オフセット index 構築のみを行い、各セルの文字列パースは
//     `data()` が呼ばれた瞬間に LRU キャッシュ越しに走る。これによって
//     数十万〜100 万行クラスの CSV でも、初回表示は index 走査だけで済む。
//   - 全文検索はテキスト全体に対する `QString::indexOf` でマッチを集め、
//     位置を行オフセット index で (row, col) に逆引きする。

// Model: バイト列 + 行オフセット index を保持し、必要な行だけパースする。
class CsvTableModel : public QAbstractTableModel {
  Q_OBJECT
public:
  explicit CsvTableModel(QObject* parent = nullptr);

  // 新しい入力ソース。`text` はデコード済みの全文 (ワーカーで作る)。
  // `rowStarts` は各行の `text` 内オフセット (最後に sentinel として
  // text.size() を持つ)。`columnCount` は事前スキャンで見えた最大列数。
  void setSource(QString text,
                  QChar delimiter,
                  QVector<int> rowStarts,
                  int columnCount,
                  bool firstRowAsHeader);
  void clear();
  void setFirstRowAsHeader(bool enabled);
  bool firstRowAsHeader() const { return m_firstRowAsHeader; }

  // 検索: 文字列が部分一致するセルを yellow ハイライト + マッチ位置リスト
  // を保持。空文字列でクリア。戻り値はヒット件数。
  int setSearchString(const QString& needle, bool caseSensitive);
  // 0-based のマッチ index → (view 表示上の row, column)。範囲外で {-1,-1}。
  QPair<int,int> matchAt(int matchIndex) const;
  int            matchCount() const { return m_matches.size(); }
  QString        searchString() const { return m_search; }

  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

  // 全行数 (ヘッダ行を含む生の行数)。
  int totalRowCountRaw() const { return m_rowStarts.size() - 1; }

private:
  // 指定の data 行 (0-based、ヘッダ行を引いていない物理 index) を返す。
  // 必要に応じてパース + キャッシュ。範囲外で空 QStringList。
  QStringList rowAtRaw(int rawRow) const;
  // 単一行を text から抽出してパース。
  QStringList parseOneRow(int startOffset, int endOffset) const;
  // 検索インデックスを再構築。text 全体に対して indexOf で一気に集める。
  // ヒット位置 → (row, col) は m_rowStarts + 行内パースで逆引き。
  void rebuildMatches();
  // text 内のオフセット → 該当する物理 row (0-based)。二分探索。
  int findRowForOffset(int offset) const;
  // 物理 row 内のオフセット (row 開始からの差分) → そのセルの列 index。
  int findColForOffsetWithinRow(int rawRow, int offsetInRow) const;

  QString        m_text;          // デコード済み全文
  QChar          m_delimiter      = QChar(',');
  // m_rowStarts.size() は 行数 + 1。最後の要素は sentinel として text.size()。
  QVector<int>   m_rowStarts;
  int            m_columnCount    = 0;
  bool           m_firstRowAsHeader = true;

  // LRU キャッシュ。data() が直近に触ったセルの行を覚えておく。
  // 大量の行を順次描画するときの再パースを抑える。
  mutable QHash<int, QStringList> m_rowCache;
  mutable QList<int>              m_cacheOrder;   // 古い順
  static constexpr int            kCacheLimit = 4096;

  QString m_search;
  bool    m_searchCaseSensitive = false;
  // マッチした (view 表示上の row, col) のリスト。表示順で並ぶ。
  QVector<QPair<int,int>> m_matches;
};


class CsvView : public QWidget {
  Q_OBJECT

public:
  // 区切り文字。Auto は拡張子 + 先頭サンプル投票で決める。
  enum class Delimiter { Auto, Comma, Tab, Semicolon };

  // 非同期ロード用の中間表現。ワーカーで「全文デコード + 行オフセット index」
  // までを構築する。実際のセル文字列パースはモデル側で遅延実行する。
  struct PreparedLoad {
    bool         ok = false;
    QString      filePath;
    QByteArray   data;
    QString      actualEncoding;
    QChar        actualDelimiter = QChar(',');
    QString      text;          // デコード済みテキスト全体
    QVector<int> rowStarts;     // 各行の text 内開始 offset + sentinel
    int          columnCount    = 0;  // 事前スキャンで見えた最大列数
    qint64       totalSize  = 0;
    qint64       loadedSize = 0;
  };

  explicit CsvView(QWidget* parent = nullptr);
  ~CsvView() override;

  bool loadFile(const QString& filePath);

  // ワーカースレッド対応のロード関数。
  //   userEncoding : "Auto" なら uchardet で検出。
  //   delimiter    : Delimiter::Auto なら拡張子 + 投票で決定。
  //   maxBytes > 0 のときはファイル先頭 maxBytes だけ読み (Text と同じ作法)。
  static PreparedLoad prepareLoad(const QString& filePath,
                                  const QString& userEncoding,
                                  Delimiter      delimiter,
                                  const std::atomic<bool>* cancelToken = nullptr,
                                  qint64 maxBytes = -1);
  void applyPreparedLoad(const PreparedLoad& result);

  QString currentUserEncoding() const { return m_userEncoding; }
  Delimiter currentDelimiter() const { return m_userDelimiter; }

  void clearContent();

  // ステータスバー表示用 ("CSV · N rows · M cols · UTF-8 · 12 KB")
  QString statusInfo() const;

protected:
  // Cmd/Ctrl+F のグローバルキャプチャ用 (TextView / PdfView / MarkdownView と
  // 同じ作法)。
  bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
  void onEncodingComboChanged(const QString& encoding);
  void onDelimiterComboChanged(int idx);
  void onHeaderToggleChanged(bool useHeader);
  void onFindTextChanged(const QString& text);
  void findNext();
  void findPrevious();
  void focusFindInput();

private:
  void setupUi();
  void reloadFromBuffer();   // 同じファイルの bytes を別エンコーディング /
                              // 区切りで再パースしてモデルに流す。
  // 現在の m_findEdit のテキスト + case トグルでモデルを再検索し、最初の
  // ヒットへ移動する (検索の入り直し)。
  void runSearchAndJump();
  // 0-based のマッチ index へ table を scroll + 選択。範囲外なら何もしない。
  void jumpToMatch(int matchIndex);
  void updateFindStatus();

  QToolBar*   m_toolbar          = nullptr;
  QComboBox*  m_encodingCombo    = nullptr;
  QComboBox*  m_delimiterCombo   = nullptr;
  QToolButton* m_headerToggle    = nullptr;

  // 検索 UI
  QLineEdit*   m_findEdit     = nullptr;
  QToolButton* m_findPrevBtn  = nullptr;
  QToolButton* m_findNextBtn  = nullptr;
  QToolButton* m_findCsButton = nullptr;
  QLabel*      m_findStatus   = nullptr;
  int          m_currentMatchIndex = -1;

  QTableView*    m_table  = nullptr;
  CsvTableModel* m_model  = nullptr;

  // 現在表示中のファイル状態。ファイル切替時 / encoding・delimiter 変更時に
  // m_data から再パースするために bytes も保持しておく。
  QString    m_filePath;
  QByteArray m_data;
  QString    m_actualEncoding;
  QChar      m_actualDelimiter = QChar(',');
  qint64     m_totalSize  = 0;
  qint64     m_loadedSize = 0;

  // ユーザー操作で上書きされた値 (Settings 由来初期 / UI で変更)
  QString   m_userEncoding  = QStringLiteral("Auto");
  Delimiter m_userDelimiter = Delimiter::Auto;
};

} // namespace Farman
