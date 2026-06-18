#include "BinaryViewerSettingsPage.h"

#include "settings/Settings.h"
#include "types.h"

#include <QComboBox>
#include <QFormLayout>
#include <QVBoxLayout>

namespace Farman {

BinaryViewerSettingsPage::BinaryViewerSettingsPage(QWidget* parent)
  : IPluginSettingsPage(parent) {
  auto* outer = new QVBoxLayout(this);
  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  m_unitCombo = new QComboBox(this);
  m_unitCombo->addItem(tr("1 Byte"), 1);
  m_unitCombo->addItem(tr("2 Byte"), 2);
  m_unitCombo->addItem(tr("4 Byte"), 4);
  m_unitCombo->addItem(tr("8 Byte"), 8);
  m_unitCombo->setToolTip(tr("Number of bytes grouped per value column."));
  form->addRow(tr("Unit:"), m_unitCombo);

  m_endianCombo = new QComboBox(this);
  m_endianCombo->addItem(tr("Little Endian"),
                         static_cast<int>(BinaryViewerEndian::Little));
  m_endianCombo->addItem(tr("Big Endian"),
                         static_cast<int>(BinaryViewerEndian::Big));
  m_endianCombo->setToolTip(tr("Byte order used when grouping multiple bytes."));
  form->addRow(tr("Endian:"), m_endianCombo);

  m_encodingCombo = new QComboBox(this);
  m_encodingCombo->setEditable(true);
  for (const char* e : { "UTF-8", "UTF-16LE", "UTF-16BE", "Shift_JIS",
                         "EUC-JP", "ISO-8859-1" }) {
    m_encodingCombo->addItem(QString::fromLatin1(e));
  }
  m_encodingCombo->setToolTip(
    tr("Encoding used for the string (ASCII/text) column."));
  form->addRow(tr("String Encoding:"), m_encodingCombo);

  outer->addLayout(form);
  outer->addStretch();

  Settings& s = Settings::instance();
  s.load();
  applyValuesToUi(binaryViewerUnitToBytes(s.binaryViewerUnit()),
                  static_cast<int>(s.binaryViewerEndian()),
                  s.binaryViewerEncoding());
}

void BinaryViewerSettingsPage::applyValuesToUi(int unitBytes, int endian,
                                               const QString& encoding) {
  for (int i = 0; i < m_unitCombo->count(); ++i) {
    if (m_unitCombo->itemData(i).toInt() == unitBytes) {
      m_unitCombo->setCurrentIndex(i);
      break;
    }
  }
  for (int i = 0; i < m_endianCombo->count(); ++i) {
    if (m_endianCombo->itemData(i).toInt() == endian) {
      m_endianCombo->setCurrentIndex(i);
      break;
    }
  }
  m_encodingCombo->setCurrentText(encoding);
}

void BinaryViewerSettingsPage::save() {
  Settings& s = Settings::instance();
  s.setBinaryViewerUnit(bytesToBinaryViewerUnit(m_unitCombo->currentData().toInt()));
  s.setBinaryViewerEndian(
    static_cast<BinaryViewerEndian>(m_endianCombo->currentData().toInt()));
  s.setBinaryViewerEncoding(m_encodingCombo->currentText().trimmed());
  s.save();
}

void BinaryViewerSettingsPage::restoreDefaults() {
  applyValuesToUi(/*unitBytes=*/1, static_cast<int>(BinaryViewerEndian::Little),
                  QStringLiteral("UTF-8"));
}

} // namespace Farman
