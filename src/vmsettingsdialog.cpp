#include "vmsettingsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDir>

VMSettingsDialog::VMSettingsDialog(const VMConfig &config, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("VM Settings - " + config.name);
    setMinimumSize(580, 620);
    resize(620, 680);
    setupUI(config);
}

void VMSettingsDialog::setupUI(const VMConfig &cfg) {
    auto *mainLayout = new QVBoxLayout(this);

    m_tabs = new QTabWidget;

    // === System Tab ===
    auto *sysTab = new QWidget;
    auto *sysLayout = new QFormLayout(sysTab);
    sysLayout->setSpacing(8);

    m_nameEdit = new QLineEdit;
    m_nameEdit->setText(cfg.name);
    sysLayout->addRow("Name:", m_nameEdit);

    m_osTypeCombo = new QComboBox;
    m_osTypeCombo->addItems({"linux", "windows", "freebsd", "openbsd", "other"});
    m_osTypeCombo->setCurrentText(cfg.osType);
    sysLayout->addRow("OS Type:", m_osTypeCombo);

    m_productKeyEdit = new QLineEdit;
    m_productKeyEdit->setText(cfg.productKey);
    m_productKeyEdit->setVisible(cfg.osType == "windows");
    m_productKeyEdit->setPlaceholderText("Product key (Windows only, optional)");
    sysLayout->addRow("Product Key:", m_productKeyEdit);

    m_machineCombo = new QComboBox;
    m_machineCombo->addItems({"q35", "pc", "pc-i440fx-2.1", "virt"});
    m_machineCombo->setCurrentText(cfg.machine);
    sysLayout->addRow("Machine:", m_machineCombo);

    m_cpuModelCombo = new QComboBox;
    m_cpuModelCombo->addItems({"host", "max", "qemu64", "qemu32", "kvm64", "core2duo"});
    m_cpuModelCombo->setCurrentText(cfg.cpuModel);
    sysLayout->addRow("CPU Model:", m_cpuModelCombo);

    m_cpuCoresSpin = new QSpinBox;
    m_cpuCoresSpin->setRange(1, 128);
    m_cpuCoresSpin->setValue(cfg.cpuCores);
    sysLayout->addRow("CPU Cores:", m_cpuCoresSpin);

    m_memorySpin = new QSpinBox;
    m_memorySpin->setRange(128, 1048576);
    m_memorySpin->setValue(cfg.memoryMB);
    m_memorySpin->setSuffix(" MB");
    m_memorySpin->setSingleStep(256);
    sysLayout->addRow("Memory:", m_memorySpin);

    m_efiCheck = new QCheckBox("Enable UEFI (OVMF)");
    m_efiCheck->setChecked(cfg.efiBoot);
    sysLayout->addRow("", m_efiCheck);

    m_kvmCheck = new QCheckBox("Enable KVM acceleration");
    m_kvmCheck->setChecked(cfg.kvmEnabled);
    sysLayout->addRow("", m_kvmCheck);

    m_audioCheck = new QCheckBox("Enable audio");
    m_audioCheck->setChecked(cfg.audioEnabled);
    sysLayout->addRow("", m_audioCheck);

    m_tabs->addTab(sysTab, "System");

    // === Storage Tab ===
    auto *storTab = new QWidget;
    auto *storLayout = new QFormLayout(storTab);
    storLayout->setSpacing(8);

    auto *diskPathRow = new QHBoxLayout;
    m_diskPathEdit = new QLineEdit;
    m_diskPathEdit->setText(cfg.disk.path);
    m_diskBrowseBtn = new QPushButton("Browse...");
    diskPathRow->addWidget(m_diskPathEdit, 1);
    diskPathRow->addWidget(m_diskBrowseBtn);
    storLayout->addRow("Disk Path:", diskPathRow);

    m_diskFormatCombo = new QComboBox;
    m_diskFormatCombo->addItems({"qcow2", "raw", "vmdk", "vdi", "vhd"});
    m_diskFormatCombo->setCurrentText(cfg.disk.format);
    storLayout->addRow("Format:", m_diskFormatCombo);

    m_diskInterfaceCombo = new QComboBox;
    m_diskInterfaceCombo->addItems({"virtio", "ide", "ahci", "nvme", "sata", "scsi"});
    m_diskInterfaceCombo->setCurrentText(cfg.disk.controller);
    storLayout->addRow("Interface:", m_diskInterfaceCombo);

    m_tabs->addTab(storTab, "Storage");

    // === Network Tab ===
    auto *netTab = new QWidget;
    auto *netLayout = new QFormLayout(netTab);
    netLayout->setSpacing(8);

    m_networkModeCombo = new QComboBox;
    m_networkModeCombo->addItems({"user", "bridge", "tap"});
    m_networkModeCombo->setCurrentText(cfg.network.mode);
    netLayout->addRow("Mode:", m_networkModeCombo);

    m_networkModelCombo = new QComboBox;
    m_networkModelCombo->addItems({"virtio-net", "e1000", "e1000e", "rtl8139", "vmxnet3"});
    m_networkModelCombo->setCurrentText(cfg.network.model);
    netLayout->addRow("Model:", m_networkModelCombo);

    m_tabs->addTab(netTab, "Network");

    // === Display Tab ===
    auto *dispTab = new QWidget;
    auto *dispLayout = new QFormLayout(dispTab);
    dispLayout->setSpacing(8);

    m_displayModeCombo = new QComboBox;
    m_displayModeCombo->addItems({"vnc", "spice", "gtk", "sdl", "none"});
    m_displayModeCombo->setCurrentText(cfg.displayMode);
    dispLayout->addRow("Display Mode:", m_displayModeCombo);

    m_vncPortSpin = new QSpinBox;
    m_vncPortSpin->setRange(5900, 5999);
    m_vncPortSpin->setValue(cfg.vncPort);
    dispLayout->addRow("VNC Port:", m_vncPortSpin);

    m_vgaQxlCheck = new QCheckBox("Paravirtual graphics (QXL)");
    m_vgaQxlCheck->setChecked(cfg.vgaQxl);
    dispLayout->addRow("", m_vgaQxlCheck);

    m_vga3dCheck = new QCheckBox("Enable 3D acceleration");
    m_vga3dCheck->setChecked(cfg.vga3d);
    dispLayout->addRow("", m_vga3dCheck);

    m_clipboardCheck = new QCheckBox("Enable clipboard sharing");
    m_clipboardCheck->setChecked(cfg.clipboardShare);
    dispLayout->addRow("", m_clipboardCheck);

    connect(m_displayModeCombo, &QComboBox::currentTextChanged,
            this, &VMSettingsDialog::updatePortDisplay);
    connect(m_osTypeCombo, &QComboBox::currentTextChanged, this,
            [this](const QString &type) {
        m_productKeyEdit->setVisible(type == "windows");
    });

    m_tabs->addTab(dispTab, "Display");

    // === Advanced Tab ===
    auto *advTab = new QWidget;
    auto *advLayout = new QFormLayout(advTab);
    advLayout->setSpacing(8);

    auto *kernelRow = new QHBoxLayout;
    m_kernelPathEdit = new QLineEdit;
    m_kernelPathEdit->setText(cfg.kernelPath);
    m_kernelBrowseBtn = new QPushButton("Browse...");
    kernelRow->addWidget(m_kernelPathEdit, 1);
    kernelRow->addWidget(m_kernelBrowseBtn);
    advLayout->addRow("Kernel:", kernelRow);

    auto *initrdRow = new QHBoxLayout;
    m_initrdPathEdit = new QLineEdit;
    m_initrdPathEdit->setText(cfg.initrdPath);
    m_initrdBrowseBtn = new QPushButton("Browse...");
    initrdRow->addWidget(m_initrdPathEdit, 1);
    initrdRow->addWidget(m_initrdBrowseBtn);
    advLayout->addRow("Initrd:", initrdRow);

    m_kernelCmdlineEdit = new QLineEdit;
    m_kernelCmdlineEdit->setText(cfg.kernelCmdline);
    advLayout->addRow("Kernel Cmdline:", m_kernelCmdlineEdit);

    m_extraArgsEdit = new QLineEdit;
    m_extraArgsEdit->setText(cfg.extraArgs);
    m_extraArgsEdit->setPlaceholderText("Additional QEMU arguments");
    advLayout->addRow("Extra Args:", m_extraArgsEdit);

    m_notesEdit = new QPlainTextEdit;
    m_notesEdit->setPlainText(cfg.notes);
    m_notesEdit->setMaximumHeight(100);
    advLayout->addRow("Notes:", m_notesEdit);

    m_tabs->addTab(advTab, "Advanced");

    mainLayout->addWidget(m_tabs);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_diskBrowseBtn, &QPushButton::clicked, this, &VMSettingsDialog::browseDiskPath);
    connect(m_kernelBrowseBtn, &QPushButton::clicked, this, &VMSettingsDialog::browseKernel);
    connect(m_initrdBrowseBtn, &QPushButton::clicked, this, &VMSettingsDialog::browseInitrd);

    updatePortDisplay();
}

