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

### Bug fixes

- Fixed an issue where the cursor on the last file could be scrolled out of
  view after returning from an inline viewer to the file list.

### Other

- Stability improvements and bug fixes
