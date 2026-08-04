#pragma once

#include "types.h"
#include "core/FileItem.h"
#include "core/DirectoryCompare.h"
#include "core/ThumbnailCache.h"
#include <QAbstractItemModel>
#include <QFileSystemWatcher>
#include <QFileIconProvider>
#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <memory>

namespace Farman {

class ArchiveContext;

class FileListModel : public QAbstractItemModel {
  Q_OBJECT

public:
  // 列定義。先頭 4 つはずっと存在していた既定列、後ろ 5 つは Behavior タブの
  // 「Columns」設定で 2 画面 / 1 画面ごとに ON/OFF できる拡張列。
  // 並びは固定。Name は常に表示 (UI 側でも非表示にできない)。
  enum Column {
    Name         = 0,
    Type         = 1,
    Size         = 2,
    LastModified = 3,
    Created      = 4,
    Permissions  = 5,
    Attributes   = 6,
    Owner        = 7,
    Group        = 8,
    LinkTarget   = 9,
    ColumnCount
  };

  // カスタムロール
  enum Role {
    FileItemRole = Qt::UserRole + 1,  // FileItem* を返す
    IsSelectedRole,
    IsDirRole,
    IsHiddenRole
  };

  explicit FileListModel(QObject* parent = nullptr);
  ~FileListModel() override;

  // 所属ペインのアクティブ状態。非アクティブ時は Settings 側で有効化されていれば
  // 専用のカラーに切替える。
  void setActive(bool active);
  bool isActive() const { return m_active; }

  // 1 画面モードか 2 画面モードか。サイズ・更新日時の表示形式は
  // モード別に Settings から読むので、モードが切り替わったら表示も
  // 切り替える。
  void setSinglePaneMode(bool single);
  bool isSinglePaneMode() const { return m_singlePane; }

  // サムネイル表示モードの ON/OFF。FileListPane::setViewMode から呼ばれ、
  // Thumbnail のとき data(DecorationRole) が画像ファイルに対し実画像を
  // setScaledSize で decode したアイコンを返すようになる。Phase 1 では
  // 同期 decode で、スクロール時のブロッキングは Phase 2 の非同期キャッシュ
  // で解消する。切替時は dataChanged を発火して再描画させる。
  void setThumbnailEnabled(bool enabled);
  bool thumbnailEnabled() const { return m_thumbnailEnabled; }
  // サムネイルの外接サイズ (px)。アスペクト比を保ったまま内接させる。
  // 変更時は dataChanged を発火する。
  void setThumbnailPixelSize(int sizePx);
  int  thumbnailPixelSize() const { return m_thumbnailPixelSize; }

  // ── パス操作 ──────────────────────────────
  QString currentPath() const;
  bool    setPath(const QString& path);  // false = アクセス不可
  void    refresh();

  // ── ソート ───────────────────────────────
  void setSortSettings(
    SortKey       key,
    Qt::SortOrder order       = Qt::AscendingOrder,
    SortKey       key2nd      = SortKey::Name,
    SortDirsType  dirsType    = SortDirsType::First,
    bool          dotFirst    = true,
    Qt::CaseSensitivity cs    = Qt::CaseInsensitive
  );

  // ── フィルタ ──────────────────────────────
  void setNameFilters(const QStringList& patterns);  // glob: {"*.cpp","*.h"}
  void setAttrFilter(AttrFilterFlags flags);
  void toggleHiddenFiles();  // 隠しファイルの表示/非表示をトグル

  // 即時フィルタ (Quick Filter Bar): 部分一致 (case-insensitive)。
  // 既存の nameFilters / attrFilter とは独立した「その場限り」のフィルタで、
  // setPath() (= ディレクトリ移動) で自動的にクリアされる。
  // 空文字なら無効。先頭・末尾の空白は呼び出し側で trim 済みにすること。
  void    setLiveFilter(const QString& text);
  QString liveFilter() const { return m_liveFilter; }

