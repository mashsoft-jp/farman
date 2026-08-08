#include "ViewerKeyBindingManager.h"
#include "ViewerCommands.h"
#include "viewer/IViewerPlugin.h"

#include <QMetaObject>
#include <QVariant>
#include <QWidget>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QKeyEvent>
#include <QStringList>

namespace Farman {

namespace {

constexpr int kSchemaVersion = 1;
const char* kSettingsKey = "viewerkeybindings/json";

QList<QKeySequence> parseKeys(const QJsonArray& arr) {
  QList<QKeySequence> keys;
  for (const QJsonValue& v : arr) {
    const QString s = v.toString().trimmed();
    if (s.isEmpty()) {
      continue;
    }
    QKeySequence seq(s);
    if (!seq.isEmpty()) {
      keys.append(seq);
    }
  }
  return keys;
}

QJsonArray keysToJson(const QList<QKeySequence>& keys) {
  QJsonArray arr;
  for (const QKeySequence& k : keys) {
    if (!k.isEmpty()) {
      arr.append(k.toString());  // ポータブル文字列（NativeText ではない）
    }
  }
  return arr;
}

bool sameKeys(const QList<QKeySequence>& a, const QList<QKeySequence>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (int i = 0; i < a.size(); ++i) {
    if (a.at(i) != b.at(i)) {
      return false;
    }
  }
  return true;
}

} // namespace

ViewerKeyBindingManager& ViewerKeyBindingManager::instance() {
  static ViewerKeyBindingManager s;
  return s;
}

void ViewerKeyBindingManager::applyDefaults() {
  m_keys.clear();
  m_viewerOf.clear();
  for (const ViewerCommandDef& def : viewerCommandDefs()) {
    m_keys.insert(def.commandId, def.defaultKeys);
    m_viewerOf.insert(def.commandId, def.viewerId);
  }
}

void ViewerKeyBindingManager::rebuildReverse() {
  m_reverse.clear();
  for (auto it = m_keys.constBegin(); it != m_keys.constEnd(); ++it) {
    const QString& commandId = it.key();
    const QString viewerId = m_viewerOf.value(commandId);
    if (viewerId.isEmpty()) {
      continue;
    }
    for (const QKeySequence& seq : it.value()) {
      if (seq.isEmpty()) {
        continue;
      }
      // 同一ビュアー内でキー重複時は先勝ち（UI 側で競合検出して防ぐ）。
      QMap<QKeySequence, QString>& rev = m_reverse[viewerId];
      if (!rev.contains(seq)) {
        rev.insert(seq, commandId);
      }
    }
  }
}

void ViewerKeyBindingManager::ensureLoaded() {
  if (!m_loaded) {
    loadFromSettings();
  }
}

void ViewerKeyBindingManager::loadFromSettings() {
  applyDefaults();

  QSettings settings(QStringLiteral("farman"), QStringLiteral("farman"));
  const QString json = settings.value(QLatin1String(kSettingsKey)).toString();
  if (!json.isEmpty()) {
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isObject()) {
      const QJsonArray bindings = doc.object().value(QStringLiteral("bindings")).toArray();
      for (const QJsonValue& v : bindings) {
        const QJsonObject o = v.toObject();
        const QString commandId = o.value(QStringLiteral("command")).toString();
        if (commandId.isEmpty() || !m_keys.contains(commandId)) {
          continue;  // 未知コマンドは無視（= 既定のまま。新規コマンドはバックフィルされる）
        }
        m_keys[commandId] = parseKeys(o.value(QStringLiteral("keys")).toArray());
      }
    }
  }

  rebuildReverse();
  m_loaded = true;
}

void ViewerKeyBindingManager::saveToSettings() {
  // 既定と異なるものだけ保存する（新バージョンの既定変更を反映しやすくするため）。
  QHash<QString, QList<QKeySequence>> defaults;
  for (const ViewerCommandDef& def : viewerCommandDefs()) {
    defaults.insert(def.commandId, def.defaultKeys);
  }

  QJsonArray bindings;
  for (auto it = m_keys.constBegin(); it != m_keys.constEnd(); ++it) {
    const QList<QKeySequence> def = defaults.value(it.key());
    if (sameKeys(it.value(), def)) {
      continue;
    }
    QJsonObject o;
    o.insert(QStringLiteral("command"), it.key());
    o.insert(QStringLiteral("keys"), keysToJson(it.value()));
    bindings.append(o);
  }

  QJsonObject root;
  root.insert(QStringLiteral("version"), kSchemaVersion);
  root.insert(QStringLiteral("bindings"), bindings);

  QSettings settings(QStringLiteral("farman"), QStringLiteral("farman"));
  settings.setValue(QLatin1String(kSettingsKey),
                    QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)));

  rebuildReverse();
  emit bindingsChanged();
}

void ViewerKeyBindingManager::resetAllToDefaults() {
  applyDefaults();
  rebuildReverse();
}

