# farman ヘッダファイル設計

Claude Code への指示: このファイルに定義されたヘッダを `src/` 以下に作成すること。
実装（.cpp）はヘッダのシグネチャに従って作成すること。

---

## ファイル一覧と依存関係

```
types.h
  └── FileItem.h
        └── FileListModel.h
  └── WorkerBase.h
        ├── CopyWorker.h
        ├── MoveWorker.h
        └── RemoveWorker.h
  └── IViewerPlugin.h
        └── ViewerDispatcher.h
  └── ICommand.h
        └── CommandRegistry.h
  └── KeyBindingManager.h
  └── Settings.h
```

---

## `src/types.h`

```cpp
#pragma once

#include <QtGlobal>

namespace Farman {

// ペインの種別
enum class PaneType {
  Left = 0,
  Right,
  Count
};

// ソートキー
enum class SortKey {
  Name,
  Size,
  Type,
  LastModified
};

// ディレクトリのソート位置
enum class SortDirsType {
  First,   // ディレクトリを先頭に
  Last,    // ディレクトリを末尾に
  Mixed    // ファイルと混在
};

// ファイルサイズの表示形式
enum class FileSizeFormat {
  Bytes,   // バイト表示
  SI,      // KB / MB / GB (1000進)
  IEC,     // KiB / MiB / GiB (1024進)
  Auto     // 自動選択
};

// 属性フィルタのフラグ
enum class AttrFilter : quint32 {
  None        = 0x00,
  ShowHidden  = 0x01,  // 隠しファイルを表示
  ShowSystem  = 0x02,  // システムファイルを表示 (Windows)
  DirsOnly    = 0x04,  // ディレクトリのみ
  FilesOnly   = 0x08,  // ファイルのみ
};
Q_DECLARE_FLAGS(AttrFilterFlags, AttrFilter)
Q_DECLARE_OPERATORS_FOR_FLAGS(AttrFilterFlags)

// ワーカーの操作種別
enum class WorkerOperation {
  Copy,
  Move,
  Remove
};

// 上書き確認の結果
enum class OverwriteResult {
  Yes,
  YesAll,
  No,
  NoAll,
  Cancel
};

} // namespace Farman
```

---

## `src/core/FileItem.h`

```cpp
#pragma once

#include "types.h"
#include <QFileInfo>
#include <QString>
#include <QDateTime>

namespace Farman {

class FileItem {
public:
  explicit FileItem(const QFileInfo& info);

  // 基本情報
  QString   name()         const;
  QString   absolutePath() const;
  QString   suffix()       const;   // 拡張子（小文字）
  QString   mimeType()     const;   // 遅延取得
  qint64    size()         const;   // ディレクトリは -1
  QDateTime lastModified() const;

  // 種別
  bool isDir()     const;
  bool isFile()    const;
  bool isHidden()  const;
  bool isSymLink() const;
  bool isDotDot()  const;   // ".." エントリ

  // 選択状態
  bool isSelected() const;
  void setSelected(bool selected);

  // 生の QFileInfo が必要な場合
  const QFileInfo& fileInfo() const;

private:
  QFileInfo m_info;
  bool      m_selected = false;
  mutable QString m_mimeType;  // 遅延初期化
};

} // namespace Farman
```

---

## `src/model/FileListModel.h`

```cpp
#pragma once

#include "types.h"
#include "core/FileItem.h"
#include <QAbstractItemModel>
#include <QFileSystemWatcher>
#include <memory>

namespace Farman {

class FileListModel : public QAbstractItemModel {
  Q_OBJECT

public:
  // 列定義
  enum Column {
    Name         = 0,
    Size         = 1,
    Type         = 2,
    LastModified = 3,
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

  // ── アイテムアクセス ──────────────────────
  const FileItem* itemAt(const QModelIndex& index) const;
  const FileItem* itemAt(int row) const;
  int             rowCount(const QModelIndex& parent = {}) const override;
  int             columnCount(const QModelIndex& parent = {}) const override;

  // ── 選択操作 ──────────────────────────────
  QList<int>             selectedRows() const;
  QList<const FileItem*> selectedItems() const;
  void setSelected(int row, bool selected);
  void setSelectedAll(bool selected);
  void toggleSelected(int row);
  void invertSelection();

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

private:
  void applyFilterAndSort();

  QString                          m_currentPath;
  QList<std::shared_ptr<FileItem>> m_entries;
  QFileSystemWatcher               m_watcher;

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
};

} // namespace Farman
```

---

## `src/core/workers/WorkerBase.h`