  // ── 状態取得 ──────────────────────────────
  SortKey       sortKey() const { return m_sortKey; }
  Qt::SortOrder sortOrder() const { return m_sortOrder; }
  SortKey       sortKey2nd() const { return m_sortKey2nd; }
  SortDirsType  sortDirsType() const { return m_dirsType; }
  bool          sortDotFirst() const { return m_dotFirst; }
  Qt::CaseSensitivity sortCS() const { return m_cs; }
  AttrFilterFlags attrFilter() const { return m_attrFilter; }
  QStringList   nameFilters() const { return m_nameFilters; }

  // 全件 (フィルタを適用する前) のエントリ数 ("..", 隠しファイル含む)。
  // フッタに「N / M items」のような表示をするときの母数として使う。
  int totalCount() const { return m_allEntries.size(); }

  // ── アーカイブ内ブラウジング ────────────────
  // 現在ペインがアーカイブの中を表示中か否か。
  // 書込ガード (コピー先・移動・削除等) や、ディレクトリ比較の発動可否を
  // 判定する窓口として使う。
  bool isInArchiveMode() const { return m_archiveContext != nullptr; }
  // 現在開いているアーカイブの絶対パス (ローカル FS 上の元 zip)。
  // アーカイブモード以外では空。
  QString archivePath() const;
  // アーカイブ内の「カレント」パス。先頭 '/' 必須、ルートは "/"。
  // アーカイブモード以外では空。
  QString archiveInnerPath() const { return m_archiveInnerPath; }
  // 現在開いているアーカイブのメタデータキャッシュ。
  // アーカイブモード以外は nullptr。raw pointer は呼び出し中のみ有効。
  const ArchiveContext* archiveContext() const { return m_archiveContext.get(); }

  // ── ディレクトリ比較オーバーレイ ──────────────
  // 名前 → DiffStatus の overlay を取り込むと比較モードに入り、ファイル行の
  // 背景/前景色を「Differ」「OnlyHere」用テーマ色に上書きする。
  // overlay 空 + 比較モード OFF が通常状態。
  void setCompareOverlay(const CompareOverlay& overlay);
  void clearCompareOverlay();
  bool inCompareMode() const { return m_compareMode; }
  // 現在のオーバーレイ参照。FileManagerPanel が "Newer" 等の複合条件で
  // 行を走査するときに使う (反対ペインの mtime と組み合わせて判定するため)。
  const CompareOverlay& compareOverlay() const { return m_compareOverlay; }

  // 比較モード中、overlay 上で指定 DiffStatus に該当する行を選択状態にする
  // (累積、既存の選択は維持)。クリアしてから選び直したい場合は呼び出し側で
  // 事前に setSelectedAll(false) を呼ぶ。".." は対象外。
  // 比較モード OFF / overlay 空のときは no-op。戻り値は新たに選択した行数。
  int selectByCompareStatus(DiffStatus s);

  // 直近の setPath で失敗した際のユーザー向けエラーメッセージ。成功時 or
  // 一度も失敗していない時は空文字。アーカイブ open 失敗 (パスワード付き /
  // フォーマット非対応 / キャンセル) の理由を呼び出し側に伝えるための窓口。
  QString lastLoadError() const { return m_lastLoadError; }

  // ── アイテムアクセス ──────────────────────
  const FileItem* itemAt(const QModelIndex& index) const;
  const FileItem* itemAt(int row) const;
  int             rowCount(const QModelIndex& parent = {}) const override;
  int             columnCount(const QModelIndex& parent = {}) const override;

  // ── 選択操作 ──────────────────────────────
  QList<int>             selectedRows() const;
  QList<const FileItem*> selectedItems() const;
  QStringList            selectedFilePaths() const;
  void setSelected(int row, bool selected);
  void setSelectedAll(bool selected);
  void toggleSelected(int row);
  void invertSelection();
  bool isAllSelected() const;  // 全て選択されているか（".." 除く）

