#include "ViewerHints.h"

#include "ViewerShortcutMap.h"
#include "keybinding/ViewerCommands.h"

#include <QLineEdit>
#include <QVariant>
#include <QWidget>

namespace Farman {
namespace ViewerHints {

namespace {

// ツールチップ用とプレースホルダ用でプロパティを分ける (検索欄のように 1 つの
// ウィジェットが両方を持つケースがあるため)。
const char* kTipCmd  = "farman_hint_tip_cmd";
const char* kTipTmpl = "farman_hint_tip_tmpl";
const char* kPhCmd   = "farman_hint_ph_cmd";
const char* kPhTmpl  = "farman_hint_ph_tmpl";

QString expand(const QString& tmpl, const QString& key) {
  return tmpl.contains(QLatin1String("%1")) ? tmpl.arg(key) : tmpl;
}

void applyTo(QWidget* w, const QString& key, bool placeholder) {
  if (placeholder) {
    if (auto* le = qobject_cast<QLineEdit*>(w)) {
      le->setPlaceholderText(expand(w->property(kPhTmpl).toString(), key));
    }
  } else {
    w->setToolTip(expand(w->property(kTipTmpl).toString(), key));
  }
}

}  // namespace

void tag(QWidget* w, const QString& commandId, const QString& tmpl,
         bool placeholder) {
  if (!w) {
    return;
  }
  if (placeholder) {
    w->setProperty(kPhCmd, commandId);
    w->setProperty(kPhTmpl, tmpl);
  } else {
    w->setProperty(kTipCmd, commandId);
    w->setProperty(kTipTmpl, tmpl);
  }
  // 初期表示は既定キー (push 前でも正しく見えるように)。
  applyTo(w, viewerCommandDefaultKeyText(commandId), placeholder);
}

void refresh(QWidget* root, const ViewerShortcutMap& map) {
  if (!root) {
    return;
  }
  auto keyFor = [&map](const QString& cmd) -> QString {
    QString key = map.primaryKeyText(cmd);
    if (key.isEmpty()) {
      key = viewerCommandDefaultKeyText(cmd);  // 未割り当て時は既定を表示
    }
    return key;
  };
  auto handle = [&](QWidget* w) {
    const QVariant tipCmd = w->property(kTipCmd);
    if (tipCmd.isValid()) {
      applyTo(w, keyFor(tipCmd.toString()), /*placeholder=*/false);
    }
    const QVariant phCmd = w->property(kPhCmd);
    if (phCmd.isValid()) {
      applyTo(w, keyFor(phCmd.toString()), /*placeholder=*/true);
    }
  };
  handle(root);
  const QList<QWidget*> children = root->findChildren<QWidget*>();
  for (QWidget* w : children) {
    handle(w);
  }
}

}  // namespace ViewerHints
}  // namespace Farman
