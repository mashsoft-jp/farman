# farman アーキテクチャ設計書

## 概要

farman は Qt6/C++20 で実装される2画面ファイルマネージャーです。
Model-View パターンを基本とし、プラグイン機構により拡張可能な設計を採用します。

---

## アーキテクチャ原則

1. **レイヤー分離**: UI / ビジネスロジック / データアクセス層を明確に分離
2. **Model-View パターン**: Qt の Model/View フレームワークを活用
3. **非同期処理**: ファイル操作は QThread でバックグラウンド実行
4. **プラグインアーキテクチャ**: ビュアーは動的ロード可能なプラグインとして実装
5. **設定の永続化**: QSettings による設定管理

---

## レイヤー構成

```
┌─────────────────────────────────────┐
│          UI Layer (Qt Widgets)       │
│  MainWindow / FilePane / LogWidget   │
└─────────────┬───────────────────────┘
              │ signals/slots
┌─────────────┴───────────────────────┐
│       Business Logic Layer           │
│  Operations / Viewers / Settings     │
└─────────────┬───────────────────────┘
              │
┌─────────────┴───────────────────────┐
│         Model Layer                  │
│  FileSystemModel / FilterSortProxy   │
└─────────────┬───────────────────────┘
              │
┌─────────────┴───────────────────────┐
│      Data Access Layer               │
│  QFileInfo / QDir / QFile            │
└─────────────────────────────────────┘
```

---

## ディレクトリ構造

```
src/
├── main.cpp                 # エントリーポイント
├── ui/                      # UI層
│   ├── MainWindow.{h,cpp}   # メインウィンドウ
│   ├── FilePane.{h,cpp}     # ファイルリストパネル
│   ├── LogWidget.{h,cpp}    # ログ表示
│   └── StatusBar.{h,cpp}    # ステータスバー
├── models/                  # モデル層
│   ├── FileSystemModel.{h,cpp}      # ファイルシステムモデル
│   ├── FileItem.{h,cpp}             # ファイル情報
│   └── FilterSortProxy.{h,cpp}      # フィルタ・ソート
├── operations/              # ファイル操作
│   ├── FileOperation.{h,cpp}        # 操作基底クラス
│   ├── CopyOperation.{h,cpp}        # コピー
│   ├── MoveOperation.{h,cpp}        # 移動
│   └── DeleteOperation.{h,cpp}      # 削除
├── viewer/                  # ビュアー (実体は src/viewer/)
│   ├── IViewerPlugin.h              # 外部ビュアープラグイン IF
│   ├── ViewerDispatcher.{h,cpp}     # ビュアー振り分け
│   ├── TextView.{h,cpp}             # テキストビュアー
│   ├── TextViewerWindow.{h,cpp}     # 同・独立ウィンドウ (External モード)
│   ├── ImageView.{h,cpp}            # 画像ビュアー (Exif / ICC / 回転 / 透明)
│   ├── ImageViewerWindow.{h,cpp}    # 同・独立ウィンドウ
│   ├── BinaryView.{h,cpp}           # バイナリ (16 進ダンプ)
│   ├── BinaryViewerWindow.{h,cpp}   # 同・独立ウィンドウ
│   ├── MarkdownView.{h,cpp}         # Markdown (CommonMark + GFM + 検索)
│   ├── MarkdownViewerWindow.{h,cpp} # 同・独立ウィンドウ
│   ├── PdfView.{h,cpp}              # PDF (Qt PDF / 検索 / ページ / ズーム)
│   ├── PdfViewerWindow.{h,cpp}      # 同・独立ウィンドウ
│   ├── CsvView.{h,cpp}              # CSV / TSV (QTableView ベース)
│   └── CsvViewerWindow.{h,cpp}      # 同・独立ウィンドウ
├── settings/                # 設定管理
│   ├── Settings.{h,cpp}             # 設定マネージャー
│   ├── KeyBindings.{h,cpp}          # キーバインド
│   └── ColorScheme.{h,cpp}          # カラースキーム
├── core/                    # コア機能
│   ├── BookmarkManager.{h,cpp}      # ブックマーク管理
│   ├── HistoryManager.{h,cpp}       # 履歴管理
│   └── SearchEngine.{h,cpp}         # ファイル検索
└── utils/                   # ユーティリティ
    ├── FileUtils.{h,cpp}            # ファイル操作補助
    └── PathUtils.{h,cpp}            # パス処理補助
```

---

## 主要クラス設計

### UI層

#### MainWindow
```cpp
class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  MainWindow(QWidget *parent = nullptr);

private:
  FilePane *leftPane_;
  FilePane *rightPane_;
  LogWidget *logWidget_;
  StatusBar *statusBar_;

  FilePane *activePane_;  // 現在アクティブなパネル

  void setupUi();
  void connectSignals();
  void handleKeyPress(QKeyEvent *event);
};
```

