#pragma once

#include <QObject>
#include <QHash>
#include <QMap>
#include <QString>
#include <QList>
#include <QKeySequence>

class QKeyEvent;

namespace Farman {

// 同梱ビュアーの再割り当て可能ショートカットを保持するストア（シングルトン）。
//
// 既存の KeyBindingManager が「key→command のグローバル一意マップ」なのに対し、
// ビュアーは「同じキーが別ビュアーでは別機能」になり得るため、ここは
// **ビュアー別スコープ**で持つ（同じキーを別ビュアーの別コマンドに割当可）。
//
// 永続化は QSettings("farman","farman") の "viewerkeybindings/json"。既定と異なる
// エントリだけを保存し、ロード時は defaults に上書きをマージする（アプリ更新で
// 追加された新コマンドの既定キーが既存ユーザーにも反映される = バックフィル）。
// KeyBindingManager.cpp を持たないプラグイン (media) からも同じ QSettings を読める
// よう、本クラスはプラグイン共通ソースにも含める。
class ViewerKeyBindingManager : public QObject {
  Q_OBJECT
public:
  static ViewerKeyBindingManager& instance();

  void loadFromSettings();
  void saveToSettings();
  // 全コマンドを既定キーへ戻す（メモリ上のみ。保存は saveToSettings で）。
  void resetAllToDefaults();
  // 指定ビュアーのコマンドだけ既定へ戻す。
  void resetViewerToDefaults(const QString& viewerId);

  // commandId に現在割り当てられているキー群。
  QList<QKeySequence> keysFor(const QString& commandId) const;
  // commandId のキーを設定（UI からの変更）。保存は別途 saveToSettings。
  void setKeys(const QString& commandId, const QList<QKeySequence>& keys);

  // 指定ビュアー内で seq に割り当てられたコマンド ID（無ければ空文字）。dispatch 用。
  QString commandForKey(const QString& viewerId, const QKeySequence& seq) const;

  // 指定ビュアー内で seq が exceptCommandId 以外のコマンドに割当済みならその ID を返す
  // （競合検出）。競合が無ければ空文字。
  QString conflictCommand(const QString& viewerId, const QKeySequence& seq,
                          const QString& exceptCommandId) const;

  // commandId の代表キー（先頭）をネイティブ表記で返す（ツールチップ用）。無ければ空。
  QString primaryKeyText(const QString& commandId) const;

  // キーイベントから照合用の QKeySequence を作る。修飾キー単独は空を返し、
  // Keypad 修飾は無視する（KeybindingTab の記録処理と同じ正規化）。
  static QKeySequence sequenceForEvent(const QKeyEvent* ke);

signals:
  void bindingsChanged();

private:
  ViewerKeyBindingManager() = default;
  void ensureLoaded();
  void applyDefaults();       // m_keys / m_viewerOf を既定で初期化
  void rebuildReverse();      // m_reverse を m_keys から再構築

  QHash<QString, QList<QKeySequence>> m_keys;      // commandId -> 現在キー
  QHash<QString, QString>             m_viewerOf;  // commandId -> viewerId
  // viewerId -> (key -> commandId) 逆引き（QKeySequence は operator< を持つので QMap）
  QHash<QString, QMap<QKeySequence, QString>> m_reverse;
  bool m_loaded = false;
};

} // namespace Farman
