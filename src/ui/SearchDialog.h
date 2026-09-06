#pragma once

#include "core/workers/SearchWorker.h"

#include <QDialog>
#include <QList>
#include <QString>

class QLineEdit;
class QCheckBox;
class QPushButton;
class QTableWidget;
class QLabel;
class QSpinBox;
class QComboBox;
class QDateTimeEdit;
class QRadioButton;

namespace Farman {

class SearchWorker;

// ファイル / ディレクトリ検索ダイアログ。
// - Start path / Name pattern (glob) / 検索対象 / Include subdirectories
// - Search/Stop で別スレッド検索、結果を逐次テーブルに追加
// - ダブルクリック or Go to で accept し、呼び出し側は selectedPath() を取る
// - selectedPath() は見つかったファイル / ディレクトリの絶対パス。呼び出し側は
//   その親ディレクトリへペインを移動し、該当行にカーソルを合わせる
//   (ディレクトリでも中には入らない。探した本人が場所を確認したいはずなので)。
class SearchDialog : public QDialog {
  Q_OBJECT

public:
  SearchDialog(const QString& initialPath, QWidget* parent = nullptr);
  ~SearchDialog() override;

  QString selectedPath() const { return m_selectedPath; }

protected:
  void keyPressEvent(QKeyEvent* event) override;
  bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
  void onBrowse();
  void onSearchOrStop();
  void onGoTo();
  void onResultFound(const QString& path);
  void onFinished(bool success);

private:
  void setupUi(const QString& initialPath);
  void startSearch();
  void stopSearch();
  void appendResultRow(const QString& path);
  // 現在選択されている検索対象。
  SearchTarget currentTarget() const;
  // 検索対象に合わせて、意味の無いフィルタを使えなくする。
  // サイズと内容はディレクトリに対して意味が無いので、「ディレクトリのみ」の
  // ときはチェックボックスごと無効化する (チェックしたのに効かない、を防ぐ)。
  void updateFilterAvailability();

  QLineEdit*    m_pathEdit;
  QPushButton*  m_browseButton;
  QLineEdit*    m_patternEdit;
  QLineEdit*    m_excludeEdit;
  QLineEdit*    m_excludeFileEdit = nullptr;
  QCheckBox*    m_subdirsCheck;
  // 検索対象 (ファイル / ディレクトリ / 両方)。3 択なのでラジオボタン。
  QRadioButton* m_targetFilesRadio = nullptr;
  QRadioButton* m_targetDirsRadio  = nullptr;
  QRadioButton* m_targetBothRadio  = nullptr;
  QPushButton*  m_searchButton;
  QTableWidget* m_resultsTable;
  QLabel*       m_statusLabel;
  QPushButton*  m_closeButton;

  // 拡張フィルタ (size / modified / content)
  QCheckBox*     m_sizeFilterCheck    = nullptr;
  QSpinBox*      m_minSizeSpin        = nullptr;
  QComboBox*     m_minSizeUnit        = nullptr;
  QSpinBox*      m_maxSizeSpin        = nullptr;
  QComboBox*     m_maxSizeUnit        = nullptr;
  QCheckBox*     m_dateFilterCheck    = nullptr;
  QDateTimeEdit* m_dateFromEdit       = nullptr;
  QDateTimeEdit* m_dateToEdit         = nullptr;
  QCheckBox*     m_contentFilterCheck = nullptr;
  QLineEdit*     m_contentEdit        = nullptr;
  QCheckBox*     m_contentCsCheck     = nullptr;
  // サイズ / 内容フィルタの行を丸ごと無効化するための保持 (ラベルを含む)。
  QList<QWidget*> m_sizeFilterWidgets;
  QList<QWidget*> m_contentFilterWidgets;

  SearchWorker* m_worker;
  QString       m_selectedPath;
  bool          m_searching;
};

} // namespace Farman
