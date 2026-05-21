cmake_minimum_required(VERSION 3.21)
project(farman VERSION 0.9.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt6Gui は optional dependency として WrapVulkanHeaders を探すが、farman は
# Vulkan を使わない (Qt の Vulkan サポート自体は wantsRhi=false の用途では不要)。
# 何もしないと configure ログに毎回
#   -- Could NOT find WrapVulkanHeaders (missing: Vulkan_INCLUDE_DIR)
# が出てノイジーなので、CMAKE_DISABLE_FIND_PACKAGE で find 自体を抑止する
# (Qt 側はその結果を見て該当機能を無効化するだけで実害はない)。
set(CMAKE_DISABLE_FIND_PACKAGE_WrapVulkanHeaders TRUE)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Core5Compat Concurrent Network Svg LinguistTools)

# Homebrew の libarchive は keg-only なので明示的にパスを通す。
# Apple Silicon は /opt/homebrew、Intel Mac は /usr/local が Homebrew prefix。
# Universal Binary をビルドするとき、CI 上の Intel ランナーでは /usr/local の
# 方にライブラリが入るので両方検索する。
if(APPLE)
  if(EXISTS /opt/homebrew/opt/libarchive)
    list(APPEND CMAKE_PREFIX_PATH /opt/homebrew/opt/libarchive)
  elseif(EXISTS /usr/local/opt/libarchive)
    list(APPEND CMAKE_PREFIX_PATH /usr/local/opt/libarchive)
  endif()
endif()
find_package(LibArchive REQUIRED)

# uchardet (Mozilla Universal Charset Detector) — テキストビュアーの
# エンコード自動判定に使う。Homebrew では keg ではないので pkg-config 経由か
# 直接パスを通して見つける。
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(UCHARDET QUIET IMPORTED_TARGET uchardet)
endif()
if(NOT UCHARDET_FOUND)
  # uchardet.h は通常 .../include/uchardet/uchardet.h に置かれる。
  # PATH_SUFFIXES uchardet で「uchardet/ サブディレクトリ内の uchardet.h」を
  # 探し、結果のパス (`.../include/uchardet`) をそのまま include に通す。
  # /opt/homebrew (Apple Silicon) と /usr/local (Intel) の両方を検索。
  find_path(UCHARDET_INCLUDE_DIR uchardet.h
    PATHS /opt/homebrew/include /usr/local/include /usr/include
    PATH_SUFFIXES uchardet)
  find_library(UCHARDET_LIBRARY uchardet
    PATHS /opt/homebrew/lib /usr/local/lib /usr/lib)
  if(NOT UCHARDET_INCLUDE_DIR OR NOT UCHARDET_LIBRARY)
    message(FATAL_ERROR "uchardet not found. Install via 'brew install uchardet' (macOS) or 'apt install libuchardet-dev' (Linux).")
  endif()
endif()

# macOS .app バンドルに同梱するアイコン。
# - tools/generate_icon_assets.sh で images/icon.svg から手動で再生成する
#   (CMake からは自動実行しない)。
# - MACOSX_PACKAGE_LOCATION で Contents/Resources/ 配下に配置され、
#   下の MACOSX_BUNDLE_ICON_FILE が Info.plist の CFBundleIconFile を埋める。
set(FARMAN_MACOS_ICON "${CMAKE_SOURCE_DIR}/images/icon.icns")
if(APPLE AND EXISTS "${FARMAN_MACOS_ICON}")
  set_source_files_properties("${FARMAN_MACOS_ICON}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources")
endif()

