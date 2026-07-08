# Changelog

All notable changes to **farman** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

For the per-commit / per-PR detail of each release, see the
[GitHub Releases](https://github.com/mashsoft-jp/farman/releases)
(`release.yml`'s `generate_release_notes` compiles the diff from the previous
tag into the release notes automatically). This file only summarizes the main
user-visible changes. A Japanese version of this file is available at
[CHANGELOG.md](CHANGELOG.md).

## [Unreleased]

In development toward the first stable release, **v1.0.0**.

### Added

- **UI / operation**
  - Two-pane / single-pane UI switching, synchronized browsing between panes
  - Keyboard-driven core operations (`c`/`m`/`k`/`d`/`r`/`n`/`a`/`f`/`p`/`u`, etc.)
  - Fully customizable key bindings + presets
- **File operations**
  - Copy / move / delete (Trash or permanent) / rename / bulk rename
    (template + sequential numbering + regex replace + preview)
  - Progress dialog / cancellation / overwrite modes (Ask / auto-overwrite / auto-rename)
- **Archives**
  - Create and extract (zip / tar / tar.gz / tar.bz2 / tar.xz)
  - **Creation options**: compression level (0–9), zip **password encryption**
    (fixed AES-256, with a confirmation field + match verification). On a
    libarchive build without encryption support, it errors out instead of
    writing plaintext
  - Extraction of encrypted zips (password entry + verification)
  - **Browsing inside archives**: view as a virtual FS (`archive.zip!/inner/`),
    extract-copy only the selected files to the opposite pane
- **Search / navigation**
  - Background recursive search, direct jump from the results
  - Bookmarks / directory history (persisted)
  - Address bar + path completion
- **Directory comparison**
  - Row-by-row color-coded diff between the left and right panes
  - Works together with synchronized browsing, keeps compare mode after copying
- **Viewers (built-in)**
  - Text (auto encoding detection / line numbers / word wrap)
  - Image (zoom / Fit to Window / transparent-background checker / GIF & WebP animation)
  - Binary (hex dump + colored address and text columns)
  - **Markdown** (CommonMark + GitHub Flavored Markdown rendered via
    `QTextDocument::setMarkdown`. Tables / task lists / strikethrough / autolinks /
    relative-path images supported. A toolbar "Source" toggle switches to the raw
    Markdown source)
  - **PDF** (inline rendering via Qt PDF / PDFium. Page navigation / page jump /
    zoom / fit-to-width / whole-page / continuous-view toggle / full-text search.
    Both inline and external)
  - **CSV / TSV** (tabular display in a `QTableView`. RFC 4180 quoted-field
    parsing, automatic delimiter detection + manual override, automatic encoding
    detection, a toggle to treat the first row as a header, in-cell full-text
    search, lazy loading for huge files (row-offset index + LRU cache, so even a
    12 MB / 200k-row file displays instantly on first view). Both inline and
    external)
  - Open in any viewer (Ctrl+Enter) / run with the OS default app (Shift+Enter)
  - **External viewer plugins**: at startup, load `IViewerPlugin`
    (`.dylib` / `.so` / `.dll`) from a user-specified directory. Configure the
    directory in Settings; check the load results in Help → Plugins...
  - **Cancelable viewer loading + result logging**: in both inline and external
    modes, loading can be canceled by pressing Esc or switching to another file
    (a modeless progress display with a Cancel button). Completion / failure /
    cancellation are logged to the Logger as a single Info / Warn / Info line,
    leaving a reproducible trace
- **Image viewer enhancements**
  - A **rotate 90° clockwise** button on the toolbar (display only, does not save).
    Angle label, reset to 0° on file switch; Fit-to-Window and external-viewer
    window fit follow the rotated size
  - **Automatic play-button enable/disable**: disabled for single-frame GIF/WebP
    and still images (PNG/JPEG/BMP), enabled only for multi-frame animations
  - **Image info ("i") dialog** (modeless, auto-updating on file switch):
    format / size / color depth / DPI / resolution
  - **Exif metadata for JPEG / PNG (eXIf) / WebP (EXIF) / TIFF**: a built-in
    parser shows Camera Make/Model, capture date/time, aperture / ISO / focal
    length / flash / white balance / color space, lens Make/Model, GPS
    latitude/longitude/altitude (including below sea level), and QImageReader's
    embedded text keys (PNG tEXt / iTXt / JPEG comment). No external library
    dependency
  - Shows the **ICC color profile name** ("sRGB IEC61966-2.1" / "Display P3" /
    "Adobe RGB (1998)", etc.)
  - **BMP** support (24-bit / 1-bit / RLE)
  - **PSD (Adobe Photoshop) support**: a built-in parser shows the composited
    preview image (the all-layers-merged RGBA saved when Photoshop's "Maximize
    Compatibility" is ON). Since Qt's bundled plugins can't handle PSD, it runs
    as a fallback when `QImage::load()` fails. The Image Info dialog also gains a
    path that pulls size / depth from the real `QImage` for formats
    `QImageReader` can't read
- **Reordering user-defined commands** (External Apps tab): ↑/↓ buttons on each
  command row. The Tools-menu order + the tab's display order follow this order
- **Export / import of all settings** (Settings → General → backup / restore):
  migrate the entire `settings.json` — including bookmarks / custom commands /
  color scheme / viewer settings / paths — to another machine in a single file
- **Recursive directory-size aggregation in the preview pane**: hovering the
  cursor over a directory shows "12.3 MB (calculating…)" to the right of the
  item count, updating as the background scan proceeds and settling to
  "12.3 MB (M files, K folders)" when done. Canceled immediately on cursor move.
  Can be interrupted safely even on slow filesystems like Google Drive (to avoid
  a qFatal crash from destroying a running thread, after cancel it self-destructs
  via the standard `QThread::finished → deleteLater` idiom)
- **Thumbnail view**
  - Switch the file list among list view / small / medium / large thumbnails
    (Finder-like `Cmd/Ctrl+1–4`, or from the View menu / toolbar popup)
  - Supports images (including svg / webp) and PDF (first page)
  - Asynchronous worker + LRU cache (per-size generation counters prevent
    mix-ups), also supports images inside archives (thumbnails generated directly
    from the virtual FS path)
- **Preview mode (Quick View)**
  - A third layout alongside Single / Dual. File list on the left, viewer on the
    right; the right pane's content switches as the cursor moves
  - Toggle: `Cmd/Ctrl+P` / View menu / the toolbar Preview button
  - Load strategy: 200 ms debounce + QtConcurrent worker + generation counter +
    cooperative cancellation (TextView / BinaryView). If the cursor moves while
    loading, it jumps straight to the next file
  - Text / binary over the limit (default 10 MB) is **read only for the first N
    bytes and shown with a truncation note**. Images can't be partially decoded,
    so a "Too large" message is shown
  - Preview size / debounce time are configurable in Settings
  - **Archive entries supported too**: with a zip / tar etc. open, moving the
    cursor temporarily extracts the entry and previews it through the normal
    viewer path
  - Directories use a Finder Quick Look style (folder icon + path + "N items")
  - The layout is persisted; splitter sizes are remembered independently for
    Dual / Preview
  - **Tab focus enters the preview**: at the end of the file-list pane's local
    Tab chain (★ → address bar → 📁 → list → mode switch), pressing `Tab` enters
    the preview's toolbar / main content. Pressing `Tab` at the end of the
    preview returns to the pane's own ★, closing the Tab loop. `Shift+Tab` goes
    in reverse; `Esc` returns to the file list immediately
- **Unified Tab focus chain across layouts**: in Dual mode the head / tail of the
  opposite FileListPane, in Preview mode the head / tail of the PreviewPane, in
  Single mode wrapping within the same pane. Wherever you start pressing Tab, you
  can cycle through all UI elements in a predictable order
- **Dimmed table-row highlight when inactive**: derived widgets like
  `QTableView` / `QListWidget` keep highlighting the selected row with
  `palette(highlight)` (blue) even after losing focus by Qt default, making it
  hard to tell where focus is. An NSTableView-like common style that **dims to
  gray when inactive** is applied across list widgets in the Settings /
  Keybindings dialogs, etc.
- **Automatic updates**
  - Checks the latest GitHub Releases tag at startup, at most once per day
  - On detecting a new version, shows a notification dialog (release notes
    rendered as Markdown) with three choices: `Update Now` / `Remind Me Later` /
    `Skip This Version`
  - `Update Now` downloads the per-OS asset (macOS DMG / Windows setup.exe /
    Linux AppImage), verifies it against the bundled SHA256, then launches the
    installer. In `silent` mode it applies automatically without confirmation
  - Controllable from Settings → General: "check automatically at startup",
    "silent update", "check now". Shows the last check date/time
  - Development builds (`0.0.0` / `0.0.0-dev`) skip the startup auto-check
    (manual check still works)
- **Appearance / customization**
  - Light / dark themes, theme presets
  - Fine-grained appearance settings for the address bar, cursor, per-category
    file colors, and row height
- **Other**
  - Log pane (daily rotation + configurable retention)
  - Internationalization (English / Japanese; Auto follows the OS setting)
  - Automatic reflection of external changes (QFileSystemWatcher + debounce)
  - Volume usage of the active pane shown in the status bar
    (`N GB free / M GB (P% used)`, refreshed by 5-second polling). When a cloud
    sync folder (Google Drive / iCloud / OneDrive / Dropbox) is detected, it
    shows `<cloud sync folder>` and suppresses the capacity (the host FS capacity
    would be misleading)
- **CI / distribution**
  - Automated 3-OS builds (`build.yml`, macOS arm64 / Linux x86_64 / Windows x86_64)
  - Tag push → automatic GitHub Releases publish (`release.yml`, safe draft-first flow)
  - Linux ships **`.deb`** (Debian / Ubuntu / Mint family) alongside the
    **AppImage**. A self-contained approach packs the AppDir into `/opt/farman/`,
    independent of the OS's stock Qt version
  - `farman --version` / `--help` available on the command line (prints to stdout
    and exits immediately without launching the GUI)
  - The macOS DMG is arranged in a typical Mac-installer layout — an
    `/Applications` symlink on the left and `farman.app` on the right, with icon
    positions and window size fixed via `create-dmg`. Users install by drag & drop
  - Windows `.exe` installer generation (Inno Setup 6, `windows/farman.iss`).
    Automatically registers Start-menu / desktop shortcuts and an uninstaller,
    supports both the Program Files and LocalAppData install paths. A portable
    zip is also offered
  - **The macOS build is distributed Developer ID signed + notarized**, so it
    opens without the "unidentified developer" warning on first launch
  - The project was moved to the **Mashsoft GitHub Organization
    (`mashsoft-jp/farman`)**. The auto-update reference was updated to the new repo

### Security

- **Zip Slip attacks** during archive extraction are rejected with defense in depth:
  - Escapes via `..` segments / absolute paths / Windows backslashes are checked
    at the entry-name stage, the destination-path assembly stage, and the
    libarchive write_disk stage
  - libarchive's `ARCHIVE_EXTRACT_SECURE_SYMLINKS` is enabled
  - Parent symlinks of the output directory (macOS `/tmp` → `/private/tmp`, etc.)
    are resolved to their real path via `QFileInfo::canonicalFilePath()` before
    being handed to libarchive
- Rejects a directory copy / move when the destination is the source itself or a
  descendant (prevents a recursive-expansion bug via canonical-path comparison)
- Does not treat a partial read of a broken archive as "normal"; notifies of the
  fatal error and stops

## [0.9.7] - 2026-07-08

Main user-visible changes since v0.9.6. For the per-commit / per-PR detail, see
the [GitHub Releases](https://github.com/mashsoft-jp/farman/releases/tag/v0.9.7).

### Added

- **Unified Properties dialog**: name, location, size, modified date, created
  date, and more consolidated into a single dialog. The modified date and
  attributes (permissions) are editable in place; owner / group show both name
  and ID (the name is editable only when common across the selected files, hidden
  on Windows). Multi-selection shows aggregated total size / item count
- **"Open With Viewer..." built from plugins**: the menu is generated from the
  registered plugins, so viewers that previously didn't appear in the list — such
  as the video / audio viewer — can now be chosen
- **Status bar in external viewer windows**: shows the same information as the
  inline view. The media viewer shows a summary of format / codec / resolution /
  duration
- **Improved zoom for image / video viewers**: the video viewer gains a zoom
  factor and a "Fit to window" toggle. In external viewers, the W key resizes the
  window to the actual size for the current zoom. Added keyboard shortcuts to the
  image viewer (`I` info / `Space` play-pause / `R` rotate / `F` fit to window /
  `T` toggle transparency / `+`·`-` zoom by 25% / `W` fit window to image)
- **Expanded viewer display settings**: a display-font setting for CSV / TSV, and
  display-font plus body / background / link color settings for Markdown
  (configurable per Light / Dark theme; when left unset they follow the theme's
  default)
- **Per-file byte progress bar in the progress dialog**: shows the transfer
  amount of the current file during a large copy / move

### Changed

- **Consolidated viewer settings into each plugin's settings**: extension / MIME
  association, font, colors, and image transparency — previously split between
  Appearance and Plugins — are now gathered into each viewer's plugin settings.
  Fonts / colors can be edited per theme via a Light / Dark toggle in the settings
- **Improved dark-mode theme following**: fixed the toolbar and the pane's
  sort/filter footer staying light
- **Unified the default macOS font to Helvetica 12pt** (shared by the UI and viewers)
- **Localized the built-in Tools menu item names to the UI language**
- **Trash is disabled on network / removable drives**, offering only permanent deletion
- Removed the "?" icon from confirmation dialogs

### Fixed

- File list and property dates, etc., wrapping onto two lines under Helvetica on macOS
- Markdown documents containing raw HTML being truncated partway
- The delete-confirmation dialog layout breaking with long file names
- Not falling back to home / root when the initial startup directory does not exist
- 0-byte files getting stuck on "Loading" in the binary viewer
- Garbled file names inside Japanese (Shift-JIS) zips without the UTF-8 flag
- A possible crash when closing a PDF opened in an external viewer
- A use-after-free crash when renaming during a background directory refresh
- Misdetecting Trash availability for mapped network drives on Windows

[Unreleased]: https://github.com/mashsoft-jp/farman/compare/v0.9.7...main
[0.9.7]: https://github.com/mashsoft-jp/farman/compare/v0.9.6...v0.9.7
