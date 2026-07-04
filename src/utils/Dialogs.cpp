#include "Dialogs.h"
#include "FarmanMessageBox.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace Farman {

// 注: このブランチでは inform / warn / critical / confirm / choose を
// 「QMessageBox 派生 (FarmanMessageBox) ベース」で実装する。
// macOS の「キーボードナビゲーション」OS 設定が OFF でも Tab フォーカスと
// Alt+key が動くよう、FarmanMessageBox 側で setFocusPolicy / setTabOrder
// を後付けしている。
//
// informWithSuppress / inputText / withAltMnemonic / applyAltShortcut は
// 既存のままで利用される (チェックボックス付き or 入力欄付きは QMessageBox
// では作れないため独自 QDialog 実装を維持)。

namespace {

// FarmanMessageBox に「OK ボタン + アイコン」をセットアップする共通処理。
void setupSingleOk(FarmanMessageBox& box, QMessageBox::Icon icon,
                   const QString& title, const QString& text) {
  box.setIcon(icon);
  box.setWindowTitle(title);
  box.setText(text);
  auto* okBtn = box.addButton(QObject::tr("OK"), QMessageBox::AcceptRole);
  applyAltShortcut(okBtn, Qt::Key_O);
  box.setDefaultButton(okBtn);
  // FarmanMessageBox::showEvent で enforceTabFocus が呼ばれるので、ここでは
  // ボタン追加だけしておけば OK。
}

} // anonymous namespace

bool confirm(QWidget* parent,
             const QString& title,
             const QString& text,
             bool defaultYes) {
  // Yes / No を QMessageBox のボタンとして動的に追加する。
  // 「No」を左、「Yes」を右に置く (Apple HIG の "Cancel 左 / Action 右"
  // と整合)。Esc で No 扱いになるよう escapeButton 明示。
  FarmanMessageBox box(parent);
  // アイコンは表示しない。標準の Question「?」はダークテーマで埋没し、また
  // 確認ダイアログの視認性にはタイトル + 本文で十分なため省く。
  box.setIcon(QMessageBox::NoIcon);
  box.setWindowTitle(title);
  box.setText(text);
  auto* noBtn  = box.addButton(QObject::tr("No"),  QMessageBox::RejectRole);
  auto* yesBtn = box.addButton(QObject::tr("Yes"), QMessageBox::AcceptRole);
  applyAltShortcut(noBtn,  Qt::Key_N);
  applyAltShortcut(yesBtn, Qt::Key_Y);
  // 修飾キーなしの素の Y / N キーでも即応答できるようにする
  // (Dialogs.h でこの挙動を契約として書いてある)。
  box.setBareKeyShortcut(Qt::Key_Y, yesBtn);
  box.setBareKeyShortcut(Qt::Key_N, noBtn);
  auto* defBtn = defaultYes ? yesBtn : noBtn;
  box.setDefaultButton(defBtn);
  box.setEscapeButton(noBtn);
  defBtn->setFocus();
  box.exec();
  return box.clickedButton() == yesBtn;
}

void inform(QWidget* parent, const QString& title, const QString& text) {
  FarmanMessageBox box(parent);
  setupSingleOk(box, QMessageBox::Information, title, text);
  box.exec();
}

void warn(QWidget* parent, const QString& title, const QString& text) {
  FarmanMessageBox box(parent);
  setupSingleOk(box, QMessageBox::Warning, title, text);
  box.exec();
}

void critical(QWidget* parent, const QString& title, const QString& text) {
  FarmanMessageBox box(parent);
  setupSingleOk(box, QMessageBox::Critical, title, text);
  box.exec();
}

namespace {

// DialogIcon → QMessageBox::Icon マッピング
QMessageBox::Icon toQmIcon(DialogIcon icon) {
  switch (icon) {
    case DialogIcon::None:        return QMessageBox::NoIcon;
    case DialogIcon::Information: return QMessageBox::Information;
    case DialogIcon::Question:    return QMessageBox::Question;
    case DialogIcon::Warning:     return QMessageBox::Warning;
    case DialogIcon::Critical:    return QMessageBox::Critical;
  }
  return QMessageBox::NoIcon;
}

} // anonymous namespace