```cpp
#pragma once

#include "types.h"
#include <QThread>
#include <QString>
#include <atomic>

namespace Farman {

// Worker → UI への進捗通知
struct WorkerProgress {
  QString currentFile;  // 現在処理中のファイル名
  qint64  processed;    // 処理済みバイト数
  qint64  total;        // 合計バイト数（不明な場合は -1）
  int     filesDone;    // 完了ファイル数
  int     filesTotal;   // 合計ファイル数
};

class WorkerBase : public QThread {
  Q_OBJECT

public:
  explicit WorkerBase(QObject* parent = nullptr);
  ~WorkerBase() override;

  // キャンセル要求（スレッドセーフ）
  void requestCancel();
  bool isCancelled() const;

signals:
  void progressUpdated(const WorkerProgress& progress);
  void overwriteRequired(
    const QString& srcPath,
    const QString& dstPath,
    OverwriteResult* result  // Worker側でブロックして待つ
  );
  void errorOccurred(const QString& path, const QString& message);
  void finished(bool success);

protected:
  void run() override = 0;

  // 上書き確認（UIスレッドと同期）
  OverwriteResult askOverwrite(
    const QString& srcPath,
    const QString& dstPath
  );

  std::atomic<bool> m_cancelRequested{false};
};

} // namespace Farman
```

---

## `src/core/workers/CopyWorker.h`

```cpp
#pragma once

#include "WorkerBase.h"
#include <QStringList>

namespace Farman {

class CopyWorker : public WorkerBase {
  Q_OBJECT
public:
  CopyWorker(
    const QStringList& srcPaths,
    const QString&     dstDir,
    QObject*           parent = nullptr
  );

protected:
  void run() override;

private:
  bool copyEntry(const QString& src, const QString& dst);

  QStringList m_srcPaths;
  QString     m_dstDir;
};

} // namespace Farman
```

---

## `src/core/workers/MoveWorker.h`

```cpp
#pragma once

#include "WorkerBase.h"
#include <QStringList>

namespace Farman {

class MoveWorker : public WorkerBase {
  Q_OBJECT
public:
  MoveWorker(
    const QStringList& srcPaths,
    const QString&     dstDir,
    QObject*           parent = nullptr
  );

protected:
  void run() override;

private:
  QStringList m_srcPaths;
  QString     m_dstDir;
};

} // namespace Farman
```

---

## `src/core/workers/RemoveWorker.h`

```cpp
#pragma once

#include "WorkerBase.h"
#include <QStringList>

namespace Farman {

class RemoveWorker : public WorkerBase {
  Q_OBJECT
public:
  explicit RemoveWorker(
    const QStringList& paths,
    bool               toTrash = true,  // false = 完全削除
    QObject*           parent  = nullptr
  );

protected:
  void run() override;

private:
  QStringList m_paths;
  bool        m_toTrash;
};

} // namespace Farman
```

---

## `src/viewer/IViewerPlugin.h`

```cpp
#pragma once

#include <QWidget>
#include <QStringList>
#include <QtPlugin>

namespace Farman {

class IViewerPlugin {
public:
  virtual ~IViewerPlugin() = default;

  // プラグイン識別情報
  virtual QString     pluginId()   const = 0;  // "text_viewer"
  virtual QString     pluginName() const = 0;  // "テキストビュアー"
  virtual int         priority()   const { return 0; }  // 高いほど優先

  // 対応ファイルの宣言
  virtual QStringList supportedExtensions() const = 0;  // {"txt","log","cpp"}
  virtual QStringList supportedMimeTypes()  const = 0;  // {"text/plain"}

  // このファイルを開けるか（より細かい判定が必要な場合にoverride）
  virtual bool canHandle(const QString& filePath) const;

  // ビュアーウィジェット生成（呼び出し元がownershipを持つ）
  virtual QWidget* createViewer(
    const QString& filePath,
    QWidget*       parent = nullptr
  ) = 0;
};

} // namespace Farman

#define FarmanIViewerPlugin_iid "com.farman.IViewerPlugin/3.0"
Q_DECLARE_INTERFACE(Farman::IViewerPlugin, FarmanIViewerPlugin_iid)
```

---

## `src/viewer/ViewerDispatcher.h`

```cpp
#pragma once

#include "IViewerPlugin.h"
#include <QObject>
#include <QDir>
#include <memory>

namespace Farman {

class ViewerDispatcher : public QObject {
  Q_OBJECT

public:
  static ViewerDispatcher& instance();

  // 組み込みビュアーの登録（起動時に呼ぶ）
  void registerBuiltins();

  // プラグインディレクトリからロード
  void loadPlugins(const QDir& pluginDir);

  // ファイルに対応するビュアーを返す（nullptr = 対応なし）
  IViewerPlugin* resolvePlugin(const QString& filePath) const;

  // ビュアーウィジェットを生成して返す
  // 対応ビュアーがない場合は HexViewer にフォールバック
  QWidget* createViewer(
    const QString& filePath,
    QWidget*       parent = nullptr
  ) const;

  // 全登録プラグイン一覧
  QList<IViewerPlugin*> allPlugins() const;

private:
  explicit ViewerDispatcher(QObject* parent = nullptr);
  void registerPlugin(std::shared_ptr<IViewerPlugin> plugin);

  QList<std::shared_ptr<IViewerPlugin>> m_plugins;
};

} // namespace Farman
```

---

## `src/keybinding/ICommand.h`

