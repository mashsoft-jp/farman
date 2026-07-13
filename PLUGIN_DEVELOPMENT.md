# farman プラグイン開発ガイド

farman は 2 系統の**外部プラグイン**を動的ロードできる。プラグインは farman 本体とは
別にビルド・配布し、ユーザーが所定のディレクトリに置くと起動時に読み込まれる。

| 種別 | インターフェース | 役割 | 導入先 (下記) |
|------|------------------|------|----------------|
| ビュアー | `IViewerPlugin` (`src/viewer/IViewerPlugin.h`) | ファイル内容の表示 | `plugins/viewers/` |
| アーカイブ | `IArchivePlugin` (`src/core/IArchivePlugin.h`) | 圧縮書庫の列挙・展開 | `plugins/archives/` |

参照実装:
- ビュアー (同梱公式): `plugins/official/*`
- アーカイブ (外部配布): 別リポジトリ **farman-plugin-lzh**（`.lzh`/`.lha` 対応）

---

## 1. ABI とバージョニング

各インターフェースは **IID** で ABI を識別する。プラグインの `Q_PLUGIN_METADATA` の IID が
farman 本体の IID と一致しない場合、ロード時に `qobject_cast` で**安全に拒否**される
(クラッシュはしない)。

| インターフェース | 現在の IID |
|------------------|-----------|
| `IViewerPlugin`  | `com.farman.IViewerPlugin/4.0` |
| `IArchivePlugin` | `com.farman.IArchivePlugin/1.0` |

- ABI 互換が壊れる変更 (仮想関数の追加・削除・並び替え等) をしたら **IID のメジャー番号を
  上げる**。旧 IID のプラグインは自動で弾かれる。
- プラグイン側は IID を直書きせず、ヘッダのマクロ (`FarmanIViewerPlugin_iid` /
  `FarmanIArchivePlugin_iid`) を使うこと。

### プラグイン自身のバージョン `version()`

- `version()` は**プラグインの配布版数**を返す (ABI とは別物)。Settings → Plugins に表示される。
- 同梱公式ビュアーは `IViewerPlugin::version()` の既定実装が **ビルド時の `FARMAN_VERSION`**
  を返すので、farman 本体と同じ版数になる。
- 外部プラグインは `version()` を **自前の版数で override** する。CMake の `project(VERSION)`
  をコンパイル定義で埋め込むのが簡単 (farman-plugin-lzh の `LZH_PLUGIN_VERSION` 参照)。

### priority (読み込み優先度)

`priority()` は同一拡張子を複数プラグインが名乗ったときの解決に使う (小さいほど優先)。

- **外部プラグインは 0〜9999**。範囲外はロード時にエラーで拒否。
- **10000 以上は同梱公式の予約域** (例: PDF/CSV/Markdown=10000、コアビュアーは
  99996〜99999)。
- 外部プラグインは **`author()` が必須** (空だと拒否)。

---

## 2. 外部プラグインの作り方

### 2.1 実装

インターフェースを実装した `QObject` 派生クラスを 1 つ用意し、Qt プラグインとして公開する。

```cpp
#include "core/IArchivePlugin.h"   // vendoring したヘッダ (下記 2.3)
#include <QObject>

class MyArchivePlugin : public QObject, public Farman::IArchivePlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID FarmanIArchivePlugin_iid)   // マクロを使う
  Q_INTERFACES(Farman::IArchivePlugin)
public:
  QString pluginId()   const override { return QStringLiteral("my_archive"); }
  QString pluginName() const override { return QStringLiteral("My Archive"); }
  QString author()     const override { return QStringLiteral("Your Name"); }   // 必須
  int     priority()   const override { return 100; }                           // 0〜9999
  QString version()    const override { return QStringLiteral("1.0.0"); }
  QStringList supportedExtensions() const override { return {"xyz"}; }
  ArchiveListResult listEntries(...) override { /* ... */ }
  bool extractEntry(...) override { /* ... */ }
};
```

- **セキュリティはホスト側が担保する**。アーカイブプラグインは raw なエントリ一覧を返すだけで
  よく、Zip Slip 防御 (`..`/NUL 除去) や合成ディレクトリ生成は farman 本体が行う。
- ワーカースレッドで呼ばれるので、ロケール等のスレッド状態に注意
  (例: libarchive の LHA リーダは UTF-8 ロケールで CP932 名に失敗するため、
  farman-plugin-lzh は呼び出し区間だけ `uselocale("C")` している)。

### 2.2 CMake (スタンドアロン)

