# What's New in farman 0.9.6

### Video / Audio Viewer

Play videos (mp4 / mov / webm / mkv, etc.) and audio files
(mp3 / flac / wav, etc.) directly inside farman with the `V` key.
Seeking, volume, looping, and fullscreen are supported.

### Viewers Are Now Plugins

The bundled viewers (Text / Image / PDF / CSV / Markdown / Binary / Media)
have been migrated to dynamically loaded official plugins.

- Enable or disable each plugin in Settings → Viewers
- Per-extension Viewer Associations now also apply in Inline mode
- Check plugin load status via Help → Plugins... (toolbar button / shortcut available)
- User-built external viewer plugins (.dylib / .so / .dll) can be loaded

### Other Changes

- Removed the SHA-256 hash option from directory compare in favor of the fast default comparison
- Stability improvements and bug fixes
