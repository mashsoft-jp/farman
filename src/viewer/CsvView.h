#pragma once

#include <QAbstractTableModel>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <atomic>

class QComboBox;
class QLabel;
class QTableView;
class QToolBar;
class QToolButton;

namespace Farman {

// CSV / TSV ビュアー。
//
//   - 内部は QTableView + 自前 QAbstractTableModel (CsvTableModel)。
//     QTableWidget だと cell ごとに QTableWidgetItem を作るため、数万行
//     クラスのファイルでメモリ / 構築コストが厳しい。Model 経由なら
//     QVector<QStringList> をそのまま参照するだけで済む。
//   - RFC 4180 準拠の quoted-field パース ("a","b,c", エスケープ "" 対応)。
//   - 区切り文字は拡張子と先頭サンプルから自動判定 (',', '\t', ';')。
//     ツールバーから手動切替も可能。
//   - 1 行目を「列ヘッダ」として扱うかを切り替えるトグル。
//   - 並べ替え / セル内検索 / 巨大ファイルの遅延ロードは Phase 2 以降。

// Model: ワーカースレッドでパースした 2 次元配列をそのまま見せる軽量モデル。
class CsvTableModel : public QAbstractTableModel {
  Q_OBJECT
public:
  explicit CsvTableModel(QObject* parent = nullptr);

  void setRows(QVector<QStringList> rows, bool firstRowAsHeader);
  void clear();
  void setFirstRowAsHeader(bool enabled);
  bool firstRowAsHeader() const { return m_firstRowAsHeader; }

  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

private:
  QVector<QStringList> m_rows;          // パース済みの全行 (オリジナル順)
  int                  m_columnCount = 0;  // 全行を見て決めた最大列数
  bool                 m_firstRowAsHeader = true;
};


class CsvView : public QWidget {
  Q_OBJECT

public:
  // 区切り文字。Auto は拡張子 + 先頭サンプル投票で決める。
  enum class Delimiter { Auto, Comma, Tab, Semicolon };

  // 非同期ロード用の中間表現。ワーカーで行配列まで構築する。
  struct PreparedLoad {
    bool       ok = false;
    QString    filePath;
    QByteArray data;
    QString    actualEncoding;
    QChar      actualDelimiter = QChar(',');
    QVector<QStringList> rows;
    qint64     totalSize  = 0;
    qint64     loadedSize = 0;
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

private slots:
  void onEncodingComboChanged(const QString& encoding);
  void onDelimiterComboChanged(int idx);
  void onHeaderToggleChanged(bool useHeader);

private:
  void setupUi();
  void reloadFromBuffer();   // 同じファイルの bytes を別エンコーディング /
                              // 区切りで再パースしてモデルに流す。

  QToolBar*   m_toolbar          = nullptr;
  QComboBox*  m_encodingCombo    = nullptr;
  QComboBox*  m_delimiterCombo   = nullptr;
  QToolButton* m_headerToggle    = nullptr;

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