#### FilePane
```cpp
class FilePane : public QWidget {
  Q_OBJECT
public:
  FilePane(QWidget *parent = nullptr);

  void setPath(const QString &path);
  QString currentPath() const;
  QList<FileItem> selectedFiles() const;

signals:
  void pathChanged(const QString &path);
  void selectionChanged();

private:
  QTableView *view_;
  FileSystemModel *model_;
  FilterSortProxy *proxyModel_;
  HistoryManager *history_;
};
```

### Model層

#### FileSystemModel
```cpp
class FileSystemModel : public QAbstractTableModel {
  Q_OBJECT
public:
  // QAbstractTableModel インターフェース
  int rowCount(const QModelIndex &parent) const override;
  int columnCount(const QModelIndex &parent) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;

  // ファイルシステム操作
  void setRootPath(const QString &path);
  FileItem fileItem(const QModelIndex &index) const;

public slots:
  void refresh();

private:
  QString rootPath_;
  QList<FileItem> items_;

  void loadDirectory();
};
```

#### FilterSortProxy
```cpp
class FilterSortProxy : public QSortFilterProxyModel {
  Q_OBJECT
public:
  void setNameFilter(const QString &pattern);  // glob パターン
  void setShowHidden(bool show);
  void setDirectoriesFirst(bool first);

protected:
  bool filterAcceptsRow(int source_row,
                       const QModelIndex &source_parent) const override;
  bool lessThan(const QModelIndex &left,
                const QModelIndex &right) const override;

private:
  QString nameFilter_;
  bool showHidden_ = false;
  bool directoriesFirst_ = true;
};
```

### Operations層

#### FileOperation (基底クラス)
```cpp
class FileOperation : public QThread {
  Q_OBJECT
public:
  enum class Type { Copy, Move, Delete };

  virtual ~FileOperation() = default;

  void cancel();

signals:
  void progress(qint64 current, qint64 total);
  void currentFile(const QString &fileName);
  void finished(bool success);
  void error(const QString &message);

protected:
  void run() override = 0;

  std::atomic<bool> cancelled_{false};
};
```

#### CopyOperation
```cpp
class CopyOperation : public FileOperation {
  Q_OBJECT
public:
  CopyOperation(const QList<QString> &sources,
                const QString &destination);

protected:
  void run() override;

private:
  QList<QString> sources_;
  QString destination_;

  bool copyFile(const QString &src, const QString &dst);
  bool copyDirectory(const QString &src, const QString &dst);
};
```

### Viewer層

#### IViewerPlugin (インターフェース)
```cpp
class IViewerPlugin {
public:
  virtual ~IViewerPlugin() = default;

  // プラグイン情報
  virtual QString name() const = 0;
  virtual QString version() const = 0;
  virtual QStringList supportedExtensions() const = 0;

  // ビューア生成
  virtual QWidget* createViewer(const QString &filePath,
                                 QWidget *parent = nullptr) = 0;
};

// Qt プラグインマクロ
#define IViewerPlugin_iid "com.farman.IViewerPlugin/1.0"
Q_DECLARE_INTERFACE(IViewerPlugin, IViewerPlugin_iid)
```

#### ViewerManager
```cpp
class ViewerManager : public QObject {
  Q_OBJECT
public:
  static ViewerManager& instance();

  void loadPlugins(const QString &pluginDir);
  QWidget* createViewer(const QString &filePath, QWidget *parent = nullptr);

private:
  ViewerManager() = default;

  QMap<QString, IViewerPlugin*> plugins_;  // 拡張子 -> プラグイン

  void registerBuiltinViewers();
};
```

### Settings層

#### Settings
```cpp
class Settings : public QObject {
  Q_OBJECT
public:
  static Settings& instance();

  // キーバインド
  KeyBindings& keyBindings();

  // 表示設定
  QFont font() const;
  void setFont(const QFont &font);

  ColorScheme& colorScheme();

  // その他の設定
  bool showHiddenFiles() const;
  void setShowHiddenFiles(bool show);

private:
  Settings();

  QSettings settings_;
  KeyBindings keyBindings_;
  ColorScheme colorScheme_;
};
```

---

## データフロー

### ディレクトリ読み込み

```
User input (path change)
    ↓
FilePane::setPath()
    ↓
FileSystemModel::setRootPath()
    ↓
FileSystemModel::loadDirectory()  # QDir でファイル一覧取得
    ↓
emit dataChanged()
    ↓
FilterSortProxy::filterAcceptsRow()  # フィルタ適用
    ↓
QTableView::dataChanged()  # 表示更新
```

### ファイルコピー

```
User input (copy command)
    ↓
FilePane::selectedFiles()
    ↓
new CopyOperation(sources, dest)
    ↓
operation->start()  # QThread::start()
    ↓
[Background Thread]
CopyOperation::run()
    ├─→ emit progress()
    ├─→ emit currentFile()
    └─→ emit finished()
    ↓
[Main Thread]
slot: onOperationFinished()
    ↓
LogWidget::addMessage()
FileSystemModel::refresh()
```

### プラグインロード

