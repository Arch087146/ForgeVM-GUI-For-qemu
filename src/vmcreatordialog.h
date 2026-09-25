#ifndef VMCREATORDIALOG_H
#define VMCREATORDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QProcess>
#include <QMap>
#include "vmconfig.h"

class VMCreatorDialog : public QDialog {
    Q_OBJECT
public:
    explicit VMCreatorDialog(QWidget *parent = nullptr);
    explicit VMCreatorDialog(const VMConfig &existing, QWidget *parent = nullptr);

    VMConfig config() const;
    QString diskPath() const;
    bool createDisk() const;
    static QString convertDiskImage(QWidget *parent, const QString &path);

private slots:
    void onArchChanged();
    void onOsTypeChanged(const QString &type);
    void onPickIcon();
    void onScanDirectory();
    void onAddExtraDrive();
    void onRemoveExtraDrive();
    void onAddPortForward();
    void onRemovePortForward();
    void onAddUSBDevice();
    void onRemoveUSBDevice();
    void onAddSharedDir();
    void onRemoveSharedDir();

private:
    void setupUI();
    void setupSystemTab(QTabWidget *tabs);
    void setupDisplayTab(QTabWidget *tabs);
    void setupStorageTab(QTabWidget *tabs);
    void setupNetworkTab(QTabWidget *tabs);
    void setupSoundTab(QTabWidget *tabs);
    void setupSharingTab(QTabWidget *tabs);
    void setupUSBTab(QTabWidget *tabs);
    void setupBootTab(QTabWidget *tabs);
    void setupAdvancedTab(QTabWidget *tabs);
    void populateCombo(QComboBox *combo, const QStringList &items, const QString &current);
    void populateLabeledCombo(QComboBox *combo,
        const QStringList &values, const QStringList &labels,
        const QString &current);
    void loadFromConfig(const VMConfig &cfg);
    void browseDiskPath();
    void createDiskImage();
    void browseKernel();
    void browseInitrd();
    void browseISOPath();
    void updatePortDisplay();
    void updateIconPreview();
    void refreshExtraDrivesTable();
    void refreshPortForwardTable();
    void refreshUSBTable();
    void refreshSharedDirTable();
    void onBootOrderUp();
    void onBootOrderDown();
    void onBootOrderRemove();

    QStringList queryQemuCPUs(const QString &binary);
    static QString machineTooltip(const QString &machine);

    bool m_editing = false;
    QString m_originalId;
    QString m_iconName;

    // Cache for QEMU CPU queries: binary -> CPU list
    QMap<QString, QStringList> m_cpuCache;

    // System
    QLineEdit *m_nameEdit;
    QLabel *m_iconLabel;
    QComboBox *m_archCombo;
    QComboBox *m_osTypeCombo;
    QComboBox *m_machineCombo;
    QComboBox *m_cpuModelCombo;
    QSpinBox *m_cpuCoresSpin;
    QSpinBox *m_cpuSocketsSpin;
    QSpinBox *m_cpuThreadsSpin;
    QSpinBox *m_memorySpin;
    QCheckBox *m_efiCheck;
    QCheckBox *m_kvmCheck;
    QCheckBox *m_tpmCheck;
    QLineEdit *m_productKeyEdit;

    // Display
    QComboBox *m_displayModeCombo;
    QComboBox *m_vgaModelCombo;
    QSpinBox *m_vncPortSpin;
    QCheckBox *m_vga3dCheck;
    QCheckBox *m_vgaQxlCheck;
    QCheckBox *m_usbTabletCheck;

    // Storage
    QTableWidget *m_extraDrivesTable;
    QPushButton *m_addDriveBtn;
    QPushButton *m_createDiskBtn;
    QPushButton *m_scanDirBtn;
    QPushButton *m_removeDriveBtn;

    // Network
    QComboBox *m_networkModeCombo;
    QComboBox *m_networkModelCombo;
    QTableWidget *m_portForwardTable;
    QPushButton *m_addPortBtn;
    QPushButton *m_removePortBtn;

    // Sound
    QCheckBox *m_audioCheck;
    QComboBox *m_soundModelCombo;

    // Sharing
    QTableWidget *m_sharedDirTable;
    QPushButton *m_addShareBtn;
    QPushButton *m_removeShareBtn;
    QCheckBox *m_clipboardCheck;

    // USB
    QTableWidget *m_usbTable;
    QPushButton *m_addUsbBtn;
    QPushButton *m_removeUsbBtn;

    // Boot
    QListWidget *m_bootOrderList;
    QPushButton *m_bootOrderUp;
    QPushButton *m_bootOrderDown;
    QPushButton *m_bootOrderRemove;
    QComboBox *m_bootOrderAdd;
    QLineEdit *m_kernelPathEdit;
    QPushButton *m_kernelBrowseBtn;
    QLineEdit *m_initrdPathEdit;
    QPushButton *m_initrdBrowseBtn;
    QLineEdit *m_kernelCmdlineEdit;

    // Advanced
    QLineEdit *m_extraArgsEdit;
    QPlainTextEdit *m_notesEdit;

    // Data
    QVector<DiskDrive> m_extraDrives;
    QVector<PortForwardRule> m_portForwards;
    QVector<USBPassthrough> m_usbDevices;
    QVector<SharedDirectory> m_sharedDirs;
};

#endif
