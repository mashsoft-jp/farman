#include "KeyBindingManager.h"
#include "settings/Settings.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QSet>

namespace Farman {

KeyBindingManager& KeyBindingManager::instance() {
  static KeyBindingManager instance;
  return instance;
}

KeyBindingManager::KeyBindingManager(QObject* parent) : QObject(parent) {
}

void KeyBindingManager::bind(const QKeySequence& key, const QString& commandId) {
  if (key.isEmpty()) {
    qWarning() << "KeyBindingManager::bind: empty key sequence";
    return;
  }

  if (commandId.isEmpty()) {
    qWarning() << "KeyBindingManager::bind: empty command id";
    return;
  }

  // Check for conflicts
  if (m_bindings.contains(key) && m_bindings.value(key) != commandId) {
    qWarning() << "KeyBindingManager::bind: key already bound to"
               << m_bindings.value(key) << ", rebinding to" << commandId;
  }

  m_bindings.insert(key, commandId);
  emit bindingsChanged();
}

void KeyBindingManager::unbind(const QKeySequence& key) {
  if (m_bindings.remove(key) > 0) {
    emit bindingsChanged();
  }
}

QString KeyBindingManager::commandFor(const QKeySequence& key) const {
  return m_bindings.value(key, QString());
}

bool KeyBindingManager::isBound(const QKeySequence& key) const {
  return m_bindings.contains(key);
}

QList<QKeySequence> KeyBindingManager::keysForCommand(const QString& commandId) const {
  QList<QKeySequence> result;

  for (auto it = m_bindings.begin(); it != m_bindings.end(); ++it) {
    if (it.value() == commandId) {
      result.append(it.key());
    }
  }

  return result;
}

namespace {

QList<QPair<QKeySequence, QString>> defaultBindingList() {
  return {
    // Navigation
    { QKeySequence(Qt::Key_Up),        "navigate.up"        },
    { QKeySequence(Qt::Key_Down),      "navigate.down"      },
    { QKeySequence(Qt::Key_Left),      "navigate.left"      },
    { QKeySequence(Qt::Key_Right),     "navigate.right"     },
    { QKeySequence(Qt::Key_Home),      "navigate.home"      },
    { QKeySequence(Qt::Key_End),       "navigate.end"       },
    { QKeySequence(Qt::Key_PageUp),    "navigate.pageup"    },
    { QKeySequence(Qt::Key_PageDown),  "navigate.pagedown"  },
    { QKeySequence(Qt::Key_Return),    "navigate.enter"     },
    { QKeySequence(Qt::Key_Enter),     "navigate.enter"     },
    { QKeySequence(Qt::Key_Backspace), "navigate.parent"    },

    // Selection
    // 名前通りの動作に合わせる: Space が「選択して下に移動」、Shift+Space が「カーソル据え置き」。
    { QKeySequence(Qt::Key_Space),                  "select.toggle_and_down" },
    { QKeySequence(Qt::SHIFT | Qt::Key_Space),      "select.toggle"          },
    { QKeySequence(Qt::Key_I),               "select.invert"          },
    { QKeySequence(Qt::CTRL | Qt::Key_A),    "select.all"             },

    // Pane
    // Tab は FileList → アドレスバー へのフォーカス移動に使うため
    // pane.switch のデフォルトバインドからは外している。ペイン切り替えは
    // ← / → の端到達 / マウスクリック / ユーザーが任意キーをバインドすれば良い。
    { QKeySequence(Qt::CTRL | Qt::Key_O),      "pane.toggle_single"        },
    { QKeySequence(Qt::CTRL | Qt::Key_P),      "pane.toggle_preview"       },
    { QKeySequence(Qt::Key_S),                 "pane.sort_filter"          },
    { QKeySequence(Qt::CTRL | Qt::Key_Right),  "pane.sync_other_to_active" },
    { QKeySequence(Qt::CTRL | Qt::Key_Left),   "pane.sync_active_to_other" },
    { QKeySequence(Qt::Key_Y),                 "pane.sync_browse_toggle"   },

    // File operations — 簡略キーボードでも打てる一文字キーをデフォルトに
    { QKeySequence(Qt::Key_C), "file.copy"    },
    { QKeySequence(Qt::Key_M), "file.move"    },
    { QKeySequence(Qt::Key_K), "file.mkdir"   },
    { QKeySequence(Qt::Key_D), "file.delete"  },
    { QKeySequence(Qt::Key_R),               "file.rename"      },
    { QKeySequence(Qt::CTRL | Qt::Key_R),    "file.bulk_rename" },
    { QKeySequence(Qt::CTRL | Qt::Key_C),    "file.copy_path"   },
    { QKeySequence(Qt::Key_N), "file.newfile"    },
    { QKeySequence(Qt::Key_A), "file.attributes" },
    { QKeySequence(Qt::Key_F), "file.search"     },
    { QKeySequence(Qt::Key_P), "file.pack"       },
    { QKeySequence(Qt::Key_U), "file.unpack"     },
    // ファイル実行 (OS 既定アプリで開く)。Enter / Return の両方をバインド。
    { QKeySequence(Qt::SHIFT | Qt::Key_Return), "file.execute" },
    { QKeySequence(Qt::SHIFT | Qt::Key_Enter),  "file.execute" },
    // プロパティダイアログ (Alt+Enter)
    { QKeySequence(Qt::ALT   | Qt::Key_Return), "file.properties" },
    { QKeySequence(Qt::ALT   | Qt::Key_Enter),  "file.properties" },

    // View
    // ディレクトリ比較。"=" は「左右が一致するかを見比べる」操作の語感に合い、
    // 既存の単キーバインドと衝突しない。
    // JIS / US でレイアウトが違うため、Qt が届ける可能性のあるキーシーケンスを
    // 全部登録する: US 配列の "=" 単押し / Shift+Equal / JIS の Shift+- (Minus) /
    // 物理キーが "+" / "Plus" として届く環境。help.shortcuts の "?" 対応と同じ
    // パターン。
    { QKeySequence(Qt::Key_Equal),                   "view.compare_directories" },
    { QKeySequence(Qt::SHIFT | Qt::Key_Equal),       "view.compare_directories" },
    { QKeySequence(Qt::SHIFT | Qt::Key_Minus),       "view.compare_directories" },
    { QKeySequence(Qt::Key_Plus),                    "view.compare_directories" },
    { QKeySequence(Qt::Key_V), "view.file" },
    // 任意ビュアー選択ポップアップ。Enter / Return の両方をバインド。
    { QKeySequence(Qt::CTRL | Qt::Key_Return),  "view.choose"  },
    { QKeySequence(Qt::CTRL | Qt::Key_Enter),   "view.choose"  },
    { QKeySequence(Qt::CTRL | Qt::Key_L),       "view.toggle_log" },
    // 即時フィルタ。vim / less / Total Commander 系ツールの慣習に揃えて
    // bare `/` をデフォルトに採用。"?" (Shift+/) は help.shortcuts に既に
    // 割当があるが、修飾子無しの `/` 自体は他とぶつからない。
    { QKeySequence(Qt::Key_Slash),              "view.quick_filter" },
    // 表示モード切替: List / Thumbnail S/M/L を Cmd+1〜4 で直接選択 (Finder 風)。
    // 4 段階の巡回コマンド (view.toggle_thumbnails) はデフォルトキーを当てず、
    // ユーザーが好みで割り当てる運用 (互換のため CommandRegistry には残す)。
    { QKeySequence(Qt::CTRL | Qt::Key_1),       "view.list"             },
    { QKeySequence(Qt::CTRL | Qt::Key_2),       "view.thumbnail_small"  },
    { QKeySequence(Qt::CTRL | Qt::Key_3),       "view.thumbnail_medium" },
    { QKeySequence(Qt::CTRL | Qt::Key_4),       "view.thumbnail_large"  },

    // Bookmark
    { QKeySequence(Qt::Key_B),            "bookmark.toggle" },
    { QKeySequence(Qt::CTRL | Qt::Key_B), "bookmark.list"   },

    // History
    { QKeySequence(Qt::Key_H), "history.show" },

    // Application
    { QKeySequence(Qt::CTRL | Qt::Key_Comma), "app.settings" },
    { QKeySequence(Qt::CTRL | Qt::Key_Q),     "app.quit"     },

    // Tools (外部アプリ連携の組み込み 2 件)
    // Total Commander の慣習に揃え、ターミナル = T、エディタ = E をデフォルトに。
    // ユーザー定義コマンドは ID が動的なので Settings → Keybindings タブで個別に
    // 割り当てる。
    { QKeySequence(Qt::Key_T), "user.cmd.terminal" },
    { QKeySequence(Qt::Key_E), "user.cmd.editor"   },

    // Help
    // 「?」キーは Shift+/ なので、Shift 修飾子の扱いがプラットフォームや
    // 配列で揺れる。両方登録しておけば確実に拾える。
    { QKeySequence(Qt::Key_Question),                  "help.shortcuts" },
    { QKeySequence(Qt::SHIFT | Qt::Key_Question),      "help.shortcuts" },
    { QKeySequence(Qt::SHIFT | Qt::Key_Slash),         "help.shortcuts" },
    { QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P),  "help.plugins"   },
  };
}

} // anonymous namespace