void ViewerKeyBindingManager::resetViewerToDefaults(const QString& viewerId) {
  ensureLoaded();
  for (const ViewerCommandDef& def : viewerCommandDefs()) {
    if (def.viewerId == viewerId) {
      m_keys[def.commandId] = def.defaultKeys;
    }
  }
  rebuildReverse();
}

QList<QKeySequence> ViewerKeyBindingManager::keysFor(const QString& commandId) const {
  const_cast<ViewerKeyBindingManager*>(this)->ensureLoaded();
  return m_keys.value(commandId);
}

void ViewerKeyBindingManager::setKeys(const QString& commandId,
                                      const QList<QKeySequence>& keys) {
  ensureLoaded();
  if (!m_keys.contains(commandId)) {
    return;
  }
  m_keys[commandId] = keys;
  rebuildReverse();
}

QString ViewerKeyBindingManager::commandForKey(const QString& viewerId,
                                               const QKeySequence& seq) const {
  const_cast<ViewerKeyBindingManager*>(this)->ensureLoaded();
  if (seq.isEmpty()) {
    return QString();
  }
  const auto it = m_reverse.constFind(viewerId);
  if (it == m_reverse.constEnd()) {
    return QString();
  }
  return it.value().value(seq);
}

QString ViewerKeyBindingManager::conflictCommand(const QString& viewerId,
                                                 const QKeySequence& seq,
                                                 const QString& exceptCommandId) const {
  const QString cmd = commandForKey(viewerId, seq);
  if (cmd.isEmpty() || cmd == exceptCommandId) {
    return QString();
  }
  return cmd;
}

QString ViewerKeyBindingManager::primaryKeyText(const QString& commandId) const {
  const QList<QKeySequence> keys = keysFor(commandId);
  if (keys.isEmpty()) {
    return QString();
  }
  return keys.first().toString(QKeySequence::NativeText);
}

QVariantMap ViewerKeyBindingManager::bindingsMapForViewer(const QString& viewerId) const {
  const_cast<ViewerKeyBindingManager*>(this)->ensureLoaded();
  QVariantMap map;
  for (auto it = m_keys.constBegin(); it != m_keys.constEnd(); ++it) {
    if (m_viewerOf.value(it.key()) != viewerId) {
      continue;
    }
    QStringList keyTexts;
    for (const QKeySequence& seq : it.value()) {
      if (!seq.isEmpty()) {
        keyTexts << seq.toString(QKeySequence::PortableText);
      }
    }
    map.insert(it.key(), keyTexts);
  }
  return map;
}

QKeySequence ViewerKeyBindingManager::sequenceForEvent(const QKeyEvent* ke) {
  if (!ke) {
    return QKeySequence();
  }
  const int key = ke->key();
  // 修飾キー単独は無効。
  if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt
      || key == Qt::Key_Meta || key == 0) {
    return QKeySequence();
  }
  Qt::KeyboardModifiers mods = ke->modifiers();
  mods &= ~Qt::KeypadModifier;  // テンキー修飾は無視
  return QKeySequence(QKeyCombination(mods, static_cast<Qt::Key>(key)));
}

namespace {
// view 自身、または子孫のうち applyShortcutBindings(QVariantMap) を持つ
// ウィジェットを返す。media のようにプラグインの createViewer がラッパ
// (MediaViewerWindow 等) を返し、実体ビュー (MediaView) がその子である場合に、
// ラッパではなく実体へ push できるようにする。組込みビュアーは view 自身が
// 該当メソッドを持つのでそのまま返る。
QWidget* shortcutBindingReceiver(QWidget* view) {
  if (!view) {
    return nullptr;
  }
  if (view->metaObject()->indexOfMethod("applyShortcutBindings(QVariantMap)") >= 0) {
    return view;
  }
  const QList<QWidget*> kids = view->findChildren<QWidget*>();
  for (QWidget* c : kids) {
    if (c->metaObject()->indexOfMethod("applyShortcutBindings(QVariantMap)") >= 0) {
      return c;
    }
  }
  return nullptr;
}
} // namespace

void pushViewerShortcutBindings(QWidget* view, const QString& viewerId) {
  if (viewerId.isEmpty()) {
    return;
  }
  QWidget* recv = shortcutBindingReceiver(view);
  if (!recv) {
    return;
  }
  const QVariantMap map =
    ViewerKeyBindingManager::instance().bindingsMapForViewer(viewerId);
  QMetaObject::invokeMethod(recv, "applyShortcutBindings",
                            Q_ARG(QVariantMap, map));
}

void pushViewerShortcutBindings(QWidget* view, IViewerPlugin* plugin) {
  if (!view || !plugin) {
    return;
  }
  // 取得 API（各ビュアーが自分の設定可能項目を返す）から viewerId を解決する。
  // 1 プラグイン内のコマンドは同じ viewerId を共有する前提。設定可能項目を
  // 持たない外部プラグインでは何もしない（一方向 push なので副作用なし）。
  const QList<ViewerCommandDef> defs = plugin->shortcutCommands();
  if (defs.isEmpty()) {
    return;
  }
  pushViewerShortcutBindings(view, defs.first().viewerId);
}

} // namespace Farman
