#pragma once

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QCheckBox;
class QSpinBox;
class QComboBox;
class QTableWidget;
class QDialogButtonBox;
class QLabel;

namespace Farman {

// 選択した複数ファイルをまとめてリネームするダイアログ。
//
// 入力:
//   - Template:  `{name}` `{ext}` `{n}` `{n3}` 等を展開して新名を作る
//   - Find/Replace: テンプレート展開後の名前に対して文字列または正規表現置換
//   - Case: 大文字 / 小文字へ強制変換 (Keep でそのまま)
//   - Number start / step / pad: 連番カウンタの設定
//
// 下半分のプレビューはリアルタイム更新。同じ親ディレクトリ内で衝突する名前
// (バッチ内での重複 + 既存の他ファイルとの衝突) は赤色で示し、Rename ボタンは
// 衝突があれば押せない。
class BulkRenameDialog : public QDialog {
  Q_OBJECT

public:
  // sourceDir: 全ファイルを含む親ディレクトリ (必ず同一前提)
  // names:     リネーム対象のファイル名 (basename 部分のみ、絶対パス不要)
  BulkRenameDialog(const QString&     sourceDir,
                   const QStringList& names,
                   QWidget*           parent = nullptr);

  // OK が押されたあとに使う。<元 basename, 新 basename> のリスト。
  // 元名と新名が同じエントリは省略。
  QList<QPair<QString, QString>> renames() const { return m_renames; }

private slots:
  void refreshPreview();
  void onAccept();

private:
  void setupUi();
  // 与えた index (0-based) の新名を計算する。テンプレート + Find/Replace +
  // Case を順に適用。エラー時は空文字列を返す。
  QString buildNewName(int index, QString* errorOut = nullptr) const;

  QString     m_sourceDir;
  QStringList m_originalNames;

  // Inputs
  QComboBox*  m_templateCombo      = nullptr;  // 編集可。履歴から選べる
  QLineEdit*  m_templateEdit       = nullptr;  // m_templateCombo の入力欄
  QLineEdit*  m_findEdit           = nullptr;
  QLineEdit*  m_replaceEdit        = nullptr;
  QCheckBox*  m_regexCheck         = nullptr;
  QSpinBox*   m_numStartSpin       = nullptr;
  QSpinBox*   m_numStepSpin        = nullptr;
  QSpinBox*   m_numPadSpin         = nullptr;
  QComboBox*  m_caseCombo          = nullptr;

  // Preview
  QTableWidget*    m_previewTable  = nullptr;
  QLabel*          m_warningLabel  = nullptr;
  QDialogButtonBox* m_buttonBox    = nullptr;

  // accept で確定したリネーム結果
  QList<QPair<QString, QString>> m_renames;
};

} // namespace Farman