void KeyBindingManager::loadDefaults() {
  clearAllBindings();
  for (const auto& [key, command] : defaultBindingList()) {
    m_bindings.insert(key, command);
  }
  emit bindingsChanged();
  qDebug() << "KeyBindingManager: loaded" << m_bindings.size() << "default bindings";
}

void KeyBindingManager::loadFromSettings() {
  QSettings settings("farman", "farman");

  QString jsonData = settings.value("keybindings/json").toString();

  if (jsonData.isEmpty()) {
    qDebug() << "KeyBindingManager: no saved bindings, loading defaults";
    loadDefaults();
    return;
  }

  QJsonDocument doc = QJsonDocument::fromJson(jsonData.toUtf8());
  if (!doc.isObject()) {
    qWarning() << "KeyBindingManager::loadFromSettings: invalid JSON format";
    loadDefaults();
    return;
  }

  QJsonObject root = doc.object();
  const int version = root.value("version").toInt(1);

  // version < 2: ファイル操作キーの大幅再編（F5-F8 -> c/m/k/d 等）が入ったため、
  // 古い設定は破棄して新デフォルトを入れ直す。ユーザーがカスタムしていた場合は
  // Settings の Keybindings タブで再設定してもらう。
  // version < 3: F3 -> v (view.file)、F4 -> s (pane.sort_filter) のデフォルト
  // 変更。ファンクションキーが無い簡易キーボードに対応するため。同様に強制リセット。
  // version < 4: select.invert のデフォルトを * から i に変更。`*` は Shift+8 で
  // 入力するため macOS ネイティブメニュー上で modifier がうまく扱えず表示が崩れる。
  // version < 5: select.toggle のデフォルトを Insert から Shift+Space に変更。
  // Mac キーボードに Insert キーが無いため、Mac でも打てるキーに置き換え。
  // version < 6: history.show のデフォルトを Ctrl+Shift+H から h に変更。
  // 単キーで打てるほうが操作的に楽。
  // version < 7: app.settings を F9 → Ctrl+, に、app.quit を F10/Ctrl+Q → Q に
  // 変更し、ファンクションキーを既定から排除。
  // version < 8: app.quit を Q → Ctrl+Q (macOS では Cmd+Q) に戻す。
  // OS 標準のショートカットに揃えるため。
  // version < 9: view.toggle_log (Ctrl+L) を新規追加。
  // version < 10: pane.sync_browse_toggle (y) を新規追加。
  // version < 11: user.cmd.terminal (T) / user.cmd.editor (E) を新規追加。
  // 既存バインドは保持されるが、T / E 用のデフォルトは下の merge ロジックで
  // 自動補完される。
  // version < 12: view.quick_filter (Ctrl+I) を新規追加。同上。
  // version < 13: view.quick_filter のデフォルトを Ctrl+I → bare `/` に変更
  // (vim 流に揃えるため)。同上、強制リセット。
  // version < 14: view.compare_directories ("=") を新規追加。既存バインドは
  // 保持されるが、空いている "=" キーをデフォルトとして補完する。
  // version < 15: view.compare_directories のレイアウト対応を強化。Key_Equal
  // 単押しに加えて Shift+Equal / Shift+Minus (JIS) / Key_Plus も同じコマンドに
  // 紐付けるよう default に追加。既存バインドは保持。
  // version < 16: view.toggle_thumbnails (Ctrl+G) を新規追加。既存バインドは
  // 保持されるが、Ctrl+G が空いていれば下の merge ロジックで自動補完される。
  // (Ctrl+T は macOS の NSWindow システム標準の Toggle Toolbar と衝突するため
  //  避けて Cmd+G / Ctrl+G を採用。"Grid view" の頭文字。)
  // version < 17: view.list / view.thumbnail_small / view.thumbnail_medium /
  // view.thumbnail_large を新規追加し、Cmd+1〜4 をデフォルトに。view.toggle_
  // thumbnails の Ctrl+G デフォルトは外す (コマンド自体は互換のため残る)。
  // ユーザーが Ctrl+G を view.toggle_thumbnails に手動で残していた場合は維持。
  // version < 18: pane.toggle_preview (Ctrl+P) を新規追加。下の merge ロジックで
  // Ctrl+P が空いていれば自動補完される。既存バインドは保持。
  // version < 19: help.plugins (Ctrl+Shift+P) を新規追加。Help → Plugins...
  // と同じプラグイン一覧 / ロードエラー診断をキーボードから開けるようにする。
  if (version < 13) {
    qDebug() << "KeyBindingManager: migrating bindings from version" << version;
    loadDefaults();
    saveToSettings();
    return;
  }

  clearAllBindings();

  QJsonArray bindings = root.value("bindings").toArray();

  QSet<QString> savedCommands;
  for (const QJsonValue& val : bindings) {
    if (!val.isObject()) continue;

    QJsonObject binding = val.toObject();
    QString keyStr = binding.value("key").toString();
    QString commandId = binding.value("command").toString();

    if (keyStr.isEmpty() || commandId.isEmpty()) continue;

    QKeySequence key(keyStr);
    if (!key.isEmpty()) {
      m_bindings.insert(key, commandId);
      savedCommands.insert(commandId);
    }
  }

  // version < 17: view.toggle_thumbnails の旧デフォルト Ctrl+G を外す。
  // ユーザーが Ctrl+G を view.toggle_thumbnails のままにしていた場合のみ削除し、
  // 別コマンドに再割当していた場合は維持する。merge 前に処理することで、
  // 下の merge ロジックで Cmd+1〜4 等の新規デフォルトが補完される。
  if (version < 17) {
    const QKeySequence oldKey(Qt::CTRL | Qt::Key_G);
    auto it = m_bindings.find(oldKey);
    if (it != m_bindings.end()
        && it.value() == QStringLiteral("view.toggle_thumbnails")) {
      savedCommands.remove(QStringLiteral("view.toggle_thumbnails"));
      m_bindings.erase(it);
    }
  }

  // 保存データに含まれない新規コマンドについてはデフォルトを補完する。
  // これにより、アプリアップデートで追加されたコマンドが既存ユーザー環境でも
  // 動作する。保存済みキーと衝突する場合は上書きせずスキップ。
  int merged = 0;
  for (const auto& [key, command] : defaultBindingList()) {
    if (savedCommands.contains(command)) continue;
    if (m_bindings.contains(key))        continue;
    m_bindings.insert(key, command);
    ++merged;
  }

  qDebug() << "KeyBindingManager: loaded" << m_bindings.size()
           << "bindings from settings ("
           << merged << "merged from defaults)";

  emit bindingsChanged();
}

void KeyBindingManager::saveToSettings() const {
  QJsonArray bindings;

  for (auto it = m_bindings.begin(); it != m_bindings.end(); ++it) {
    QJsonObject binding;
    binding["key"] = it.key().toString();
    binding["command"] = it.value();
    bindings.append(binding);
  }

  QJsonObject root;
  root["bindings"] = bindings;
  root["version"] = 19;

  QJsonDocument doc(root);
  QString jsonData = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));

  QSettings settings("farman", "farman");
  settings.setValue("keybindings/json", jsonData);
  settings.sync();

  qDebug() << "KeyBindingManager: saved" << m_bindings.size() << "bindings to settings";
}

QMap<QKeySequence, QString> KeyBindingManager::allBindings() const {
  return m_bindings;
}

void KeyBindingManager::clearAllBindings() {
  if (!m_bindings.isEmpty()) {
    m_bindings.clear();
    emit bindingsChanged();
  }
}

} // namespace Farman
