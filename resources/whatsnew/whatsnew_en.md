# What's New in farman 0.9.8

### Archive plugin support

farman can now load external plugins beyond viewers. The archive formats
farman can open can be extended later through external plugins, including
plugins written by third parties.

- Archives in an added format can be browsed and extracted just like
  zip / tar: you can step inside them and take files out.
- These plugins are not bundled with farman; users who need them download
  and install them separately.

### External plugin loading is now opt-in

External plugins are native code and run with the same privileges as
farman. For safety, external plugin loading is now **off** by default.

- Turn on **"Allow loading external plugins"** under Settings > Plugins to
  load plugins from the external plugin directory on the next launch
  (bundled plugins are always enabled).
- Only enable plugins from sources you trust.

### LZH (LHA) archive plugin released

As the first one, a separate external plugin for opening `.lzh` / `.lha`
archives is now available.

- Browse inside the archive and extract files (Japanese Shift-JIS file
  names are handled; read-only).
- Download the plugin for your OS and place it in the `archives/` folder
  of farman's external plugin directory, turn on "Allow loading external
  plugins" under Settings > Plugins, then restart farman. See the
  README / download page for details.

### Monospace default font for the binary and text viewers

The default font of the binary viewer (hex dump) and the text viewer is
now a true monospace font (macOS = Menlo, Windows = MS Gothic, Linux =
DejaVu Sans Mono), so columns line up correctly.

- This is the default for new installations. If you already use farman,
  pick a font under each viewer's "Font" setting, or use "Restore
  defaults" to get the new default font.

### Adjusted default fonts on Windows

On Windows, the general UI font is now **MS UI Gothic**, and the overall
font size is unified to **9pt** (the address bar, file list, and viewers
keep their existing fonts, only resized to 9pt). This is the default for
new installations.

### Other

- Stability improvements and bug fixes
