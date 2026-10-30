# What's New in farman 1.2.0

### View Adobe Illustrator (.ai) files

- The image viewer can now show Adobe Illustrator (.ai) files. It draws the first
  artboard of a PDF-compatible .ai as a composite preview. Transparent areas are
  shown with the checkerboard, like other images.
- Legacy EPS-based .ai files, and .ai files saved with "Create PDF Compatible
  File" turned off, cannot be shown.
- `ai` is added to the image viewer's target files in Settings automatically.
  If .ai files used to open in the PDF viewer, they now open in the image viewer.

### Image viewer: information in a side panel

- The image information (metadata) opened with the "i" button (or the I key) is now
  shown in a panel on the right of the image instead of a separate window. The
  panel updates when you switch files, and you can drag the divider to resize it.

### PDF viewer: outline (bookmarks) sidebar

- For PDFs that have an outline, the outline button on the toolbar (Cmd+Shift+B,
  Ctrl+Shift+B on Windows / Linux) opens the list on the left. Choose an item to
  jump to its page.
- Opening the sidebar moves the focus to it: pick with the ↑↓ keys and press
  Enter to jump. The button is disabled for PDFs without an outline.

### Markdown viewer: table of contents sidebar

- The contents button on the toolbar (Cmd+Shift+B, Ctrl+Shift+B on Windows / Linux)
  opens the list of headings on the left. Choose an item to scroll the text to that
  heading.
- The heading hierarchy is shown as is. The button is disabled for documents
  without headings, and while the source view is on.

### Zoom in every viewer

- The text, Markdown, CSV and binary viewers can now zoom. Use the zoom box on the
  toolbar (pick 50% / 100% / 200% and so on, or type a value) and the magnifier
  "−" / "+" buttons. Ctrl (Cmd on Mac) + mouse wheel zooms as well.
- Every viewer, including PDF, image and media, now has a button to reset zoom to 100%.
- The zoom keys are the same in all viewers: `+` to zoom in, `-` to zoom out and `0` to
  reset to 100% (the `+` key on a Japanese keyboard works too). No modifier key is needed,
  and typing in a search box takes priority over the shortcuts. You can change the keys in
  the key bindings settings.

### Icons on the viewer toolbars

- In the Markdown, PDF, CSV and Binary viewers, the toolbar buttons that were text
  (source view, contents, outline, zoom, previous / next page, previous / next
  match and so on) are now icons. The "i" button of the image and media viewers is
  an icon too. The assigned keys are still shown when you hover over a button.

### Bug fixes

- Fixed farman not restarting on Windows when you pressed "Restart farman" after
  updating a plugin.
- Fixed the Markdown viewer showing the whole document in large bold type, as if
  it were a heading, when you switched a document that starts with a heading to the
  source view.
- The previous / next page buttons of the PDF viewer showed "PageUp / PageDown",
  which was not the actual key. They now show the assigned keys.