qt_add_executable(farman
  src/main.cpp
  resources/farman.qrc
  src/ui/MainWindow.cpp
  src/ui/FileListPane.cpp
  src/ui/FileListView.cpp
  src/ui/FileListThumbnailView.cpp
  src/ui/FileListDelegate.cpp
  src/ui/FileListThumbnailDelegate.cpp
  src/ui/FileManagerPanel.cpp
  src/ui/ViewerPanel.cpp
  src/ui/LogPane.cpp
  src/ui/ProgressDialog.cpp
  src/ui/SettingsDialog.cpp
  src/ui/ShortcutListDialog.cpp
  src/ui/UpdateAvailableDialog.cpp
  src/ui/KeybindingTab.cpp
  src/ui/AppearanceTab.cpp
  src/ui/BehaviorTab.cpp
  src/ui/GeneralTab.cpp
  src/ui/ExternalAppsTab.cpp
  src/ui/SortFilterDialog.cpp
  src/ui/TransferConfirmDialog.cpp
  src/ui/OverwriteDialog.cpp
  src/ui/DeleteConfirmDialog.cpp
  src/ui/AttributesDialog.cpp
  src/ui/BulkRenameDialog.cpp
  src/ui/BookmarkListDialog.cpp
  src/ui/BookmarkEditDialog.cpp
  src/ui/HistoryDialog.cpp
  src/ui/SearchDialog.cpp
  src/ui/PropertiesDialog.cpp
  src/ui/CreateArchiveDialog.cpp
  src/ui/ExtractArchiveDialog.cpp
  src/ui/DirectoryCompareDialog.cpp
  src/ui/ClickableLabel.cpp
  src/core/FileItem.cpp
  src/core/BookmarkManager.cpp
  src/core/DirectoryHistory.cpp
  src/core/Logger.cpp
  src/core/UserCommand.cpp
  src/core/PlaceholderExpander.cpp
  src/core/UserCommandRunner.cpp
  src/core/UserCommandManager.cpp
  src/core/AppPresets.cpp
  src/core/ArchiveContext.cpp
  src/core/ThumbnailCache.cpp
  src/core/ThumbnailWorker.cpp
  src/core/UpdateChecker.cpp
  src/core/UpdateDownloader.cpp
  src/utils/Dialogs.cpp
  src/utils/FarmanMessageBox.cpp
  src/utils/EnterClickFilter.cpp
  src/utils/ArchivePath.cpp
  src/utils/MediaMatchers.cpp
  src/core/workers/WorkerBase.cpp
  src/core/workers/CopyWorker.cpp
  src/core/workers/MoveWorker.cpp
  src/core/workers/RemoveWorker.cpp
  src/core/workers/SearchWorker.cpp
  src/core/workers/PropertiesWorker.cpp
  src/core/workers/ArchiveCreateWorker.cpp
  src/core/workers/ArchiveExtractWorker.cpp
  src/core/workers/ArchiveExtractEntriesWorker.cpp
  src/core/workers/DirectoryCompareWorker.cpp
  src/model/FileListModel.cpp
  src/viewer/ViewerDispatcher.cpp
  src/viewer/TextView.cpp
  src/viewer/TextViewerWindow.cpp
  src/viewer/ImageView.cpp
  src/viewer/ImageViewerWindow.cpp
  src/viewer/BinaryView.cpp
  src/viewer/BinaryViewerWindow.cpp
  src/viewer/TextViewerPlugin.cpp
  src/viewer/ImageViewerPlugin.cpp
  src/viewer/BinaryViewerPlugin.cpp
  src/keybinding/ICommand.cpp
  src/keybinding/CommandRegistry.cpp
  src/keybinding/CommandLayout.cpp
  src/keybinding/KeyBindingManager.cpp
  src/keybinding/commands/BaseCommand.cpp
  src/keybinding/commands/NavigationCommands.cpp
  src/keybinding/commands/SelectionCommands.cpp
  src/keybinding/commands/PaneCommands.cpp
  src/keybinding/commands/FileOperationCommands.cpp
  src/keybinding/commands/ApplicationCommands.cpp
  src/settings/Settings.cpp
  src/settings/ColorScheme.cpp
  src/settings/PresetIO.cpp
)

# macOS の場合のみ .icns をバンドルに同梱
if(APPLE AND EXISTS "${FARMAN_MACOS_ICON}")
  target_sources(farman PRIVATE "${FARMAN_MACOS_ICON}")
endif()

target_include_directories(farman PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)

