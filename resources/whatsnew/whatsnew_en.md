# What's New in farman 0.9.9

### 3D model viewer (FBX) support

With the external plugin **farman-plugin-3d**, farman can now display
`.fbx` 3D models inside the app.

- Supports textures (external and embedded), skeletal animation, multiple
  materials, a floor grid, wireframe display, and bone (skeleton) display.
- Rotate, pan, and zoom with the mouse or keyboard.
- The plugin is not bundled with farman. Download the build for your OS,
  place it in the `viewers/` folder of farman's external plugin directory,
  turn on "Allow loading external plugins" under Settings > Plugins, then
  restart farman.
- FBX is supported first; other formats such as obj / glTF are planned.

### Directory sizes in the Size column (computed in the background)

- The file list can now compute the recursive total size of directories in
  the background and show it in the Size column.
- Directories now always show "&lt;DIR&gt;" in the Type column (regardless of
  whether directory-size calculation is enabled).
- Off by default. Enable it under Settings > Behavior > Directory Size.
- Choose "Recompute every time" or "Cache for a period", and set how long the
  cache stays valid (a change to a directory's timestamp always triggers a
  recompute).
- Force a recompute with the toolbar button or `Ctrl+Shift+R`.
- Not computed while the Size column is hidden.

### Binary viewer improvements

- Rewritten with custom rendering so **gigabyte-class files** open quickly and
  display correctly all the way to the end: only the visible rows are read
  instead of loading the whole file.
- Added jump-to-hex-address. The maximum valid address is shown next to the
  input, and out-of-range addresses are rejected as you type.

### Bug fixes

- Fixed an issue where the cursor on the last file could be scrolled out of
  view after returning from an inline viewer to the file list.
- Fixed an issue where the last row could fail to render after scrolling near
  the end of a large file.

### Other

- Stability improvements and bug fixes
