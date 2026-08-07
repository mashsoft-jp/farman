#include "ViewerCommands.h"

#include <QCoreApplication>

namespace Farman {

namespace {

// ラベルは共有コンテキスト "ViewerCommands" で translate する（ViewerNames と同様に
// farman 本体・プラグインのどちらから呼んでも farman_ja.qm で解決される）。
// lupdate が抽出できるよう、呼び出しでは QCoreApplication::translate を直接使う。
void add(QList<ViewerCommandDef>& out, const QString& viewerId,
         const QString& commandId, const QString& label,
         const QList<QKeySequence>& defaults) {
  out.append(ViewerCommandDef{viewerId, commandId, label, defaults});
}

} // namespace

QList<ViewerCommandDef> viewerCommandDefs() {
  QList<ViewerCommandDef> d;

  // ── テキストビュアー ──
  add(d, "text", "viewer.text.find_focus", QCoreApplication::translate("ViewerCommands", "Focus search field"),
      {QKeySequence(QKeySequence::Find)});
  add(d, "text", "viewer.text.encoding_focus", QCoreApplication::translate("ViewerCommands", "Focus encoding selector"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)});
  add(d, "text", "viewer.text.toggle_line_numbers", QCoreApplication::translate("ViewerCommands", "Toggle line numbers"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L)});
  add(d, "text", "viewer.text.toggle_word_wrap", QCoreApplication::translate("ViewerCommands", "Toggle word wrap"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W)});
  add(d, "text", "viewer.text.toggle_case", QCoreApplication::translate("ViewerCommands", "Toggle case-sensitive search"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C)});

  // ── CSV/TSV ビュアー ──
  add(d, "csv", "viewer.csv.find_focus", QCoreApplication::translate("ViewerCommands", "Focus search field"),
      {QKeySequence(QKeySequence::Find)});
  add(d, "csv", "viewer.csv.encoding_focus", QCoreApplication::translate("ViewerCommands", "Focus encoding selector"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)});
  add(d, "csv", "viewer.csv.delimiter_focus", QCoreApplication::translate("ViewerCommands", "Focus delimiter selector"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D)});
  add(d, "csv", "viewer.csv.toggle_header", QCoreApplication::translate("ViewerCommands", "Toggle first-row header"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H)});
  add(d, "csv", "viewer.csv.toggle_case", QCoreApplication::translate("ViewerCommands", "Toggle case-sensitive search"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C)});

  // ── Markdown ビュアー ──
  add(d, "markdown", "viewer.markdown.find_focus", QCoreApplication::translate("ViewerCommands", "Focus search field"),
      {QKeySequence(QKeySequence::Find)});
  add(d, "markdown", "viewer.markdown.toggle_source", QCoreApplication::translate("ViewerCommands", "Toggle raw source view"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)});
  add(d, "markdown", "viewer.markdown.toggle_case", QCoreApplication::translate("ViewerCommands", "Toggle case-sensitive search"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C)});

  // ── PDF ビュアー ──
  add(d, "pdf", "viewer.pdf.find_focus", QCoreApplication::translate("ViewerCommands", "Focus search field"),
      {QKeySequence(QKeySequence::Find)});
  add(d, "pdf", "viewer.pdf.fit_width", QCoreApplication::translate("ViewerCommands", "Fit page width"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W)});
  add(d, "pdf", "viewer.pdf.fit_page", QCoreApplication::translate("ViewerCommands", "Fit whole page"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O)});
  add(d, "pdf", "viewer.pdf.toggle_continuous", QCoreApplication::translate("ViewerCommands", "Toggle continuous scrolling"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N)});
  add(d, "pdf", "viewer.pdf.zoom_in", QCoreApplication::translate("ViewerCommands", "Zoom in"),
      {QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal),
       QKeySequence(Qt::SHIFT | Qt::Key_Equal)});
  add(d, "pdf", "viewer.pdf.zoom_out", QCoreApplication::translate("ViewerCommands", "Zoom out"),
      {QKeySequence(Qt::Key_Minus), QKeySequence(Qt::SHIFT | Qt::Key_Minus)});
  add(d, "pdf", "viewer.pdf.prev_page", QCoreApplication::translate("ViewerCommands", "Previous page"),
      {QKeySequence(Qt::Key_Up)});
  add(d, "pdf", "viewer.pdf.next_page", QCoreApplication::translate("ViewerCommands", "Next page"),
      {QKeySequence(Qt::Key_Down)});

  // ── 画像ビュアー ──
  add(d, "image", "viewer.image.toggle_fit", QCoreApplication::translate("ViewerCommands", "Toggle fit to window"),
      {QKeySequence(Qt::Key_F)});
  add(d, "image", "viewer.image.rotate", QCoreApplication::translate("ViewerCommands", "Rotate 90° clockwise"),
      {QKeySequence(Qt::Key_R)});
  add(d, "image", "viewer.image.toggle_transparency", QCoreApplication::translate("ViewerCommands", "Toggle transparency background"),
      {QKeySequence(Qt::Key_T)});
  add(d, "image", "viewer.image.toggle_animation", QCoreApplication::translate("ViewerCommands", "Play / pause animation"),
      {QKeySequence(Qt::Key_Space)});
  add(d, "image", "viewer.image.info", QCoreApplication::translate("ViewerCommands", "Show image information"),
      {QKeySequence(Qt::Key_I)});
  add(d, "image", "viewer.image.zoom_in", QCoreApplication::translate("ViewerCommands", "Zoom in"),
      {QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal),
       QKeySequence(Qt::SHIFT | Qt::Key_Equal)});
  add(d, "image", "viewer.image.zoom_out", QCoreApplication::translate("ViewerCommands", "Zoom out"),
      {QKeySequence(Qt::Key_Minus), QKeySequence(Qt::SHIFT | Qt::Key_Minus)});

  // ── バイナリビュアー ──（HexView のカーソル移動は固定なので含めない）
  add(d, "binary", "viewer.binary.find_focus", QCoreApplication::translate("ViewerCommands", "Focus search field"),
      {QKeySequence(QKeySequence::Find)});
  add(d, "binary", "viewer.binary.find_next", QCoreApplication::translate("ViewerCommands", "Find next"),
      {QKeySequence(Qt::CTRL | Qt::Key_G), QKeySequence(Qt::Key_F3)});
  add(d, "binary", "viewer.binary.find_prev", QCoreApplication::translate("ViewerCommands", "Find previous"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G),
       QKeySequence(Qt::SHIFT | Qt::Key_F3)});
  add(d, "binary", "viewer.binary.address_focus", QCoreApplication::translate("ViewerCommands", "Focus address field"),
      {QKeySequence(Qt::CTRL | Qt::Key_J)});
  add(d, "binary", "viewer.binary.toggle_endian", QCoreApplication::translate("ViewerCommands", "Toggle endianness"),
      {QKeySequence(Qt::CTRL | Qt::Key_E)});
  add(d, "binary", "viewer.binary.encoding_focus", QCoreApplication::translate("ViewerCommands", "Focus encoding selector"),
      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)});
  add(d, "binary", "viewer.binary.unit_1", QCoreApplication::translate("ViewerCommands", "Unit: 1 byte"),
      {QKeySequence(Qt::CTRL | Qt::Key_1)});
  add(d, "binary", "viewer.binary.unit_2", QCoreApplication::translate("ViewerCommands", "Unit: 2 bytes"),
      {QKeySequence(Qt::CTRL | Qt::Key_2)});
  add(d, "binary", "viewer.binary.unit_4", QCoreApplication::translate("ViewerCommands", "Unit: 4 bytes"),
      {QKeySequence(Qt::CTRL | Qt::Key_3)});
  add(d, "binary", "viewer.binary.unit_8", QCoreApplication::translate("ViewerCommands", "Unit: 8 bytes"),
      {QKeySequence(Qt::CTRL | Qt::Key_4)});
  add(d, "binary", "viewer.binary.copy", QCoreApplication::translate("ViewerCommands", "Copy selection"),
      {QKeySequence(QKeySequence::Copy)});

  // ── メディアビュアー ──
  add(d, "media", "viewer.media.play_pause", QCoreApplication::translate("ViewerCommands", "Play / pause"),
      {QKeySequence(Qt::Key_Space)});
  add(d, "media", "viewer.media.stop", QCoreApplication::translate("ViewerCommands", "Stop"),
      {QKeySequence(Qt::Key_S)});
  add(d, "media", "viewer.media.seek_back", QCoreApplication::translate("ViewerCommands", "Seek backward"),
      {QKeySequence(Qt::Key_Left)});
  add(d, "media", "viewer.media.seek_forward", QCoreApplication::translate("ViewerCommands", "Seek forward"),
      {QKeySequence(Qt::Key_Right)});
  add(d, "media", "viewer.media.volume_up", QCoreApplication::translate("ViewerCommands", "Volume up"),
      {QKeySequence(Qt::Key_Up)});
  add(d, "media", "viewer.media.volume_down", QCoreApplication::translate("ViewerCommands", "Volume down"),
      {QKeySequence(Qt::Key_Down)});
  add(d, "media", "viewer.media.rate_slower", QCoreApplication::translate("ViewerCommands", "Playback speed: slower"),
      {QKeySequence(Qt::Key_BracketLeft)});
  add(d, "media", "viewer.media.rate_faster", QCoreApplication::translate("ViewerCommands", "Playback speed: faster"),
      {QKeySequence(Qt::Key_BracketRight)});
  add(d, "media", "viewer.media.zoom_in", QCoreApplication::translate("ViewerCommands", "Zoom in"),
      {QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal),
       QKeySequence(Qt::SHIFT | Qt::Key_Equal)});
  add(d, "media", "viewer.media.zoom_out", QCoreApplication::translate("ViewerCommands", "Zoom out"),
      {QKeySequence(Qt::Key_Minus), QKeySequence(Qt::SHIFT | Qt::Key_Minus)});
  add(d, "media", "viewer.media.toggle_mute", QCoreApplication::translate("ViewerCommands", "Toggle mute"),
      {QKeySequence(Qt::Key_M)});
  add(d, "media", "viewer.media.toggle_loop", QCoreApplication::translate("ViewerCommands", "Toggle loop"),
      {QKeySequence(Qt::Key_L)});
  add(d, "media", "viewer.media.info", QCoreApplication::translate("ViewerCommands", "Show media information"),
      {QKeySequence(Qt::Key_I)});
  add(d, "media", "viewer.media.toggle_fullscreen", QCoreApplication::translate("ViewerCommands", "Toggle full screen"),
      {QKeySequence(Qt::Key_F)});

  return d;
}

