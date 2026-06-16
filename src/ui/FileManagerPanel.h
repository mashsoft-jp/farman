#pragma once

#include <QWidget>
#include "types.h"
#include "core/DirectoryHistory.h"
#include "core/DirectoryCompare.h"

class QSplitter;
class QKeyEvent;

namespace Farman {

class FileListPane;
class FileListModel;
class LogPane;
class PreviewPane;
class PreviewController;

class FileManagerPanel : public QWidget {
  Q_OBJECT

public:
  explicit FileManagerPanel(QWidget* parent = nullptr);
  ~FileManagerPanel() override;

  // 初期化
  void loadInitialPath();
  void applySettings();  // 設定を両ペインに適用

  // アクセサ
  FileListPane* activePane() const;
  FileListPane* leftPane() const { return m_leftPane; }
  FileListPane* rightPane() const { return m_rightPane; }
  QString currentPath() const;
  QString leftPath() const;
  QString rightPath() const;

  // ペイン操作
  void setActivePane(PaneType pane);

  // レイアウトモード (Dual / Single / Preview)。setSinglePaneMode は
  // 互換 API: 既存呼び出しを壊さないため Single ⇔ Dual の二値トグルとして
  // 残してあるが、内部は setLayoutMode に流す。
  void setLayoutMode(LayoutMode mode);
  LayoutMode layoutMode() const { return m_layoutMode; }
  bool isSinglePaneMode() const { return m_layoutMode == LayoutMode::Single; }
  bool isPreviewMode()    const { return m_layoutMode == LayoutMode::Preview; }
  bool isDualPaneMode()   const { return m_layoutMode == LayoutMode::Dual; }
  void setSinglePaneMode(bool single);
  void togglePaneMode();

  // 同期ブラウズ (Sync Browse): 片方のペインで cd すると、もう一方も
  // 起点からの相対パスで追従する。シングルペイン時は自動的に OFF にし、
  // トグル操作も無効になる。永続化はしない (起動時は常に OFF)。
  bool isSyncBrowseEnabled() const { return m_syncBrowseEnabled; }
  void setSyncBrowseEnabled(bool enabled);
  void toggleSyncBrowse();

  // キーイベント処理
  bool handleKeyEvent(QKeyEvent* event);

protected:
  // 非アクティブペイン側のビューをマウスクリックされたら、そのペインを
  // アクティブにする。
  bool eventFilter(QObject* watched, QEvent* event) override;

public:

  // ファイル操作
  void copySelectedFiles();
  void moveSelectedFiles();
  void deleteSelectedFiles();
  void createDirectory();
  void createFile();
  void renameItem();
  // 選択した複数 (or カーソル行) を BulkRenameDialog で一括リネーム
  void bulkRenameItems();
  void changeAttributes();
  void createArchive();
  void extractArchive();

  // ── ディレクトリ比較 ─────────────────────
  // 左右ペインのカレントディレクトリを突き合わせて、結果を両ペインに着色表示する。
  // モード ON 中は m_compareMode == true。ペイン遷移で自動 OFF。
  void startDirectoryCompare();
  void stopDirectoryCompare();
  bool isDirectoryCompareActive() const { return m_compareMode; }

  // 比較モード中、アクティブペインで条件に合う行をまとめて選択する補助:
  //   Differ:    両側にあるが内容が違う行
  //   OnlyHere:  このペインにしか無い行
  //   Newer:     Differ で、自ペインの mtime が反対ペインの同名ファイルより新しい
  // 既存の選択は維持 (累積) する。.. は対象外。比較モード OFF / 該当 0 件のときは
  // no-op だがログに記録。選択後は既存の c / d / m などで普通に操作可能。
  void selectCompareDiffer();
  void selectCompareOnlyHere();
  void selectCompareNewer();

  // 比較モード中だった場合、現在のオプションで再比較ワーカーを走らせて
  // overlay を最新化する。コピー / 移動 / 削除完了後に呼ぶことで、
  // 操作によって解消した差分行が即 Same 化する。比較モード OFF なら no-op。
  void recompareIfActive();

  // アクティブペインのソート・フィルタ編集ダイアログを開く
  void openSortFilterDialog();

  // 任意のパスへアクティブペインを遷移させる（失敗時は false）
  bool navigateActivePaneTo(const QString& path);

  // 反対側のペインをアクティブと同じディレクトリへ移動
  void syncOtherToActive();
  // 反対側のペインのディレクトリをアクティブへ取り込む（アクティブを反対側と同じに）
  void syncActiveToOther();

  // 外部 / 反対側ペインからのドロップを受け、Copy / Move / Cancel を尋ねて
  // 該当 worker を起動する。destPane が drop を受け取ったペイン。
  void handleExternalDrop(FileListPane* destPane, const QList<QUrl>& urls);

  // ── ディレクトリ履歴 ─────────────────────
  DirectoryHistory&       history(PaneType pane);
  const DirectoryHistory& history(PaneType pane) const;

  // ── 選択操作（コマンドから直接呼ばれる） ─────────
  // カーソル行を選択トグル（カーソル据え置き）
  void toggleSelection();