void VMSettingsDialog::browseDiskPath() {
    QString path = QFileDialog::getOpenFileName(this, "Select Disk Image",
        QDir::homePath(), "Disk Images (*.qcow2 *.raw *.qed *.vmdk *.vdi);;All Files (*)");
    if (!path.isEmpty()) m_diskPathEdit->setText(path);
}

void VMSettingsDialog::browseKernel() {
    QString path = QFileDialog::getOpenFileName(this, "Select Kernel Image",
        QDir::homePath(), "All Files (*)");
    if (!path.isEmpty()) m_kernelPathEdit->setText(path);
}

void VMSettingsDialog::browseInitrd() {
    QString path = QFileDialog::getOpenFileName(this, "Select Initrd",
        QDir::homePath(), "All Files (*)");
    if (!path.isEmpty()) m_initrdPathEdit->setText(path);
}

void VMSettingsDialog::updatePortDisplay() {
    bool vnc = m_displayModeCombo->currentText() == "vnc";
    m_vncPortSpin->setEnabled(vnc);
}

VMConfig VMSettingsDialog::config() const {
    VMConfig cfg;
    cfg.id = "";  // caller sets ID
    cfg.name = m_nameEdit->text().trimmed();
    cfg.osType = m_osTypeCombo->currentText();
    cfg.machine = m_machineCombo->currentText();
    cfg.cpuModel = m_cpuModelCombo->currentText();
    cfg.cpuCores = m_cpuCoresSpin->value();
    cfg.memoryMB = m_memorySpin->value();
    cfg.efiBoot = m_efiCheck->isChecked();
    cfg.kvmEnabled = m_kvmCheck->isChecked();
    cfg.audioEnabled = m_audioCheck->isChecked();

    cfg.disk.path = m_diskPathEdit->text().trimmed();
    cfg.disk.format = m_diskFormatCombo->currentText();
    cfg.disk.controller = m_diskInterfaceCombo->currentText();

    cfg.network.mode = m_networkModeCombo->currentText();
    cfg.network.model = m_networkModelCombo->currentText();

    cfg.displayMode = m_displayModeCombo->currentText();
    cfg.vncPort = m_vncPortSpin->value();
    cfg.vgaQxl = m_vgaQxlCheck->isChecked();
    cfg.vga3d = m_vga3dCheck->isChecked();
    cfg.clipboardShare = m_clipboardCheck->isChecked();

    cfg.kernelPath = m_kernelPathEdit->text().trimmed();
    cfg.initrdPath = m_initrdPathEdit->text().trimmed();
    cfg.kernelCmdline = m_kernelCmdlineEdit->text().trimmed();
    cfg.extraArgs = m_extraArgsEdit->text().trimmed();
    cfg.notes = m_notesEdit->toPlainText().trimmed();
    cfg.productKey = m_productKeyEdit->text().trimmed();

    return cfg;
}