```
Application startup
    ↓
ViewerManager::loadPlugins()
    ↓
QDir::entryList("*.dylib|*.dll|*.so")
    ↓
for each plugin file:
  QPluginLoader::instance()
  qobject_cast<IViewerPlugin*>()
  plugins_[ext] = plugin
```

---

## 設計パターン

### Singleton パターン
- `ViewerManager::instance()`
- `Settings::instance()`

理由: アプリケーション全体で単一のインスタンスを共有

### Observer パターン (Qt Signal/Slot)
- Model の変更を View に通知
- 非同期操作の進捗通知

### Strategy パターン
- `FileOperation` の派生クラス (Copy/Move/Delete)
- 各操作を独立したクラスとして実装

### Proxy パターン
- `FilterSortProxy`: モデルに対するフィルタ・ソート機能を追加

### Plugin パターン
- `IViewerPlugin`: ビューアの動的ロード

---

## 非同期処理設計

### QThread の使用方針

1. **ファイル操作**: `FileOperation` (QThread 継承)
   - `run()` メソッドで処理を実装
   - `signals` で進捗・完了を通知
   - `cancelled_` フラグでキャンセル対応

2. **UIスレッド**: メインスレッド
   - すべての UI 操作
   - Signal/Slot 接続は `Qt::QueuedConnection` (自動)

### スレッド安全性

- `std::atomic<bool>` でキャンセルフラグ
- Qt の Signal/Slot 機構 (スレッド間通信は安全)
- UI 要素への直接アクセス禁止 (signal 経由)

---

## プラグインシステム

### プラグインの実装方法

```cpp
// プラグイン実装例
class PdfViewerPlugin : public QObject, public IViewerPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID IViewerPlugin_iid)
  Q_INTERFACES(IViewerPlugin)

public:
  QString pluginId() const override { return "pdf_viewer"; }
  QString pluginName() const override { return "PDF Viewer"; }
  QStringList supportedExtensions() const override { return {"pdf"}; }
  QStringList supportedMimeTypes() const override { return {"application/pdf"}; }

  QWidget* createViewer(const QString &filePath,
                        QWidget *parent,
                        const PluginContext &ctx) override {
    Q_UNUSED(ctx);
    return new PdfViewerWidget(filePath, parent);
  }
};
```

### プラグインディレクトリ

- 既定: `QStandardPaths::AppDataLocation + "/plugins"`
  - macOS: `~/Library/Application Support/Farman/farman/plugins`
  - Linux: `~/.local/share/Farman/farman/plugins`
  - Windows: `%APPDATA%/Farman/farman/plugins`
- Settings → General → Viewer Plugins で任意ディレクトリに変更可能。
- Help → Plugins... でロード結果とエラー理由を確認できる。

---

## パフォーマンス最適化

### 大量ファイル対応 (10,000+ ファイル)

1. **遅延ロード**: 表示範囲のみデータ取得 (Model の実装で対応)
2. **QTableView**: 仮想化により表示行のみ描画
3. **ソート・フィルタ**: `QSortFilterProxyModel` で効率的に実装
4. **キャッシュ**: ファイル情報の適切なキャッシング

### 起動時間最適化 (目標 1秒以内)

1. **プラグイン遅延ロード**: 必要になるまでロードしない
2. **設定の軽量化**: QSettings の読み込み最小限
3. **初期表示の優先**: UI表示を優先、詳細情報は後から取得

---

## テスト戦略

### 単体テスト (Qt Test)

- Model 層: `FileSystemModel`, `FilterSortProxy`
- Operations 層: `CopyOperation`, `MoveOperation`
- Utils 層: `FileUtils`, `PathUtils`

### 統合テスト

- UI とモデルの連携
- プラグインシステムのロード
- ファイル操作の実行

### 手動テスト

- クロスプラットフォーム動作確認
- パフォーマンステスト (大量ファイル)
- UI/UX の確認

---

## 今後の拡張性

### プラグイン拡張

- ビュアープラグイン以外にも対応可能
  - ファイル操作プラグイン (FTP/SFTP 等)
  - アーカイブ形式プラグイン

### テーマシステム

- `ColorScheme` をベースに UI テーマ切り替え
- QSS (Qt Style Sheets) との連携

### スクリプト対応

- Qt Script / Lua 等によるマクロ機能

---

## 依存ライブラリ

### 必須
- Qt6 Core
- Qt6 Widgets

### オプション (プラグインで使用)
- Qt6 Gui (画像ビュアー)
- サードパーティライブラリ (プラグイン実装次第)

---

## まとめ

このアーキテクチャは以下を実現します:

1. ✅ **保守性**: レイヤー分離により変更の影響範囲を限定
2. ✅ **拡張性**: プラグインシステムにより機能追加が容易
3. ✅ **パフォーマンス**: 非同期処理と効率的なモデル設計
4. ✅ **テスト容易性**: 各層を独立してテスト可能
5. ✅ **クロスプラットフォーム**: Qt の抽象化により複数 OS 対応