farman 本体とは独立してビルドする。`qt_add_plugin` で MODULE を作り、依存 (Qt・libarchive 等) を
リンクする。farman-plugin-lzh の `CMakeLists.txt` がテンプレート。

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Core5Compat)
qt_add_plugin(MyArchivePlugin CLASS_NAME MyArchivePlugin
  src/MyArchivePlugin.cpp
  farman-sdk/core/IArchivePlugin.cpp        # 既定 canHandle 実装
)
target_include_directories(MyArchivePlugin PRIVATE src farman-sdk)
```

### 2.3 farman-sdk の vendoring

プラグインは farman 本体のソースツリー無しで単体ビルドできるよう、必要な共有ファイルを
`farman-sdk/` にコピー (vendoring) して同梱する。

- 必須: 実装するインターフェースのヘッダ (`IArchivePlugin.h` / `IViewerPlugin.h`) と、それが
  参照する型 (`ArchiveEntry.h` 等)、既定実装の `.cpp` (`IArchivePlugin.cpp` の `canHandle`)。
- **IID を farman 本体と一致させること**。本体が IID を上げたら vendored ヘッダも更新して再ビルド。

---

## 3. ビルドと配布 (重要)

### 3.1 ABI 一致

**プラグインは配布版 farman と同じ Qt / 依存ライブラリでビルドすること。** バージョンがずれると
ロード時のシンボル解決や `qobject_cast` に失敗する。farman の CI は **Qt 6.10.3** を使う
(`.github/workflows/release.yml` の `QT_VERSION`)。プラグイン CI もこれに合わせる。

### 3.2 実行時リンク (OS 別)

配布 farman は Qt / libarchive を **アプリバンドル内に同梱** (macdeployqt 等) している。
外部ディレクトリに置いたプラグインが、その同梱ライブラリを再利用できるようにする:

- **macOS**: ビルド直後のプラグインは Qt/libarchive をビルド環境のパスで参照している。
  `install_name_tool` で **`@executable_path/../Frameworks/<leaf>`** に書き換える。
  `@executable_path` は「ロードする側の実行ファイル (= farman 本体)」を指すので、外部配置でも
  `farman.app/Contents/Frameworks/` の既ロード済みフレームワークに解決される
  (Qt バージョンや framework の install-name id に非依存)。**`@rpath` は不可**
  (プラグイン自身の RPATH で解決され farman のバンドルに届かない)。
- **Linux / Windows**: 同名の Qt / 依存ライブラリが farman プロセスに既にロードされていれば
  再利用されるため、追加の書き換えは不要 (バージョン一致が前提)。

### 3.3 CI テンプレート

farman-plugin-lzh の `.github/workflows/release.yml` が 3 OS 分をビルドして draft Release に
上げるテンプレート。要点:
- Qt を `QT_VERSION` (farman と同一) で入れ、`qt5compat` 等必要モジュールを指定
- libarchive 等は brew / apt / vcpkg で入れる (Windows は vcpkg バイナリキャッシュ推奨)
- macOS ジョブで上記 install_name 書き換え
- 成果物を `<Plugin>-<tag>-<os>.{dylib,so,dll}` + `.sha256` で添付

### 3.4 macOS のコード署名 (第三者プラグイン)

配布版 farman.app は Hardened Runtime + 公証で署名され、**`com.apple.security.cs.disable-library-validation`**
エンタイトルメントを持つ (`macos/farman.entitlements`)。このため farman 本体と異なる Team ID で
署名された (あるいは未署名の) **第三者プラグインもロードできる**。

ただし macOS の Gatekeeper は、**ダウンロードしたファイルに付く隔離属性 (`com.apple.quarantine`)**
を別途チェックする。スムーズに導入させるには、いずれか:

- **推奨: プラグイン作者自身の Developer ID で署名 + 公証する** (ユーザー無操作で通る)。
- 署名しない場合、ユーザーが隔離属性を手動解除する必要がある
  (`xattr -dr com.apple.quarantine <plugin>`)。README に明記すること。

Linux / Windows に署名の必須要件はない (Windows は DL 時に SmartScreen 警告が出得る)。

---

## 4. 導入先 (ユーザー)

ビルドした `.dylib` / `.so` / `.dll` を farman の外部プラグインディレクトリ配下の種別サブ
フォルダに置き、farman を再起動する。**外部プラグインの読込みは既定でオフ**なので、
設定 → プラグインの **「外部プラグインの読込みを許可する」** をオンにする必要がある
(セキュリティのため。同梱プラグインは常に有効)。既定のプラグインディレクトリは OS 依存
(Settings → Plugins で変更可):

| OS | 既定のプラグインディレクトリ |
|----|------------------------------|
| macOS | `~/Library/Application Support/Farman/farman/plugins/` |
| Linux | `~/.local/share/Farman/farman/plugins/` |
| Windows | `%APPDATA%\Farman\farman\plugins\` |

その下の `viewers/` (ビュアー) または `archives/` (アーカイブ) に配置する。

---

## 5. 規約

- リポジトリ名: **`farman-plugin-<name>`**。GitHub topics に `farman` / `farman-plugin` を付ける。
- 読み取り専用など制約は README に明記する。
- ライセンスは farman 本体に準じる。

## 参考

- アーキテクチャ全体: [ARCHITECTURE.md](ARCHITECTURE.md)
- 仕様 (プラグインシステム): [SPEC.md](SPEC.md)
- 参照実装: `plugins/official/*` (同梱ビュアー)、farman-plugin-lzh (外部アーカイブ)
