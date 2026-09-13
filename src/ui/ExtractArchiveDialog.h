#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QLineEdit;
class QPushButton;

namespace Farman {

// アーカイブ展開ダイアログ。
// - 対象アーカイブパス（表示のみ）
// - 出力先パス（既定は反対ペイン）+ Browse
// - アーカイブ名のディレクトリを作るかどうか（既定は作る）
class ExtractArchiveDialog : public QDialog {
  Q_OBJECT

public:
  // defaultOutputDir: 既定の出力先 (=「相手ペイン」)。初期表示値。
  // sourcePaneDir:    アクティブペイン (アーカイブが置かれているディレクトリ)。
  //                   ↑/↓ キーで defaultOutputDir との間をトグルする。
  // TransferConfirmDialog (Copy/Move) と同じ UX。
  ExtractArchiveDialog(const QString& archivePath,
                       const QString& defaultOutputDir,
                       const QString& sourcePaneDir,
                       QWidget*       parent = nullptr);
  ~ExtractArchiveDialog() override = default;

  QString outputDirectory() const;

  // 出力先の下に「アーカイブ名のディレクトリ」を作ってから展開するか。
  // false なら出力先へ直接展開する (中身が散らばるので既定は true)。
  bool createSubdirectory() const;

protected:
  void keyPressEvent(QKeyEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
  void onBrowseDir();

private:
  void setupUi(const QString& archivePath, const QString& defaultOutputDir);

  QString      m_destPaneDir;     // 相手ペインのカレント (= 既定値)
  QString      m_sourcePaneDir;   // 自分ペインのカレント (= ↑↓トグル相手)
  QLineEdit*   m_dirEdit;
  QPushButton* m_browseButton;
  QCheckBox*   m_createSubdirCheck = nullptr;
};

} // namespace Farman