  // ── QAbstractItemModel 必須実装 ───────────
  QModelIndex index(int row, int col,
                    const QModelIndex& parent = {}) const override;
  QModelIndex parent(const QModelIndex& index) const override;
  QVariant    data(const QModelIndex& index,
                   int role = Qt::DisplayRole) const override;
  QVariant    headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;

signals:
  void pathChanged(const QString& newPath);
  void loadFailed(const QString& path, const QString& reason);

private slots:
  // ThumbnailCache::thumbnailReady を受けて、該当 row の DecorationRole を
  // 再描画させる。key.path が m_entries にあれば dataChanged を発火、無ければ
  // 無視 (ディレクトリ移動でこのモデルが別 path を表示中のときに古い結果が
  // 届くケース)。
  void onThumbnailReady(const Farman::ThumbnailKey& key, const QPixmap& pixmap);

private:
  // m_allEntries (setPath で読み込んだ全件) から m_entries (フィルタ後の表示用) を
  // 構築してソートする。シェアド ptr で参照を持つため、選択状態はフィルタ
  // 切替で消えない (FileItem 自体を共有)。
  void applyFilterAndSort();
  // 1 件が現在の attr / name / live フィルタを通るか判定。".." は常に true。
  bool passesFilters(const FileItem* item) const;
  int compareItems(const FileItem* a, const FileItem* b, SortKey key) const;

  QString                          m_currentPath;
  // m_allEntries: setPath で読み込んだディレクトリの全件 (隠し含む、".." も先頭に)。
  // m_entries:    現在のフィルタを通過したサブセット。表示はこちらを基準にする。
  // 両者は shared_ptr<FileItem> を共有するので選択状態などは一致する。
  QList<std::shared_ptr<FileItem>> m_allEntries;
  QList<std::shared_ptr<FileItem>> m_entries;
  QFileSystemWatcher               m_watcher;
  QFileIconProvider                m_iconProvider;

  // ファイルアイコンのキャッシュ (絶対パス → QIcon)。DecorationRole は
  // 描画のたびに呼ばれ、macOS の QFileIconProvider::icon() は LaunchServices
  // 経由で 1 件 ms オーダーと遅い。カーソル移動で現/旧行が繰り返し再描画される
  // たびに同じファイルのアイコンを取り直すと、キー長押しで遅延が積み重なる。
  // 一度取得したアイコンをパス単位でキャッシュし、再フェッチを防ぐ。ディレクトリ
  // 再読込 (beginResetModel) 時にクリアする。const な data() から使うため mutable。
  mutable QHash<QString, QIcon>    m_iconCache;

  // ソート設定
  SortKey             m_sortKey    = SortKey::Name;
  SortKey             m_sortKey2nd = SortKey::Name;
  Qt::SortOrder       m_sortOrder  = Qt::AscendingOrder;
  SortDirsType        m_dirsType   = SortDirsType::First;
  bool                m_dotFirst   = true;
  Qt::CaseSensitivity m_cs         = Qt::CaseInsensitive;

  // フィルタ設定
  QStringList     m_nameFilters;
  AttrFilterFlags m_attrFilter = AttrFilter::None;
  // 即時フィルタ (Quick Filter Bar) の文字列。空 = 無効。
  // 部分一致 (case-insensitive) でファイル名にマッチさせる。
  QString         m_liveFilter;

  // ペインのアクティブ状態（表示カラーの切替用）
  bool            m_active = true;
  // 現在のペイン表示モード（サイズ・日時のフォーマット切替用）
  bool            m_singlePane = false;

  // サムネイル表示モード関連 (Phase 1: 同期 decode、Phase 2 で非同期化)。
  bool            m_thumbnailEnabled    = false;
  int             m_thumbnailPixelSize  = 160;  // Medium の px 値

  // ── アーカイブ内ブラウジング ────────────────
  // 非 null のときアーカイブモード。元 zip の全エントリメタデータを
  // 共有保持するキャッシュ。
  std::shared_ptr<ArchiveContext> m_archiveContext;
  // アーカイブ内の現在ディレクトリ。先頭 '/' 必須、ルートは "/"。
  // m_archiveContext が null のときは未使用。
  QString                          m_archiveInnerPath;

  // ── ディレクトリ比較オーバーレイ ──────────────
  // m_compareMode が true のとき m_compareOverlay の各エントリの DiffStatus を
  // 参照して背景色 / 前景色を上書きする。setPath / navigatePane で OFF に戻す。
  bool                             m_compareMode = false;
  CompareOverlay                   m_compareOverlay;

  // 直近の setPath 失敗時のエラーメッセージ (アーカイブ open 失敗等)
  QString                          m_lastLoadError;
};

} // namespace Farman