int choose(QWidget* parent,
           const QString& title,
           const QString& text,
           const QList<DialogButton>& buttons,
           int defaultIndex,
           int cancelIndex,
           DialogIcon icon) {
  FarmanMessageBox box(parent);
  box.setIcon(toQmIcon(icon));
  box.setWindowTitle(title);
  box.setText(text);

  QList<QAbstractButton*> btns;
  for (int i = 0; i < buttons.size(); ++i) {
    const auto& b = buttons.at(i);
    // role は AcceptRole で統一 (押されたボタンの index を clickedButton で
    // 判定するため、role の使い分けは不要)。escapeButton は別途 setEscape
    // Button で指定する。
    auto* btn = box.addButton(b.label, QMessageBox::AcceptRole);
    if (b.altKey != Qt::Key(0)) {
      applyAltShortcut(qobject_cast<QPushButton*>(btn), b.altKey);
    }
    btns.append(btn);
  }

  if (defaultIndex >= 0 && defaultIndex < btns.size()) {
    auto* def = qobject_cast<QPushButton*>(btns[defaultIndex]);
    if (def) {
      box.setDefaultButton(def);
      def->setFocus();
    }
  }
  if (cancelIndex >= 0 && cancelIndex < btns.size()) {
    auto* esc = qobject_cast<QPushButton*>(btns[cancelIndex]);
    if (esc) box.setEscapeButton(esc);
  }

  box.exec();
  QAbstractButton* clicked = box.clickedButton();
  for (int i = 0; i < btns.size(); ++i) {
    if (btns[i] == clicked) return i;
  }
  return cancelIndex;
}

QString withAltMnemonic(const QString& text, Qt::Key key) {
  // & は mnemonic 用の予約文字。再呼び出しに備えて一旦除去する。
  QString out = text;
  out.remove(QLatin1Char('&'));

  const QKeySequence seq(Qt::ALT | key);

#ifdef Q_OS_MAC
  // macOS は & mnemonic を表示しない (Apple HIG)。末尾に "(⌥X)" を添えて
  // ショートカットを視覚的に伝える。既に末尾に hint があれば追加しない。
  const QString hint = QStringLiteral(" (%1)").arg(seq.toString(QKeySequence::NativeText));
  if (out.endsWith(hint)) return out;
  return out + hint;
#else
  // Windows / Linux は & mnemonic がネイティブ流儀。Alt 押下時に該当文字が
  // アンダーラインで描画される。QPushButton / QRadioButton / QCheckBox は
  // この & 表記から Alt+key で自動的にアクティベートする。
  //   - 英語: "Copy" + Qt::Key_C → "&Copy"  (C にアンダーライン)
  //   - 日本語: "コピー" + Qt::Key_C → 該当字無いので "コピー (&C)" と末尾追加
  //     これは Windows の和文ボタン慣習 (例: "OK(&O)") に揃える形。
  const QChar target = QChar(static_cast<int>(key)).toLower();
  for (int i = 0; i < out.length(); ++i) {
    if (out[i].toLower() == target) {
      out.insert(i, QLatin1Char('&'));
      return out;
    }
  }
  // 該当字なし: "(&F)" 末尾追加
  const QChar upper = QChar(static_cast<int>(key));
  return out + QStringLiteral(" (&%1)").arg(upper);
#endif
}

void applyAltShortcut(QPushButton* btn, Qt::Key key) {
  if (!btn) return;
  btn->setText(withAltMnemonic(btn->text(), key));
  btn->setShortcut(QKeySequence(Qt::ALT | key));
  btn->setFocusPolicy(Qt::StrongFocus);
}

