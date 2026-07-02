# What's New in farman 0.9.7

### Redesigned Properties Dialog

File / folder properties are now consolidated into a single dialog.

- Review name, location, size, modified date, and created date together
- Multi-selection shows aggregated total size and item count
- Edit the modified date and attributes (permissions) in place
- Owner / group are shown with both name and ID; the name is editable
  only when it is common across the selected files (hidden on Windows)

### "Open With Viewer..." Built from Plugins

The "Open With Viewer..." menu is now generated automatically from the
registered plugins. Viewers that previously didn't appear in the list,
such as the video / audio viewer, can now be chosen from all installed
viewers.

### Status Bar in External Viewers

Viewers opened in a separate window now show the same status information
as the inline view. The media viewer shows a summary of the format /
codec / resolution / duration.

### Improved Zoom for Image / Video Viewers

- The video viewer now also has a zoom factor and a "Fit to window" toggle
- "Fit content to window" is enabled by default; when turned off, content is
  shown / played at the default zoom (configurable in the plugin settings)
- In external viewers, "Fit window to image / video" (W key) resizes the
  window to the actual size for the current zoom factor
- Added keyboard shortcuts to the image viewer
  (`I`=info, `Space`=play/pause animation, `R`=rotate, `F`=fit to window,
  `T`=toggle transparency background, `+`/`-`=zoom in / out by 25%,
  `W`=fit window to image)

### Other Changes

- Fixed Markdown documents containing raw HTML being truncated partway
- On network / removable drives, where the Trash is unavailable, "Move to
  Trash" is now disabled so only permanent deletion is offered
- Fixed the delete-confirmation dialog layout breaking with long file names
- Fall back to home / root when the initial startup directory does not exist
- Fixed 0-byte files getting stuck on "Loading" in the binary viewer
- Fixed garbled file names for Japanese (Shift-JIS) zips without the UTF-8
  flag, both when browsing inside the archive and when extracting
- Fixed a possible crash when closing a PDF opened in an external viewer
- Stability improvements and bug fixes