```cpp
#pragma once

#include <QString>
#include <functional>

namespace Farman {

class ICommand {
public:
  virtual ~ICommand() = default;

  virtual QString id()       const = 0;  // "copy", "move", "delete"
  virtual QString label()    const = 0;  // "コピー"（設定画面に表示）
  virtual QString category() const { return "general"; }

  virtual bool isEnabled()   const { return true; }
  virtual void execute()           = 0;
};

// ラムダからコマンドを手軽に作るヘルパー
class LambdaCommand : public ICommand {
public:
  LambdaCommand(
    QString               id,
    QString               label,
    std::function<void()> fn,
    QString               category = "general"
  );

  QString id()       const override;
  QString label()    const override;
  QString category() const override;
  void    execute()        override;

private:
  QString               m_id;
  QString               m_label;
  QString               m_category;
  std::function<void()> m_fn;
};

} // namespace Farman
```

---

## `src/keybinding/CommandRegistry.h`

```cpp
#pragma once

#include "ICommand.h"
#include <QObject>
#include <QMap>
#include <memory>

namespace Farman {

class CommandRegistry : public QObject {
  Q_OBJECT

public:
  static CommandRegistry& instance();

  void registerCommand(std::shared_ptr<ICommand> cmd);

  // コマンドIDから実行
  bool execute(const QString& commandId);

  // コマンドIDからコマンドを取得
  ICommand* command(const QString& commandId) const;

  // 全コマンド一覧（設定画面のキーバインド一覧に使用）
  QList<ICommand*> allCommands() const;
  QList<ICommand*> commandsByCategory(const QString& category) const;

private:
  explicit CommandRegistry(QObject* parent = nullptr);
  QMap<QString, std::shared_ptr<ICommand>> m_commands;
};

} // namespace Farman
```

---

## `src/keybinding/KeyBindingManager.h`

```cpp
#pragma once

#include <QObject>
#include <QKeySequence>
#include <QMap>

namespace Farman {

class KeyBindingManager : public QObject {
  Q_OBJECT

public:
  static KeyBindingManager& instance();

  // キー → コマンドID のマッピング
  void    bind(const QKeySequence& key, const QString& commandId);
  void    unbind(const QKeySequence& key);
  QString commandFor(const QKeySequence& key) const;
  bool    isBound(const QKeySequence& key) const;

  // デフォルトバインディングをロード
  void loadDefaults();

  // QSettings から読み書き
  void loadFromSettings();
  void saveToSettings() const;

  // 全バインディング（設定画面に表示するため）
  QMap<QKeySequence, QString> allBindings() const;

private:
  explicit KeyBindingManager(QObject* parent = nullptr);
  QMap<QKeySequence, QString> m_bindings;
};

} // namespace Farman
```

---

## `src/settings/Settings.h`

```cpp
#pragma once

#include "types.h"
#include <QObject>
#include <QFont>
#include <QColor>
#include <QMap>

namespace Farman {

// ファイル種別ごとのカラーリング設定
struct ColorRule {
  QString pattern;     // glob パターン: "*.cpp", "*.h"
  QColor  foreground;
  QColor  background;
  bool    bold = false;
};

// ペインごとの設定
struct PaneSettings {
  QString         path;
  SortKey         sortKey      = SortKey::Name;
  Qt::SortOrder   sortOrder    = Qt::AscendingOrder;
  SortKey         sortKey2nd   = SortKey::Name;
  SortDirsType    sortDirsType = SortDirsType::First;
  bool            sortDotFirst = true;
  Qt::CaseSensitivity sortCS   = Qt::CaseInsensitive;
  QStringList     nameFilters;
  AttrFilterFlags attrFilter   = AttrFilter::None;
};

class Settings : public QObject {
  Q_OBJECT

public:
  static Settings& instance();

  // ── ペイン設定 ─────────────────────────
  PaneSettings paneSettings(PaneType pane) const;
  void setPaneSettings(PaneType pane, const PaneSettings& s);

  // ── 表示設定 ───────────────────────────
  QFont  font()                        const;
  void   setFont(const QFont& font);

  FileSizeFormat fileSizeFormat()       const;
  void           setFileSizeFormat(FileSizeFormat fmt);

  QString        dateTimeFormat()       const;
  void           setDateTimeFormat(const QString& fmt);

  // ── カラーリング ───────────────────────
  QList<ColorRule> colorRules()         const;
  void             setColorRules(const QList<ColorRule>& rules);

  // ── 起動設定 ───────────────────────────
  bool restoreLastPath()                const;
  void setRestoreLastPath(bool restore);

  // ── 読み書き ───────────────────────────
  void load();
  void save() const;

signals:
  void settingsChanged();

private:
  explicit Settings(QObject* parent = nullptr);

  PaneSettings     m_paneSettings[static_cast<int>(PaneType::Count)];
  QFont            m_font;
  FileSizeFormat   m_fileSizeFormat  = FileSizeFormat::Auto;
  QString          m_dateTimeFormat  = "yyyy/MM/dd HH:mm:ss";
  QList<ColorRule> m_colorRules;
  bool             m_restoreLastPath = true;
};

} // namespace Farman
```
