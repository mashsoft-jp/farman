## Open the keybinding list any time

Press {key:help.shortcuts} to open the keybinding list, showing the keys you can use right now; press it again to
close. You can change the keys under Settings → Keybindings.

## Shortcuts for copy, move and delete

{key:file.copy} copies, {key:file.move} moves, {key:file.delete} deletes, {key:file.mkdir} creates
a directory, and {key:file.rename} renames.
The other pane is used as the destination automatically.

## Select several files at once

{key:select.toggle_and_down} selects the file and moves down. {key:select.invert} inverts the
selection, {key:select.all} selects everything. The total size of the selection is shown in the status bar.

## Jump to a file by its first letter

Hold **`Shift`** and press a letter to move the cursor to the next file or directory
starting with that letter. Press the same key again to go to the next match (it wraps
around to the top). The list itself stays as it is — this only moves the cursor.

## Narrow down the list

Press {key:view.quick_filter} to open the quick filter; only files containing what you type stay
visible. **`Esc`** clears it. Sorting and detailed filters are under {key:pane.sort_filter}.

## Sort each directory its own way

{key:pane.sort_filter} changes sorting and filters for the current directory.
Check "Override defaults for this directory" to save them, so the directory opens the same way next time.
Turn off "Sort dot files first" and dot files such as ".bashrc" are placed by their name without the leading ".".

## Bring both panes to the same place

{key:pane.sync_other_to_active} moves the other pane to this pane's directory,
{key:pane.sync_active_to_other} does the reverse. Press {key:pane.sync_browse_toggle} to turn on Sync Browse, and navigation is mirrored in both panes.

## Open in the viewer, or choose which viewer

{key:view.file} (or {key:navigate.enter}) opens a file in the built-in viewer.
{key:view.choose} lets you pick
which viewer to use — text, image, binary and so on.

## Preview mode: browse while looking inside

{key:pane.toggle_preview} turns the other pane into a preview that shows the file under the cursor.
Images, PDFs and videos work too.

## Bookmarks and history

{key:bookmark.toggle} adds or removes the current directory as a bookmark,
{key:bookmark.list} opens the list. {key:history.show} shows the directories you visited recently so you can jump back.

## Archives open just like directories

zip, tar, 7z and other archives can be entered with {key:navigate.enter} and browsed as if they
were directories. {key:file.pack} packs the selected files, {key:file.unpack} extracts an archive.

## Compare two directories

{key:view.compare_directories} compares the directories in the left and right panes and colors files
that exist on one side only or differ in size or date. Handy for checking a sync.

## Open a terminal or editor right here

{key:user.cmd.terminal} opens the current directory in a terminal, {key:user.cmd.editor} opens
the selected file in a
text editor. Choose which applications to use under Settings → External Apps.

## Switch to thumbnails

{key:view.list} shows the list, {key:view.thumbnail_small} / {key:view.thumbnail_medium} /
{key:view.thumbnail_large} show small, medium and large thumbnails. Useful for directories full of images.

## Use one wide pane

{key:pane.toggle_single} switches to a single pane so the file list fills the window. Press it
again to go back to two panes.

## Copy paths or file names

{key:file.copy_path} copies the full paths of the selected files, {key:file.copy_name} copies just
the file names. The separator used for multiple files can be chosen under
Settings → Behavior.

## Rename many files at once

Select several files and press {key:file.bulk_rename} to rename them together with sequence
numbers or search-and-replace. The new names are previewed before anything changes.

## Add formats with plugins

Under Settings → Plugins ({key:help.plugins}) you can install and update the official
plugins (3D model viewer, LZH archives, ...) from inside farman. You can also drop a
plugin file there to install it.

## Look at the log

{key:view.toggle_log} opens the log pane, where you can see the results of copies and deletions,
plugin load status and more. It can also be written to a file from Settings.
