# What's New in farman 0.9.6

### Video / Audio Viewer

Play videos (mp4 / mov / webm / mkv, etc.) and audio files
(mp3 / flac / wav, etc.) directly inside farman with the `V` key.
Seeking, volume, looping, and fullscreen are supported.

### Viewers Are Now Plugins

The bundled viewers (Text / Image / PDF / CSV / Markdown / Binary / Media)
have been migrated to dynamically loaded official plugins.

- User-built external viewer plugins (.dylib / .so / .dll) can be loaded
- Per-extension Viewer Associations now also apply in Inline mode

### New "Plugins" Settings Page

Everything plugin-related now lives in one place: the plugins
directory and the installed plugin list, grouped into per-type tabs,
with load status and enable/disable toggles. Each row's "Details..."
shows the full plugin information. Help → Plugins...
(toolbar button / shortcut available) opens the page directly.

### Change Viewer Defaults from "Details"

Each viewer's defaults can now be changed from a settings page inside
its plugin "Details". Besides the associated extensions, you can set
the default text encoding, image Fit / zoom, PDF view mode, CSV
delimiter, media volume / autoplay, and more. When several viewers
list the same extension, the one with the higher priority is used.

### HEIC / HEIF Image Support

HEIC / HEIF images (e.g. photos taken on an iPhone) can now be shown in
the image viewer. macOS uses the system decoder; Windows and Linux use a
bundled libheif.

### What's New Dialog

After an update, farman shows this dialog once on first launch.
You can reopen it any time via Help → What's New...

### Other Changes

- Broadened the media viewer's supported formats (WMV / MPEG-1·2 / M2TS / WMA, etc.)
- Removed the SHA-256 hash option from directory compare in favor of the fast default comparison
- Stability improvements and bug fixes
