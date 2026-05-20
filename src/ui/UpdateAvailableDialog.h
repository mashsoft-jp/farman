#pragma once

#include "core/UpdateChecker.h"

#include <QDialog>
#include <QString>

class QTextBrowser;

namespace Farman {

// 「新しい farman が公開されています」と知らせるモードレスダイアログ。
// 起動直後の自動チェックが「新版あり」を見つけたとき MainWindow からポップアップ
// される。手動 "Check for Updates..." 経由でも同じダイアログを使う (Phase A の
// simple inform() 置き換え)。
//
// 構成:
//   タイトル: "Update available — farman X.Y.Z"
//   本文上段: "Current: A.B.C / Latest: X.Y.Z" + Release page リンク
//   本文中段: リリースノート (Markdown を QTextBrowser::setMarkdown で簡易整形)
//   ボタン:   [Update Now] [Remind Me Later] [Skip This Version]
//
// 終了シグナルでユーザー選択を返す (action は enum):
//   UpdateNow   → MainWindow が htmlUrl をブラウザで開く (Phase B) /
//                  Phase C ではアセット選択 + ダウンロード起動。
//   RemindLater → 何もせず閉じる。次回 (24h 後) のチェックでまた表示される。
//   Skip        → Settings::addAutoUpdateSkippedVersion でバージョン記録。
class UpdateAvailableDialog : public QDialog {
  Q_OBJECT

public:
  enum class Action {
    Closed,        // ダイアログを閉じただけ (X / Esc) → Remind 扱い
    UpdateNow,
    RemindLater,
    Skip
  };

  // currentVersion はラベル表示用。release は GitHub から取った情報。
  UpdateAvailableDialog(const QString& currentVersion,
                        const ReleaseInfo& release,
                        QWidget* parent = nullptr);

  // ユーザーが押したボタンを返す。close 後のみ valid。
  Action lastAction() const { return m_action; }
  // 表示中のリリース情報 (Update Now の遷移先 URL を呼出側が使う)。
  const ReleaseInfo& release() const { return m_release; }

private:
  Action      m_action = Action::Closed;
  ReleaseInfo m_release;
  QTextBrowser* m_notesView = nullptr;
};

} // namespace Farman
