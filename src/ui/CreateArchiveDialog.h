#pragma once

#include "core/workers/ArchiveCreateWorker.h"
#include <QDialog>
#include <QString>
#include <QStringList>

class QLineEdit;
class QComboBox;
class QPushButton;

namespace Farman {

// 選択ファイルをアーカイブ化する前の設定ダイアログ。
// - Format（zip / tar / tar.gz / tar.bz2 / tar.xz）
// - Output directory（既定は反対ペイン）
// - Output filename
class CreateArchiveDialog : public QDialog {
  Q_OBJECT

public:
  // defaultOutputDir: 既定の出力先 (=「相手ペイン」)。初期表示値。
  // sourcePaneDir:    アクティブペイン (圧縮対象が置かれているディレクトリ)。
  //                   ↑/↓ キーで defaultOutputDir との間をトグルする。
  // TransferConfirmDialog (Copy/Move) と同じ UX。
  CreateArchiveDialog(const QStringList& inputPaths,
                      const QString&     defaultOutputDir,
                      const QString&     sourcePaneDir,
                      QWidget*           parent = nullptr);
  ~CreateArchiveDialog() override = default;

  QString                      outputPath() const;
  ArchiveCreateWorker::Format  format()     const;

  // 作成オプション。zip 以外 / パスワード空のときは暗号化なし扱いを返す。
  QString                          passphrase()       const;
  ArchiveCreateWorker::Encryption  encryption()       const;
  int                              compressionLevel() const;  // -1 = 既定

protected:
  void keyPressEvent(QKeyEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
  void onBrowseDir();
  void onFormatChanged();
  // OK 押下時の検証 (パスワード一致など)。問題なければ accept() する。
  void tryAccept();

private:
  void setupUi(const QString& defaultOutputDir);
  QString baseName() const;
  QString extensionForFormat(ArchiveCreateWorker::Format fmt) const;

  QString      m_destPaneDir;     // 相手ペイン (= 既定値)
  QString      m_sourcePaneDir;   // 自分ペイン (= ↑↓トグル相手)
  QStringList  m_inputPaths;
  QComboBox*   m_formatCombo;
  QLineEdit*   m_dirEdit;
  QPushButton* m_browseButton;
  QLineEdit*   m_nameEdit;
  QComboBox*   m_compressionCombo     = nullptr;  // 圧縮レベル (-1=既定 / 0〜9)
  QLineEdit*   m_passwordEdit         = nullptr;  // zip 暗号化パスワード (AES-256 固定)
  QLineEdit*   m_passwordConfirmEdit  = nullptr;  // 確認用
};

} // namespace Farman
