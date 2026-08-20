# What's New in farman 0.9.10

(This version is still in development. The contents may change.)

### New "Archive" tab in Settings

- You can now see every supported archive format in one list and turn each one on
  or off. Built-in formats and formats provided by archive plugins appear in the
  same list.
- "Details..." on each row lets you change the file patterns and the defaults used
  when creating an archive (compression level, encryption method, file name
  encoding). Formats that farman cannot create show no creation settings.
- The temporary extraction directory, the number of password attempts, and the
  maximum nesting depth for archives inside archives are all configurable here.

### More archive formats

- Added 7-Zip, RAR, ISO9660, Microsoft Cabinet, xar / pkg, cpio, ar / Debian
  package, and TAR combined with Zstandard / LZ4 / LZMA / lzip / compress.
- A **single file** compressed with gzip, bzip2, xz, LZMA, Zstandard, LZ4 or
  compress can now be opened as an archive containing one entry.
- These are off by default. Turn on the formats you need in Settings → Archive.
  (Formats your build of libarchive does not support cannot be opened.)

### Settings reorganized

- The "Plugins" tab is gone. Viewer plugins are now managed in the new "Viewer"
  tab, and archive plugins in the "Archive" tab.
- "Allow loading external plugins" and the plugins directory moved to the
  "General" tab.
- In both lists you can toggle a row directly with its checkbox, or switch
  everything at once with "Enable all".

### File patterns

- Viewers and archives used different names and formats for the same thing.
  They are now both "File patterns", and in addition to plain extensions you can
  write:
  - `mp4` … an extension (as before)
  - `*.tar.gz` … a compound extension
  - `Makefile` … a file with no extension
- Extensions you added or removed in the settings did not always affect which
  viewer was chosen. They now take effect as written.

### Archives

- **Archives inside archives** now open with Enter, to any depth (the depth
  limit is configurable). Backspace or ".." walks back out one level at a time.
- The per-format "File name encoding" setting is now actually used when
  reading entry names — useful for older zips without the UTF-8 flag, where
  auto-detection guesses wrong.
- The "Temporary directory" and "Password attempts" settings are now applied
  as well (a change to the temporary directory takes effect at next start).

### File list operations

- "Copy Path" now copies the paths of **all selected files** at once
  (previously it copied only the item under the cursor).
- Added an action that copies just the **file names** instead of the paths
  (default shortcut Shift+Ctrl+C, Shift+Cmd+C on macOS).
- The separator used when copying several entries is configurable: comma,
  comma + space, or a newline (LF / CRLF / CR). Settings → Behavior →
  File Operations.
- After a copy, move, delete or rename, the status bar briefly shows what was
  done in place of the current path. The duration is configurable, and 0
  turns the notice off.
- Changed what gets selected when renaming: for files, everything up to the
  **last** dot ("photo.tar.gz" selects "photo.tar"); for directories, the whole
  name (dots in a directory name are not an extension).

### Bug fixes

- Fixed the English What's New dialog being cut off partway through, and made
  Markdown rendering in general immune to the same kind of truncation.
- With every viewer plugin disabled, only the binary viewer used to open. Now
  nothing opens at all.
- Fixed "Open With Viewer..." (Shift+Ctrl+Enter, Cmd+Enter on macOS) on a file
  inside an archive: it showed empty content or failed to open.
