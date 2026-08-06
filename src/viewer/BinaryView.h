#pragma once

#include "types.h"
#include <QByteArray>
#include <QString>
#include <QWidget>

#include <atomic>

class QComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;

namespace Farman {

class HexView;

// 16 進ダンプ表示用の読み取り専用ビュー。
// 上部のツールバーで unit / endian / encoding をその場で変更でき、16 進アドレスへ
// ジャンプできる。設定ファイルには保存しない (Settings 側のデフォルトは別途
// Appearance タブで編集)。`Settings::settingsChanged` でグローバル設定が変わると
// ローカルの上書きはリセットされる。
//
// 大ファイル対応: ファイルは mmap (QFile::map) して、表示中の行のバイトだけを
// 読み取って整形する (QTableView + 仮想化モデル BinaryHexModel)。全体を読み込んだり
// hex 文字列を一括生成したりしないので、ギガ級でも即座に開ける。mmap 不可の環境
// では seek+read のフォールバックに切り替わる。
class BinaryView : public QWidget {
  Q_OBJECT

public:
  // 非同期ロード用の中間表現。prepareLoad (ワーカースレッド可) が UI を触らずに
  // 生成し、applyPreparedLoad (メインスレッド) が反映する。
  // 旧実装と違い hex 文字列は持たない (整形は表示時にモデルが行う)。prepareLoad は
  // ファイルサイズの取得と、"Auto" 時のエンコード判定 (先頭サンプル) だけを行う。
  struct PreparedLoad {
    bool       ok             = false;
    QString    filePath;
    qint64     totalSize      = 0;
    qint64     loadedSize     = 0;   // 大ファイルでも切り詰めないため totalSize と同じ
    QByteArray data;                 // 互換のため残置 (未使用)
    QString    text;                 // 互換のため残置 (未使用)
    QString    actualEncoding;       // "Auto" 判定結果 or ユーザー指定そのまま
  };

  explicit BinaryView(QWidget* parent = nullptr);
  ~BinaryView() override;

  // 失敗時 (open エラー) は false。
  bool loadFile(const QString& filePath);

  // ワーカースレッドで実行可能なロード処理 (UI 非依存・軽量)。
  // サイズ取得と "Auto" 時のエンコード判定 (先頭サンプルのみ読む) を行う。
  // cancelToken が true になったら早期 return する。maxBytes は互換のため残すが
  // 大ファイルでも切り詰めないため無視する。
  static PreparedLoad prepareLoad(const QString&     filePath,
                                  BinaryViewerUnit   unit,
                                  BinaryViewerEndian endian,
                                  const QString&     encoding,
                                  const std::atomic<bool>* cancelToken = nullptr,
                                  qint64 maxBytes = -1);
  // ワーカーの結果を UI に反映する。必ずメインスレッドから呼ぶこと
  // (ここで mmap を開いてモデルに渡す)。
  void applyPreparedLoad(const PreparedLoad& result);

  // 現在の実効ローカル設定 (ViewerPanel が prepareLoad に渡す)。
  BinaryViewerUnit   currentUnit()     const { return m_unit; }
  BinaryViewerEndian currentEndian()   const { return m_endian; }
  QString            currentEncoding() const { return m_encoding; }

  // 表示中ファイルをクリア。
  void clearContent();

  // ステータスバー表示用の要約 ("56 KB" 等)。
  QString statusInfo() const;

protected:
  // アドレス入力欄で Enter を押したときに、親ウィンドウへ伝播して「ビュアーを
  // 閉じる」が動くのを防ぎ、その場でアドレスジャンプにする。
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void setupUi();
  void syncFromSettings();
  void rebuildEncodingItems();
  void applyModelFormat();       // unit/endian/encoding を HexView へ反映して再描画
  void jumpToAddress();          // アドレス入力欄の 16 進値へスクロール
  // 検索: forward=true で次を、false で前を検索。ヒットしたら HexView で選択表示。
  void doSearch(bool forward);
  // 検索入力 (16 進 or 文字列) を、現在の単位/エンディアン/エンコーディングに
  // 従って検索対象バイト列に変換する。不正なら ok=false。16 進のとき
  // formattedHex != nullptr なら、単位ごとに空白区切りした整形文字列を返す。
  QByteArray buildSearchPattern(bool& ok, QString* formattedHex = nullptr) const;
  // 16 進検索欄のプレースホルダを、表示単位に応じた入力例に更新する。
  void updateSearchPlaceholder();

  QComboBox*      m_unitCombo     = nullptr;
  QComboBox*      m_endianCombo   = nullptr;
  QComboBox*      m_encodingCombo = nullptr;
  QLineEdit*      m_addressEdit   = nullptr;
  // アドレス入力欄の右に表示する、指定可能なアドレスの上限 ("/ 3FFFFFFF" 等)。
  QLabel*         m_addressMaxLabel = nullptr;
  // 検索バー: 16 進 / 文字列のラジオ + 入力欄 + 状態表示。
  QRadioButton*   m_searchHexRadio  = nullptr;
  QRadioButton*   m_searchTextRadio = nullptr;
  QLineEdit*      m_searchEdit      = nullptr;
  QLabel*         m_searchStatus    = nullptr;
  HexView*        m_hex           = nullptr;

  QString m_filePath;
  qint64  m_totalSize = 0;
  // 指定可能なアドレスの上限 (= 最終バイトのオフセット = totalSize - 1)。空ファイル
  // では -1。アドレス入力欄のバリデータがこの値を参照して範囲外入力を弾く。
  qint64  m_maxAddress = -1;

  // 表示に使う実効設定 (ローカル上書き)。Settings 変更時に再同期される。
  BinaryViewerUnit   m_unit     = BinaryViewerUnit::Byte1;
  BinaryViewerEndian m_endian   = BinaryViewerEndian::Little;
  QString            m_encoding       = QStringLiteral("UTF-8");
  QString            m_actualEncoding = QStringLiteral("UTF-8");
};

} // namespace Farman
