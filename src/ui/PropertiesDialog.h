#pragma once

#include <QDateTime>
#include <QDialog>
#include <QString>
#include <QStringList>

class QBoxLayout;
class QCheckBox;
class QDateTimeEdit;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;

namespace Farman {

class PropertiesWorker;

// 選択したファイル / ディレクトリの詳細を表示し、変更可能な属性 (パーミッション /
// Windows 属性) をその場で編集できる統合ダイアログ。`Alt+Enter` (file.properties)
// と `a` (attributes) の両方から開かれる。
//
// 単一選択ではフル情報を、複数選択では「N 個の項目」と合計サイズを表示する。
// ディレクトリを含む / 複数選択のときはサイズ集計を別スレッド (PropertiesWorker)
// で行い、途中経過を表示する。閉じる前にキャンセルすると計算は中断される。
// 属性編集は OS で異なる (macOS/Linux = rwx パーミッション、Windows =
// Read-only / Hidden)。複数選択時は混在を PartiallyChecked で表し、ユーザーが
// 触った項目だけを適用する。
class PropertiesDialog : public QDialog {
  Q_OBJECT

public:
  PropertiesDialog(const QStringList& paths, QWidget* parent = nullptr);
  ~PropertiesDialog() override;

protected:
  void closeEvent(QCloseEvent* event) override;

private slots:
  void onStatsUpdated(qint64 totalBytes, int fileCount, int dirCount);
  void onWorkerFinished(bool success);
  void onAccepted();

private:
  void setupUi();
  void populateStaticInfo();
  void buildAttributeEditor(QBoxLayout* parentLayout);
  void loadAttributes();
  void applyAttributes();
  void applyEditableFields();  // 更新日時 / 所有者 / グループの変更を適用

  QStringList      m_paths;
  bool             m_aggregateSize = false;  // ディレクトリ含む or 複数 → worker 集計

  // 行ごとの表示/非表示を切り替えるために form layout を保持する。
  QFormLayout*     m_form = nullptr;

  // 一覧表示用のラベル群
  QLineEdit*       m_nameEdit         = nullptr;     // 名前 (リネーム可能)
  QLabel*          m_pathLabel        = nullptr;     // パス (折り返して全体表示)
  QLabel*          m_typeLabel        = nullptr;
  QLabel*          m_sizeLabel        = nullptr;     // 動的更新
  QDateTimeEdit*   m_modifiedEdit     = nullptr;     // 更新日時 (編集可能)
  QLabel*          m_createdLabel     = nullptr;
  QLabel*          m_accessedLabel    = nullptr;
  QLineEdit*       m_ownerEdit        = nullptr;     // 所有者名 (編集可能, Unix)
  QLabel*          m_ownerIdLabel     = nullptr;     // 所有者 ID (表示のみ)
  QWidget*         m_ownerWidget      = nullptr;     // 名前 + ID をまとめた行
  QLineEdit*       m_groupEdit        = nullptr;     // グループ名 (編集可能, Unix)
  QLabel*          m_groupIdLabel     = nullptr;     // グループ ID (表示のみ)
  QWidget*         m_groupWidget      = nullptr;     // 名前 + ID をまとめた行
  QLabel*          m_linkTargetLabel  = nullptr;

  // 変更検出用の元値 (変わったものだけ適用する)
  QString          m_origName;
  QDateTime        m_origModified;
  QString          m_origOwner;
  QString          m_origGroup;

  // 複数選択時、所有者 / グループが全対象で共通のときだけ編集可能にする。
  bool             m_ownerEditable = false;
  bool             m_groupEditable = false;

  // 編集セクション (属性)。プラットフォームで持つチェックボックスが異なる。
#ifdef Q_OS_WIN
  QCheckBox*       m_readOnlyCheck = nullptr;
  QCheckBox*       m_hiddenCheck   = nullptr;
#else
  QCheckBox*       m_ownerRead  = nullptr;
  QCheckBox*       m_ownerWrite = nullptr;
  QCheckBox*       m_ownerExec  = nullptr;
  QCheckBox*       m_groupRead  = nullptr;
  QCheckBox*       m_groupWrite = nullptr;
  QCheckBox*       m_groupExec  = nullptr;
  QCheckBox*       m_otherRead  = nullptr;
  QCheckBox*       m_otherWrite = nullptr;
  QCheckBox*       m_otherExec  = nullptr;
#endif

  // ディレクトリ / 複数選択の集計用ワーカー (不要なときは nullptr)
  PropertiesWorker* m_worker = nullptr;

  QPushButton*     m_okButton     = nullptr;
  QPushButton*     m_cancelButton = nullptr;
};

} // namespace Farman