  // ── ログペイン ──────────────────────────
  void setLogPaneVisible(bool visible);
  bool isLogPaneVisible() const;
  void setLogPaneHeight(int px);

signals:
  void pathChanged(const QString& leftPath, const QString& rightPath);
  // ファイルを開く要求。
  //   filePath:    実際にディスクから読むパス
  //   displayPath: ステータス・タイトルに出す表示用パス。空のときは filePath
  //                をそのまま使う。アーカイブ内エントリの一時展開ケースで
  //                "/path/x.zip!/inner" を見せたい場合のみ別物になる。
  void fileActivated(const QString& filePath,
                     const QString& displayPath = QString());
  // ステータスバー連携用: アクティブペインのフォーカス中ファイルの絶対パス。
  // 何も選択されていない場合は空文字列。
  void activeFocusedPathChanged(const QString& path);
  // ステータスバー連携用: アクティブペインの要約 (件数 / 選択数 / サイズ)。
  void activeSummaryChanged(const QString& summary);
  // 同期ブラウズの ON/OFF が切り替わったとき (UI 更新トリガ)。
  void syncBrowseChanged(bool enabled);
  // ディレクトリ比較モードの ON/OFF が切り替わったとき (メニュー・ステータス
  // バーの UI 更新トリガ)。
  void directoryCompareChanged(bool active);
  // 1 / 2 ペインモードが切り替わったとき (ツールバーのトグル状態同期に使う)。
  // 互換のため bool 版も残すが、新規受信側は layoutModeChanged を使うべき。
  void singlePaneModeChanged(bool single);
  // 3 値のレイアウトモード遷移通知 (Dual / Single / Preview)。
  void layoutModeChanged(LayoutMode mode);
  // ログペインの表示が切り替わったとき (同上)。
  void logPaneVisibleChanged(bool visible);

private slots:
  void onLeftPaneCurrentChanged(const QModelIndex& current, const QModelIndex& previous);
  void onRightPaneCurrentChanged(const QModelIndex& current, const QModelIndex& previous);
  void onLeftFolderButtonClicked();
  void onRightFolderButtonClicked();
  // Tab 連鎖の端 (ファイルリストペインの mode ボタンで Tab / ★ で Shift+Tab) に
  // 達したときに呼ばれるルータ。レイアウトに応じて遷移先を振り分ける:
  //   - Dual    : 対向 FileListPane の ★ (forward) / mode (back)
  //   - Preview : PreviewPane の先頭 (forward) / 末尾 (back)
  //   - Single  : 同ペイン内でラップ (★ / mode)
  void onPaneFocusExit(PaneType from, bool forward);
  // PreviewPane の Tab 連鎖の端から呼ばれるルータ。プレビューを抜けたら
  // アクティブな FileListPane の頭 (forward) / 末尾 (back) に戻す。
  void onPreviewFocusExit(bool forward);

private:
  void setupUi();
  void handleEnterKey();
  // アクティブペインの状態をステータスバー向けに再計算してシグナル送出する
  void emitActivePaneStatus();
  void handleBackspaceKey();
  void handleSpaceKey();
  void handleAsteriskKey();
  void handleSelectAllKey();
  void handleTabKey();
  void updatePathSignal();

  // ペインのパスに対応する保存済み override があればそれを、なければ既定を適用する
  void applyPathSortFilter(PaneType paneType);
  // ペインのパス遷移を一元化するヘルパ（applyPathSortFilter 適用まで面倒を見る）
  bool navigatePane(PaneType paneType, const QString& path);

  // Sync Browse: navigatePane 成功時に呼ばれ、もう一方のペインに同じ
  // 「相対変化」を適用する。oldPath → newPath の差分を取り、もう一方の
  // 現在地から同じ動きをさせる。ターゲットが存在しなければ何もしない。
  // 再帰呼び出しを防ぐため m_syncBrowseInProgress フラグでガード。
  void maybeSyncFollow(PaneType navigatedPane,
                       const QString& oldPath,
                       const QString& newPath);

  QSplitter* m_splitter;
  FileListPane* m_leftPane;
  FileListPane* m_rightPane;
  LogPane*      m_logPane = nullptr;

  PaneType m_activePane;
  LayoutMode m_layoutMode = LayoutMode::Dual;

  // Preview レイアウト用 (右ペイン位置に常駐し、Dual/Single 時は hide される)。
  PreviewPane*       m_previewPane       = nullptr;
  PreviewController* m_previewController = nullptr;

  // Splitter のサイズ記憶 (Dual / Preview を独立に保存)。
  // setLayoutMode で切替前のサイズを退避し、復帰時に元のサイズに戻す。
  // Single は片側が hide されてサイズが歪むため記録対象外。
  QList<int> m_savedSplitterSizesDual;
  QList<int> m_savedSplitterSizesPreview;

  // mode 切替時のスプリッタサイズ適用。キャッシュがあればそれを、無ければ
  // 表示中 2 スロットへ均等配分する。均等配分が実際に ~50/50 になったか
  // (= ペイン最小幅でクランプされていないか) を返す。
  bool applyModeSplitSizes(LayoutMode mode);
  // 既定 50/50 がクランプで偏ったまま確定できていないとき true。
  // スプリッタが最終サイズに広がった Resize で再適用して 50/50 に直す。
  // (起動時の Preview 復元はウィンドウが最終サイズになる前に走るため、
  //  小さい幅で 50/50 を設定すると左ペイン最小幅にクランプされて狭くなる)
  bool m_pendingDefaultSplit = false;

  // ── Sync Browse ─────────────────────
  // ON/OFF はメニュー (View → Sync Browse) または `y` キーで切替。
  // アンカー (起点) は持たず、片方のペインがディレクトリを移動した瞬間に
  // 「同じ相対変化」をもう一方のペインの現在地から再生する方式。
  // 浮遊トグルボタンは将来ツールバー実装のタイミングで再検討する。
  bool    m_syncBrowseEnabled    = false;
  bool    m_syncBrowseInProgress = false;

  DirectoryHistory m_leftHistory;
  DirectoryHistory m_rightHistory;

  // ── ディレクトリ比較 ─────────────────────
  // モード ON のとき左右両ペインに DiffStatus 着色が乗る。
  // ペイン遷移 (navigatePane) で自動 OFF。
  bool             m_compareMode = false;
  CompareOptions   m_compareOptions;
};

} // namespace Farman