# FARMAN_VERSION: 実行ファイルに埋め込むバージョン文字列。
#   - 通常は project() の VERSION (x.y.z) をそのまま使う
#   - CI でテストタグ (`v0.9.1-test` 等) から作る test ビルドでは、
#     `-DFARMAN_VERSION_SUFFIX=test` を渡すと "0.9.1-test" になる。
#     semver の prerelease 規約 ("0.9.1-test < 0.9.1") に乗るので、
#     test ビルドから安定版へのアップデート検知が正しく働く。
set(FARMAN_VERSION_SUFFIX "" CACHE STRING
  "Optional prerelease suffix appended to FARMAN_VERSION (e.g. \"test\" → 0.9.1-test)")
if(FARMAN_VERSION_SUFFIX)
  set(FARMAN_VERSION_STRING "${PROJECT_VERSION}-${FARMAN_VERSION_SUFFIX}")
else()
  set(FARMAN_VERSION_STRING "${PROJECT_VERSION}")
endif()
message(STATUS "FARMAN_VERSION = ${FARMAN_VERSION_STRING}")

target_compile_definitions(farman PRIVATE
  FARMAN_VERSION=${FARMAN_VERSION_STRING}
)

# GUI アプリとしてビルド:
#   macOS  → .app バンドル (build/farman.app)
#   Windows → ウィンドウ無しの GUI exe (黒い cmd ウィンドウが付かない)
#   Linux   → 何もしない (通常の ELF 実行ファイル)
# 開発時にデバッグログを Terminal で見たい場合は、バンドル内の素の実行
# ファイルを直接叩けばよい:
#   ./build/farman.app/Contents/MacOS/farman
set_target_properties(farman PROPERTIES
  MACOSX_BUNDLE                          TRUE
  WIN32_EXECUTABLE                       TRUE
  MACOSX_BUNDLE_BUNDLE_NAME              "farman"
  MACOSX_BUNDLE_GUI_IDENTIFIER           "co.jp.mashsoft.farman"
  MACOSX_BUNDLE_BUNDLE_VERSION           "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING     "${PROJECT_VERSION}"
  MACOSX_BUNDLE_COPYRIGHT                "Copyright (c) Mashsoft Inc."
  MACOSX_BUNDLE_ICON_FILE                "icon.icns"
)

# 翻訳: .ts → .qm を生成し、Qt リソースとして実行ファイルに埋め込む。
# `cmake --build build --target update_translations` で .ts を更新できる
# (Qt 6.10+ が `update_translations` という集約ターゲットを自動生成するため
# LUPDATE_TARGET は明示しない。明示すると Qt の自動生成と衝突する)。
qt_add_translations(farman
  TS_FILES translations/farman_ja.ts
  RESOURCE_PREFIX /translations
)

target_link_libraries(farman PRIVATE
  Qt6::Core
  Qt6::Widgets
  Qt6::Core5Compat
  Qt6::Concurrent
  Qt6::Network
  Qt6::Svg
  LibArchive::LibArchive
)

# uchardet をリンク (pkg-config が見つけたら IMPORTED ターゲット、
# そうでなければ直接 find_library で得たパスを使う)
if(TARGET PkgConfig::UCHARDET)
  target_link_libraries(farman PRIVATE PkgConfig::UCHARDET)
else()
  target_include_directories(farman PRIVATE ${UCHARDET_INCLUDE_DIR})
  target_link_libraries(farman PRIVATE ${UCHARDET_LIBRARY})
endif()

# Test executable for Worker functionality
qt_add_executable(test_worker
  src/test_worker.cpp
  src/core/workers/WorkerBase.cpp
  src/core/workers/CopyWorker.cpp
  src/core/workers/MoveWorker.cpp
  src/ui/ProgressDialog.cpp
  src/utils/Dialogs.cpp
  src/utils/FarmanMessageBox.cpp
  # ProgressDialog が Settings に依存するので test_worker でも Settings を含める。
  # Settings.cpp は core/UserCommand.h を include しており、defaults / JSON の
  # 各シンボルが未定義になるので UserCommand.cpp も同梱する (Manager / Runner は
  # Settings 自体は使わないので不要)。
  src/settings/Settings.cpp
  src/settings/ColorScheme.cpp
  src/core/BookmarkManager.cpp
  src/core/UserCommand.cpp
)

target_include_directories(test_worker PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_worker PRIVATE
  Qt6::Core
  Qt6::Widgets
)
