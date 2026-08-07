#pragma once

#include <QWidget>
#include <QString>
#include <QList>
#include <QKeySequence>

class QKeySequenceEdit;

namespace Farman {

// 1 ビュアー分のショートカット設定 UI（各ビュアーの設定ページに埋め込む再利用
// ウィジェット）。コマンドごとに QKeySequenceEdit を並べ、save() で
// ViewerKeyBindingManager へ書き戻す。プラグインの設定ページから使うので、
// プラグイン共通ソースにも含める。
class ViewerShortcutSettingsWidget : public QWidget {
  Q_OBJECT
public:
  explicit ViewerShortcutSettingsWidget(const QString& viewerId,
                                        QWidget* parent = nullptr);

  // 変更を ViewerKeyBindingManager に反映して永続化する（設定ページの save から呼ぶ）。
  void save();
  // UI を既定キーへ戻す（永続化はしない。続けて save されたら書き込まれる）。
  void restoreDefaults();

private:
  struct Row {
    QString             commandId;
    QList<QKeySequence> defaultKeys;
    QKeySequence        initialKey;   // 表示初期値（現在の割当の先頭）
    QKeySequenceEdit*   edit = nullptr;
  };

  QString     m_viewerId;
  QList<Row>  m_rows;
};

} // namespace Farman
