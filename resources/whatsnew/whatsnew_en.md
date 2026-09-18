# What's New in farman 1.0.1

### Overwrite confirmation for copy and move

- The overwrite dialog has a new **"Apply this choice to all remaining conflicts"**
  check box (Alt+A, ⌥A on macOS). Tick it and press OK, and the rest of that copy
  or move uses the same choice (overwrite / rename / skip) without asking again.
  If you chose "Rename to", the remaining conflicts are named with the auto-rename
  template. The choice lasts only for that operation and is not saved in Settings.
- "Skip this file" used to cancel the whole operation. It now skips just that
  file and carries on. Skipped files still count toward the progress, so the
  operation finishes at 100%.
- The Source / Destination rows in the dialog are now aligned with the heading
  for easier reading.

### Disk space in the status bar

- The status bar was showing disk space as GiB (1024-based) values. It now
  shows GB (1000-based) values. Note that the free space is what the OS reports
  as actually free; it does not include the "purgeable" space that macOS counts
  in "Available".

### Bug fixes

- Fixed the disk usage in the status bar showing a doubled percent sign, such as
  "23%% used".
- Fixed the "(⌥O)" hint on the Search button disappearing while a search was
  running and after it finished.
- Fixed a typo in the Japanese label of "Sync Active Pane to Other" in the View
  menu.