QList<ViewerCommandDef> viewerCommandsForViewer(const QString& viewerId) {
  QList<ViewerCommandDef> out;
  for (const ViewerCommandDef& def : viewerCommandDefs()) {
    if (def.viewerId == viewerId) {
      out.append(def);
    }
  }
  return out;
}

QString viewerCommandDefaultKeyText(const QString& commandId) {
  for (const ViewerCommandDef& def : viewerCommandDefs()) {
    if (def.commandId == commandId) {
      return def.defaultKeys.isEmpty()
        ? QString()
        : def.defaultKeys.first().toString(QKeySequence::NativeText);
    }
  }
  return QString();
}

QStringList viewerCommandViewerIds() {
  return {"text", "csv", "markdown", "pdf", "image", "binary", "media"};
}

QString viewerCommandViewerName(const QString& viewerId) {
  // ViewerNames コンテキストを共有（プラグインの pluginName と同じ訳）。
  if (viewerId == QLatin1String("text")) {
    return QCoreApplication::translate("ViewerNames", "Text Viewer");
  }
  if (viewerId == QLatin1String("csv")) {
    return QCoreApplication::translate("ViewerNames", "CSV/TSV Viewer");
  }
  if (viewerId == QLatin1String("markdown")) {
    return QCoreApplication::translate("ViewerNames", "Markdown Viewer");
  }
  if (viewerId == QLatin1String("pdf")) {
    return QCoreApplication::translate("ViewerNames", "PDF Viewer");
  }
  if (viewerId == QLatin1String("image")) {
    return QCoreApplication::translate("ViewerNames", "Image Viewer");
  }
  if (viewerId == QLatin1String("binary")) {
    return QCoreApplication::translate("ViewerNames", "Binary Viewer");
  }
  if (viewerId == QLatin1String("media")) {
    return QCoreApplication::translate("ViewerNames", "Media Viewer");
  }
  return viewerId;
}

} // namespace Farman
