#pragma once

#include "types.h"

#include <QUrl>
#include <QWidget>

class QAbstractItemView;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QToolButton;
class QTableView;
class QFileSystemWatcher;
class QTimer;

namespace Farman {

class FileListModel;
class FileListDelegate;
class FileListThumbnailDelegate;
class FileListView;
class FileListThumbnailView;
class ClickableLabel;

class FileListPane : public QWidget {
  Q_OBJECT

public:
  explicit FileListPane(QWidget* parent = nullptr);
  ~FileListPane() override;

  // アクセサ。view() は QTableView* で公開し、D&D 等の派生機能は
  // 内部で FileListView を使って実現している (List モード時の中身)。
  // サムネイル表示モード時も視点を切替えたいときは activeView() を使う。
  QTableView* view() const;
  // 現在の表示モードに応じた active な item view を返す (Thumbnail モード時は
  // 内部 QListView)。selection / current / scroll bar の取得など、ビュー型を
  // 問わない API はこちらを使う。
  QAbstractItemView* activeView() const;
  FileListModel* model() const { return m_model; }
  FileListDelegate* delegate() const { return m_delegate; }

  // 表示モード (List / Thumbnail) の取得と切替。setViewMode は QStackedWidget を
  // 切替えるだけで、Settings への保存は呼び出し側 (MainWindow のコマンド) 経由
  // でやる。
  ListViewMode viewMode() const { return m_viewMode; }
  void setViewMode(ListViewMode mode);

  // パス操作
  QString currentPath() const;
  bool setPath(const QString& path);

  // アクティブ状態
  void setActive(bool active);

  // 現在のソート・フィルタ条件をフッタに表示するラベルを更新する
  void refreshSortFilterStatus();

  // 設定からパス表示のカラー等を再適用する
  void refreshAppearance();

  // 1 画面 / 2 画面モードに応じて、表示する列を Settings から再適用する。
  // setSinglePaneMode と applySettings の両方から呼ばれる。
  void applyColumnVisibility(bool singlePane);

  // アドレスバーを編集モードにしてフォーカスを移す。Tab キーで FileList から
  // アドレスバーへ飛ばすために FileManagerPanel から呼ばれる。
  void focusAddressBar();
  // ★ ブックマークラベルへフォーカス。Tab 連鎖の起点として使う。
  void focusBookmarkLabel();

  // 即時フィルタ (Quick Filter Bar) のトグル表示。
  //   - 非表示 → 表示 + フォーカス + テキスト全選択
  //   - 表示中 → 非表示 + フィルタクリア + ファイルリストへフォーカス戻す
  // FileManagerPanel から `view.quick_filter` コマンド経由で呼ばれる。
  void toggleQuickFilter();

signals:
  void folderButtonClicked();
  void currentChanged(const QModelIndex& current, const QModelIndex& previous);
  // 外部 / 反対側ペインからのファイルドロップ。FileManagerPanel が拾って
  // Copy / Move のいずれかをユーザーに尋ねる。
  void externalUrlsDropped(const QList<QUrl>& urls);

public:
  // 現在のパスのブックマーク登録状態を切り替える。
  // - 登録済みなら即削除。
  // - 未登録なら名前入力ダイアログを出し、OK で追加、キャンセルで何もしない。
  void toggleBookmarkForCurrentPath();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
  void onFolderButtonClicked();
  void onBookmarkButtonClicked();
  void onCurrentChanged(const QModelIndex& current, const QModelIndex& previous);
  void onHeaderClicked(int section);
  // 現在のパスがブックマーク済みかどうかを indicator に反映する
  void refreshBookmarkIndicator();
  // 外部からカレントディレクトリが変更されたとき (Finder 等) の遅延更新。
  // QFileSystemWatcher のイベントを debounce してから model を refresh する。
  void onExternalDirectoryChanged();
  // アドレスバー編集
  void enterAddressEdit();    // 表示モード → 編集モード
  void cancelAddressEdit();   // 編集を破棄して表示モードへ戻す
  void commitAddressEdit();   // Enter: 入力されたパスへ移動

  // 即時フィルタ
  void onQuickFilterTextEdited(const QString& text);

private:
  void setupUi();
  void leaveAddressEdit(bool restoreText);

  // 即時フィルタ用ヘルパ
  void hideQuickFilter();   // バーを閉じてフィルタを解除、フォーカスを view へ

  QLineEdit*       m_addressEdit = nullptr;
  bool             m_addressEditing = false;  // 現在編集モードか
  ClickableLabel*  m_bookmarkLabel;
  QToolButton*     m_folderButton;
  // 表示の中心: QStackedWidget で List ビュー (FileListView/QTableView) と
  // Thumbnail ビュー (FileListThumbnailView/QListView) を切替える。両方とも
  // 同じ FileListModel と同じ selectionModel を共有しているので、選択 / カーソル
  // 行位置はモード切替で失われない。
  QStackedWidget* m_viewStack = nullptr;
  FileListView* m_view;
  FileListThumbnailView* m_thumbnailView = nullptr;
  FileListThumbnailDelegate* m_thumbnailDelegate = nullptr;
  ListViewMode m_viewMode = ListViewMode::List;
  QLabel* m_sortFilterStatusLabel;
  // 即時フィルタの 1 行入力欄。デフォルトは非表示。
  QLineEdit*       m_quickFilterEdit = nullptr;
  FileListModel* m_model;
  FileListDelegate* m_delegate;
  // Settings の "Auto" を復元するために、構築時の Qt 既定行高を覚えておく
  int m_defaultRowHeight = 0;

  // 外部 (Finder 等) からのファイル増減を検知するためのウォッチャ。
  // setPath で監視対象を切り替える。短時間に複数イベントが発火することが
  // あるので m_refreshDebounce で間引いてから model->refresh() を呼ぶ。
  QFileSystemWatcher* m_dirWatcher     = nullptr;
  QTimer*             m_refreshDebounce = nullptr;
};

} // namespace Farman
