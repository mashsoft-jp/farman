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
  // サムネイル表示用の内部 QListView を返す。MainWindow が両ビューにキー
  // event filter を仕掛けるための accessor。
  QAbstractItemView* thumbnailView() const;
  FileListModel* model() const { return m_model; }
  FileListDelegate* delegate() const { return m_delegate; }

  // 表示モード (List / Thumbnail) の取得と切替。setViewMode は QStackedWidget を
  // 切替えるだけで、Settings への保存は呼び出し側 (MainWindow のコマンド) 経由
  // でやる。
  ListViewMode viewMode() const { return m_viewMode; }
  void setViewMode(ListViewMode mode);

  // 自分が Left / Right のどちらのペインかを設定する。FileManagerPanel が
  // 各ペイン構築直後に 1 回だけ呼ぶ。フッタのモード切替ボタンが Settings に
  // 自ペインの設定を保存するときに使う。
  void setPaneType(PaneType pt) { m_paneType = pt; }
  PaneType paneType() const { return m_paneType; }

  // Settings::thumbnailSize() の現在値を model / view / delegate に再適用する。
  // ツールバーや Appearance タブからサイズ変更が起きたときに呼ばれる。
  // モードが List のときも内部状態は更新するので、後で Thumbnail モードに
  // 切り替えた瞬間から正しいサイズが反映される。
  void reapplyThumbnailMetrics();

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
  // 反対ペインから「Tab で頭から入って欲しい」ときに呼ばれる。
  void focusBookmarkLabel();
  // フッタのサムネイル表示モード切替ボタンへフォーカス。Tab 連鎖の末尾。
  // 反対ペインから「Shift+Tab で末尾から入って欲しい」ときに呼ばれる。
  void focusModeButton();
  // フォルダ参照ボタン (📁) へフォーカス。view + Shift+Tab で 1 つ戻る。
  void focusFolderButton();
  // フッタのモード切替ボタンへフォーカス (view + Tab forward の遷移先)。
  // 互換のため focusModeButton() の旧称として残す。
  void focusFooterControls();

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

  // Tab 連鎖の端 (フッタモードボタンで Tab 押下 / ★で Shift+Tab 押下) に
  // 到達したことを通知する。FileManagerPanel が router で対向側
  // (Dual: 反対ペイン / Preview: PreviewPane / Single: 自分の先頭/末尾) に
  // フォーカスを移す。
  void focusExitForward();    // mode + Tab → 対向ペインの先頭 (★)
  void focusExitBackward();   // ★ + Shift+Tab → 対向ペインの末尾 (mode)

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
  // 左右どちらのペインか (FileManagerPanel が constructor 後に setPaneType で設定)。
  PaneType     m_paneType = PaneType::Left;
  QLabel* m_sortFilterStatusLabel;
  // フッタ右端の View Mode 切替 (List / Thumbnail S/M/L の 4 値を 1 つの
  // popup から選択)。クリック / Space / Enter で popup を開き、↑↓ で項目
  // 移動、Enter で確定。Cmd+G ショートカットは 4 段階を順に巡回する。
  QToolButton* m_viewModeButton = nullptr;
  // View Mode メニュー各項目の (action, SVG パス)。テーマ変更時にアイコンを
  // パレット色へ再着色するため保持する。
  QList<QPair<QAction*, QString>> m_viewModeMenuIcons;
  // View Mode ボタン / メニューのアイコンを現在テーマ色で着色し直す。
  void refreshViewModeIcons();
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