bool informWithSuppress(QWidget* parent,
                        const QString& title,
                        const QString& message,
                        bool initiallyShow) {
  QDialog dlg(parent);
  dlg.setWindowTitle(title);
  dlg.setModal(true);
  dlg.resize(480, 0);

  auto* layout = new QVBoxLayout(&dlg);

  auto* msg = new QLabel(message, &dlg);
  msg->setWordWrap(true);
  msg->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(msg);

  // 「次回以降表示しない」(チェック ON で「次回以降表示しない」)
  // initiallyShow=true (= 通常呼び出し: 設定上はまだ表示する) → チェック OFF
  // initiallyShow=false (= 既に「表示しない」設定だが手動で再表示している場合) →
  //   チェック ON で開いて、ユーザーが解除すれば「再び表示する」に戻せる。
  auto* suppressCheck = new QCheckBox(
    QObject::tr("Do not show this dialog next time"), &dlg);
  suppressCheck->setChecked(!initiallyShow);
  layout->addWidget(suppressCheck);

  auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
  auto* okBtn = btnBox->button(QDialogButtonBox::Ok);
  applyAltShortcut(okBtn, Qt::Key_O);
  okBtn->setDefault(true);
  layout->addWidget(btnBox);

  QObject::connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);

  dlg.exec();

  // 戻り値は「次回以降も表示するか」: チェックが OFF なら true (= 表示し続ける)
  return !suppressCheck->isChecked();
}

QString inputText(QWidget* parent,
                  const QString& title,
                  const QString& label,
                  const QString& defaultValue,
                  bool* ok,
                  TextInputCursor cursor) {
  QDialog dlg(parent);
  dlg.setWindowTitle(title);
  dlg.setModal(true);
  dlg.resize(480, 0);

  auto* layout = new QVBoxLayout(&dlg);

  auto* labelWidget = new QLabel(label, &dlg);
  labelWidget->setWordWrap(true);
  layout->addWidget(labelWidget);

  auto* edit = new QLineEdit(defaultValue, &dlg);
  edit->setFocusPolicy(Qt::StrongFocus);
  if (cursor == TextInputCursor::BeforeExtension) {
    // ベース名を選択しつつカーソルを拡張子の手前に置く。
    // 先頭 '.' (ドットファイル) は拡張子としてではなくベース名の一部とみなす。
    //   "foo.txt"      → "foo" を選択、カーソルは '.' の直前
    //   "foo.tar.gz"   → "foo" を選択 (最初の '.' の直前)
    //   ".gitignore"   → 全選択 (拡張子とみなさない)
    //   "Makefile"     → 全選択 ('.' なし)
    // ダイアログ show() 直後に Qt が selectAll を再適用してくることが
    // あるので、QTimer で次のイベントループまで遅延してから上書きする。
    int extPos = -1;
    for (int i = 1; i < defaultValue.length(); ++i) {
      if (defaultValue.at(i) == QLatin1Char('.')) { extPos = i; break; }
    }
    const int pos = (extPos > 0) ? extPos : defaultValue.length();
    QTimer::singleShot(0, edit, [edit, pos]() {
      edit->setFocus();
      // setSelection(start, length) は選択範囲を設定すると同時に
      // カーソルを選択末尾 (= pos) に移動するので、別途 setCursorPosition
      // を呼ぶ必要はない。
      edit->setSelection(0, pos);
    });
  } else {
    edit->selectAll();
  }
  layout->addWidget(edit);

  // 他のダイアログ（ConfirmDialog など）と同じ見た目にするため、
  // QDialogButtonBox ではなく自前の QPushButton を右寄せで配置する。
  auto* btnLayout = new QHBoxLayout();
  btnLayout->addStretch(1);
  auto* cancelBtn = new QPushButton(QObject::tr("Cancel"), &dlg);
  auto* okBtn     = new QPushButton(QObject::tr("OK"),     &dlg);
  applyAltShortcut(cancelBtn, Qt::Key_X);
  applyAltShortcut(okBtn,     Qt::Key_O);
  okBtn->setDefault(true);
  btnLayout->addWidget(cancelBtn);
  btnLayout->addWidget(okBtn);
  layout->addLayout(btnLayout);
  QObject::connect(okBtn,     &QPushButton::clicked, &dlg, &QDialog::accept);
  QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

  // Tab: 入力欄 → Cancel → OK（実行系が最後）
  QWidget::setTabOrder(edit,      cancelBtn);
  QWidget::setTabOrder(cancelBtn, okBtn);

  edit->setFocus();

  const bool accepted = dlg.exec() == QDialog::Accepted;
  if (ok) *ok = accepted;
  return accepted ? edit->text() : QString();
}

} // namespace Farman
