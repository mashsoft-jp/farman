# What's New in farman 1.1.0

### Install plugins inside the app

- The new **Settings → Plugins** page lets you install, update and uninstall
  external plugins in one place. You no longer have to copy files into the
  plugin directory by hand.
- Drag and drop a plugin file onto the page, or pick one with "Install from
  File...", to install it.
- Official plugins can be installed or updated right from the list with
  "Install" / "Update". "Check for Updates" looks up the latest versions, and
  plugins with an update are marked with ❗ in bold.
- Installs, updates and uninstalls take effect the next time farman starts. Use
  the "Restart farman" button on the page to restart right away.
- You can also open this page from Help → "Plugins..." (Ctrl+Shift+P) or the
  toolbar. The Settings pages are now numbered 4. Plugins / 5. Viewer /
  6. Archive, and so on.

### Tips on startup

- farman now shows a usage tip once a day on startup, introducing shortcuts and
  easy-to-miss features one at a time.
- Browse the tips with "Previous" / "Next". Keys in the text follow your current
  keybindings.
- Open them any time from Help → "Tips...". To stop showing them on
  startup, uncheck the box at the bottom left of the dialog or in
  Settings → General.

### Sort and filter

- Fixed temporary settings (applied without "Override defaults for this
  directory") appearing as the defaults when the dialog was reopened.
- When the dialog opens with settings that differ from the defaults, it now
  says whether they are saved or temporary, and which items differ.
- The sort / filter line under each pane now starts with **[Default] /
  [Temporary] / [Custom]** (saved for this directory), so you can tell which
  settings are in effect. It also shows how dot files are handled (Dot-first /
  Dot-ignored).
- With "Sort dot files first" turned off, dot files are now sorted by their name
  without the leading "." (.bashrc sorts among the b's).

### Settings

- The viewer display mode (in the main window / separate windows) has moved from
  the "Behavior" page to the "Viewer" page.
- Check boxes on the "General" and "Behavior" pages are laid out in two columns
  so they are easier to scan.

### Bug fixes

- Fixed the Size column in search results using the OS language for its units
  (such as "36 バイト") even when the UI was set to English.
