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
directory, the installed plugin list (load status) with
enable/disable toggles, and Viewer Associations. Help → Plugins...
(toolbar button / shortcut available) opens the page directly.

### What's New Dialog

After an update, farman shows this dialog once on first launch.
You can reopen it any time via Help → What's New...

### Other Changes

- Removed the SHA-256 hash option from directory compare in favor of the fast default comparison
- Stability improvements and bug fixes
