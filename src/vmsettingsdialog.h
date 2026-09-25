#ifndef VMSETTINGSDIALOG_H
#define VMSETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include "vmconfig.h"

class VMSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit VMSettingsDialog(const VMConfig &config, QWidget *parent = nullptr);

    VMConfig config() const;

private:
    void setupUI(const VMConfig &config);
    void browseDiskPath();
    void browseKernel();
    void browseInitrd();
    void updatePortDisplay();
    void onArchChanged();
    void populateCombo(QComboBox *combo, const QStringList &items, const QString &current);

    QLineEdit *m_nameEdit;
    QComboBox *m_archCombo;
    QComboBox *m_osTypeCombo;
    QComboBox *m_machineCombo;
    QComboBox *m_cpuModelCombo;
    QSpinBox *m_cpuCoresSpin;
    QSpinBox *m_memorySpin;
    QCheckBox *m_efiCheck;
    QCheckBox *m_kvmCheck;
    QCheckBox *m_audioCheck;
    QLineEdit *m_productKeyEdit;

    QLineEdit *m_diskPathEdit;
    QPushButton *m_diskBrowseBtn;
    QComboBox *m_diskFormatCombo;
    QComboBox *m_diskInterfaceCombo;

    QComboBox *m_networkModeCombo;
    QComboBox *m_networkModelCombo;

    QComboBox *m_displayModeCombo;
    QSpinBox *m_vncPortSpin;
    QCheckBox *m_vga3dCheck;
    QCheckBox *m_vgaQxlCheck;
    QCheckBox *m_clipboardCheck;

    QLineEdit *m_kernelPathEdit;
    QPushButton *m_kernelBrowseBtn;
    QLineEdit *m_initrdPathEdit;
    QPushButton *m_initrdBrowseBtn;
    QLineEdit *m_kernelCmdlineEdit;

    QLineEdit *m_extraArgsEdit;
    QPlainTextEdit *m_notesEdit;

    QTabWidget *m_tabs;
};

#endif
