#include "vmcreatordialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QProcess>
#include <QCompleter>
#include <QGridLayout>
#include <QScrollArea>
#include <QIcon>
#include <QPixmap>
#include <QFileInfo>
#include <algorithm>

VMCreatorDialog::VMCreatorDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Create New VM");
    setMinimumSize(640, 700);
    resize(700, 750);
    m_iconName = "linux";
    setupUI();
}

VMCreatorDialog::VMCreatorDialog(const VMConfig &existing, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Edit VM - " + existing.name);
    setMinimumSize(640, 700);
    resize(700, 750);
    m_editing = true;
    m_originalId = existing.id;
    setupUI();
    loadFromConfig(existing);
}

void VMCreatorDialog::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);
    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);

    setupSystemTab(tabs);
    setupDisplayTab(tabs);
    setupStorageTab(tabs);
    setupNetworkTab(tabs);
    setupSoundTab(tabs);
    setupSharingTab(tabs);
    setupUSBTab(tabs);
    setupBootTab(tabs);
    setupAdvancedTab(tabs);

    mainLayout->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (m_nameEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "Error", "VM name is required.");
            return;
        }
// Warn if no bootable device
        bool hasDisk = !m_extraDrives.empty() && std::any_of(m_extraDrives.begin(), m_extraDrives.end(),
                           [](const DiskDrive &d) { return d.driveType == 0 && !d.path.isEmpty(); });
        bool hasCDROM = std::any_of(m_extraDrives.begin(), m_extraDrives.end(),
                           [](const DiskDrive &d) { return d.driveType == 1 && !d.path.isEmpty(); });
        if (!hasDisk && !hasCDROM) {
            QMessageBox::StandardButton reply = QMessageBox::warning(this,
                "No Bootable Device",
                "No disk or CDROM configured. The VM may not boot.\n"
                "Are you sure you want to continue?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply != QMessageBox::Yes) return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_archCombo, &QComboBox::currentIndexChanged, this, &VMCreatorDialog::onArchChanged);
    connect(m_osTypeCombo, &QComboBox::currentTextChanged, this, &VMCreatorDialog::onOsTypeChanged);
    connect(m_kernelBrowseBtn, &QPushButton::clicked, this, &VMCreatorDialog::browseKernel);
    connect(m_initrdBrowseBtn, &QPushButton::clicked, this, &VMCreatorDialog::browseInitrd);
    connect(m_displayModeCombo, &QComboBox::currentTextChanged, this, &VMCreatorDialog::updatePortDisplay);
    connect(m_addDriveBtn, &QPushButton::clicked, this, &VMCreatorDialog::onAddExtraDrive);
    connect(m_scanDirBtn, &QPushButton::clicked, this, &VMCreatorDialog::onScanDirectory);
    connect(m_removeDriveBtn, &QPushButton::clicked, this, &VMCreatorDialog::onRemoveExtraDrive);
    connect(m_addPortBtn, &QPushButton::clicked, this, &VMCreatorDialog::onAddPortForward);
    connect(m_removePortBtn, &QPushButton::clicked, this, &VMCreatorDialog::onRemovePortForward);
    connect(m_addUsbBtn, &QPushButton::clicked, this, &VMCreatorDialog::onAddUSBDevice);
    connect(m_removeUsbBtn, &QPushButton::clicked, this, &VMCreatorDialog::onRemoveUSBDevice);
    connect(m_addShareBtn, &QPushButton::clicked, this, &VMCreatorDialog::onAddSharedDir);
    connect(m_removeShareBtn, &QPushButton::clicked, this, &VMCreatorDialog::onRemoveSharedDir);
    connect(m_bootOrderUp, &QPushButton::clicked, this, &VMCreatorDialog::onBootOrderUp);
    connect(m_bootOrderDown, &QPushButton::clicked, this, &VMCreatorDialog::onBootOrderDown);
    connect(m_bootOrderRemove, &QPushButton::clicked, this, &VMCreatorDialog::onBootOrderRemove);

    onArchChanged();
    updatePortDisplay();
}

// ============== TAB SETUP ==============

static QWidget *makeScrollPage(QWidget *content) {
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    auto *page = new QWidget;
    auto *l = new QVBoxLayout(page);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(scroll);
    return page;
}

void VMCreatorDialog::setupSystemTab(QTabWidget *tabs) {
    auto *w = new QWidget;
    auto *l = new QFormLayout(w);
    l->setContentsMargins(16, 12, 16, 12);
    l->setSpacing(8);

    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText("My Virtual Machine");
    l->addRow("Name:", m_nameEdit);

    auto *iconRow = new QHBoxLayout;
    m_iconLabel = new QLabel;
    m_iconLabel->setFixedSize(40, 40);
    m_iconLabel->setScaledContents(true);
    m_iconName = "linux";
    updateIconPreview();
    iconRow->addWidget(m_iconLabel);
    auto *changeIconBtn = new QPushButton("Change Icon...");
    iconRow->addWidget(changeIconBtn);
    iconRow->addStretch();
    connect(changeIconBtn, &QPushButton::clicked, this, &VMCreatorDialog::onPickIcon);
    l->addRow("Icon:", iconRow);

    m_archCombo = new QComboBox;
    m_archCombo->setMinimumWidth(160);
    for (const auto &a : VMConfig::supportedArchs())
        m_archCombo->addItem(a.label, a.arch);
    l->addRow("Architecture:", m_archCombo);

    m_osTypeCombo = new QComboBox;
    m_osTypeCombo->addItems({"linux", "windows", "macos", "freebsd", "openbsd", "other"});
    l->addRow("OS Type:", m_osTypeCombo);

    m_productKeyEdit = new QLineEdit;
    m_productKeyEdit->setPlaceholderText("Product key (optional, for Windows)");
    m_productKeyEdit->setVisible(false);
    l->addRow("Product Key:", m_productKeyEdit);

    m_machineCombo = new QComboBox;
    l->addRow("Machine:", m_machineCombo);

    m_cpuModelCombo = new QComboBox;
    l->addRow("CPU Model:", m_cpuModelCombo);

    auto *cpuGroup = new QGroupBox("CPU Topology");
    auto *cpuGrid = new QHBoxLayout(cpuGroup);
    cpuGrid->setContentsMargins(12, 16, 12, 12);
    cpuGrid->setSpacing(8);
    auto makeCpuSpin = [](int min, int max, int val, const QString &suffix) {
        auto *s = new QSpinBox;
        s->setRange(min, max);
        s->setValue(val);
        s->setSuffix(suffix);
        s->setFixedWidth(100);
        return s;
    };
    m_cpuCoresSpin = makeCpuSpin(1, 128, 2, " cores");
    m_cpuSocketsSpin = makeCpuSpin(1, 16, 1, " sockets");
    m_cpuThreadsSpin = makeCpuSpin(1, 8, 1, " threads");
    cpuGrid->addWidget(m_cpuCoresSpin);
    cpuGrid->addWidget(m_cpuSocketsSpin);
    cpuGrid->addWidget(m_cpuThreadsSpin);
    cpuGrid->addStretch();
    l->addRow(cpuGroup);

    m_memorySpin = new QSpinBox;
    m_memorySpin->setRange(128, 1048576);
    m_memorySpin->setValue(2048);
    m_memorySpin->setSuffix(" MB");
    m_memorySpin->setSingleStep(256);
    m_memorySpin->setFixedWidth(140);
    l->addRow("Memory:", m_memorySpin);

    l->addItem(new QSpacerItem(0, 4));

    m_efiCheck = new QCheckBox("Enable UEFI firmware");
    m_efiCheck->setChecked(true);
    l->addRow("", m_efiCheck);

    m_kvmCheck = new QCheckBox("Enable KVM acceleration");
    m_kvmCheck->setChecked(true);
    l->addRow("", m_kvmCheck);

    m_tpmCheck = new QCheckBox("Enable TPM device (for Windows 11)");
    l->addRow("", m_tpmCheck);

    tabs->addTab(makeScrollPage(w), "System");
}

void VMCreatorDialog::setupDisplayTab(QTabWidget *tabs) {
    auto *w = new QWidget;
    auto *l = new QFormLayout(w);
    l->setContentsMargins(16, 12, 16, 12);
    l->setSpacing(8);

    m_displayModeCombo = new QComboBox;
    m_displayModeCombo->addItems({"vnc", "spice", "gtk", "sdl", "none"});
    m_displayModeCombo->setMinimumWidth(120);
    l->addRow("Display Mode:", m_displayModeCombo);

    m_vgaModelCombo = new QComboBox;
    l->addRow("Graphics Model:", m_vgaModelCombo);

    m_vgaQxlCheck = new QCheckBox("Paravirtual graphics (QXL)");
    l->addRow("", m_vgaQxlCheck);

    auto *vncRow = new QHBoxLayout;
    auto *vncLabel = new QLabel("VNC Port:");
    vncLabel->setFixedWidth(80);
    m_vncPortSpin = new QSpinBox;
    m_vncPortSpin->setRange(5900, 5999);
    m_vncPortSpin->setValue(5900);
    m_vncPortSpin->setFixedWidth(80);
    vncRow->addWidget(vncLabel);
    vncRow->addWidget(m_vncPortSpin);
    vncRow->addStretch();
    l->addRow(vncRow);

    auto *vncPwRow = new QHBoxLayout;

    l->addItem(new QSpacerItem(0, 4));

    m_vga3dCheck = new QCheckBox("Enable 3D acceleration");
    l->addRow("", m_vga3dCheck);

    m_usbTabletCheck = new QCheckBox("USB tablet (improves mouse accuracy)");
    m_usbTabletCheck->setChecked(true);
    l->addRow("", m_usbTabletCheck);

    tabs->addTab(makeScrollPage(w), "Display");
}

void VMCreatorDialog::setupStorageTab(QTabWidget *tabs) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(16, 12, 16, 12);
    l->setSpacing(12);

    // ===== Storage Drives Table =====
    auto *storageGroup = new QGroupBox("Storage Drives");
    auto *sg = new QVBoxLayout(storageGroup);
    sg->setContentsMargins(12, 16, 12, 12);
    sg->setSpacing(8);

    auto *btnRow = new QHBoxLayout;
    m_addDriveBtn = new QPushButton("+ Add Drive");
    m_addDriveBtn->setToolTip("Add a hard disk, CD/DVD, or floppy drive");
    m_createDiskBtn = new QPushButton("+ Create Disk");
    m_createDiskBtn->setToolTip("Create a new empty qcow2 disk image");
    m_scanDirBtn = new QPushButton("Scan Directory...");
    m_scanDirBtn->setToolTip("Scan a folder for disk images and add them all");
    m_removeDriveBtn = new QPushButton("- Remove Selected");
    m_removeDriveBtn->setToolTip("Remove the selected drive");
    btnRow->addWidget(m_addDriveBtn);
    btnRow->addWidget(m_createDiskBtn);
    btnRow->addWidget(m_scanDirBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_removeDriveBtn);
    sg->addLayout(btnRow);

    m_extraDrivesTable = new QTableWidget(0, 6);
    m_extraDrivesTable->setHorizontalHeaderLabels({"Type", "Path", "Controller", "Format", "Size", "Boot"});
    m_extraDrivesTable->horizontalHeader()->setStretchLastSection(true);
    m_extraDrivesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_extraDrivesTable->setAlternatingRowColors(true);
    m_extraDrivesTable->setEditTriggers(QAbstractItemView::DoubleClicked);
    m_extraDrivesTable->setMinimumHeight(200);
    sg->addWidget(m_extraDrivesTable);

    auto *hint = new QLabel("Tip: Double-click a row to change its type, controller, or format. Use 'Create Disk' to make a new empty image.");
    hint->setStyleSheet("font-size: 11px; color: gray;");
    sg->addWidget(hint);

    l->addWidget(storageGroup);
    tabs->addTab(makeScrollPage(w), "Storage");
}

void VMCreatorDialog::setupNetworkTab(QTabWidget *tabs) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(16, 12, 16, 12);
    l->setSpacing(8);

    auto *netGroup = new QGroupBox("Network Adapter");
    auto *ng = new QFormLayout(netGroup);
    ng->setContentsMargins(12, 16, 12, 12);
    ng->setSpacing(8);

    m_networkModeCombo = new QComboBox;
    m_networkModeCombo->addItems({"user", "bridge", "tap"});
    m_networkModeCombo->setMinimumWidth(120);
    ng->addRow("Mode:", m_networkModeCombo);

    m_networkModelCombo = new QComboBox;
    ng->addRow("Model:", m_networkModelCombo);

    auto *note = new QLabel("User mode: NAT (no setup). Bridge: requires bridge interface.");
    note->setWordWrap(true);
    note->setStyleSheet("font-size: 11px; color: gray;");
    ng->addRow("", note);

    l->addWidget(netGroup);

    auto *pfGroup = new QGroupBox("Port Forwarding (User mode)");
    auto *pfg = new QVBoxLayout(pfGroup);
    pfg->setContentsMargins(12, 16, 12, 12);
    pfg->setSpacing(6);

    auto *btnRow = new QHBoxLayout;
    m_addPortBtn = new QPushButton("Add Rule");
    m_removePortBtn = new QPushButton("Remove Selected");
    btnRow->addWidget(m_addPortBtn);
    btnRow->addWidget(m_removePortBtn);
    btnRow->addStretch();
    pfg->addLayout(btnRow);

    m_portForwardTable = new QTableWidget(0, 4);
    m_portForwardTable->setHorizontalHeaderLabels({"Protocol", "Host Port", "Guest Port", "Enabled"});
    m_portForwardTable->horizontalHeader()->setStretchLastSection(true);
    m_portForwardTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_portForwardTable->setAlternatingRowColors(true);
    m_portForwardTable->setMinimumHeight(100);
    pfg->addWidget(m_portForwardTable);

    l->addWidget(pfGroup);
    tabs->addTab(makeScrollPage(w), "Network");
}

void VMCreatorDialog::setupSoundTab(QTabWidget *tabs) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(16, 12, 16, 12);
    l->setSpacing(12);

    auto *group = new QGroupBox("Audio Configuration");
    auto *gl = new QFormLayout(group);
    gl->setContentsMargins(12, 16, 12, 12);
    gl->setSpacing(8);

    m_audioCheck = new QCheckBox("Enable audio");
    m_audioCheck->setChecked(true);
    gl->addRow("", m_audioCheck);

    m_soundModelCombo = new QComboBox;
    m_soundModelCombo->addItems({"hda", "ac97", "es1370", "sb16"});
    m_soundModelCombo->setMinimumWidth(120);
    gl->addRow("Sound Model:", m_soundModelCombo);

    l->addWidget(group);
    l->addStretch();

    tabs->addTab(makeScrollPage(w), "Sound");
}

void VMCreatorDialog::setupSharingTab(QTabWidget *tabs) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(16, 12, 16, 12);
    l->setSpacing(8);

    auto *shareGroup = new QGroupBox("Shared Directories (9p)");
    auto *sg = new QVBoxLayout(shareGroup);
    sg->setContentsMargins(12, 16, 12, 12);
    sg->setSpacing(6);

    auto *btnRow = new QHBoxLayout;
    m_addShareBtn = new QPushButton("Add Directory");
    m_removeShareBtn = new QPushButton("Remove Selected");
    btnRow->addWidget(m_addShareBtn);
    btnRow->addWidget(m_removeShareBtn);
    btnRow->addStretch();
    sg->addLayout(btnRow);

    m_sharedDirTable = new QTableWidget(0, 4);
    m_sharedDirTable->setHorizontalHeaderLabels({"Host Path", "Mount Tag", "Read Only", "Enabled"});
    m_sharedDirTable->horizontalHeader()->setStretchLastSection(true);
    m_sharedDirTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sharedDirTable->setAlternatingRowColors(true);
    m_sharedDirTable->setMinimumHeight(100);
    sg->addWidget(m_sharedDirTable);

    l->addWidget(shareGroup);

    m_clipboardCheck = new QCheckBox("Enable clipboard sharing (SPICE)");
    l->addWidget(m_clipboardCheck);

    l->addStretch();

    tabs->addTab(makeScrollPage(w), "Sharing");
}

void VMCreatorDialog::setupUSBTab(QTabWidget *tabs) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(16, 12, 16, 12);
    l->setSpacing(8);

    auto *group = new QGroupBox("USB Passthrough");
    auto *gl = new QVBoxLayout(group);
    gl->setContentsMargins(12, 16, 12, 12);
    gl->setSpacing(6);

    auto *desc = new QLabel("Pass through USB devices from host to guest.\n"
                            "Find vendor/product IDs with: lsusb");
    desc->setWordWrap(true);
    desc->setStyleSheet("font-size: 11px; color: gray;");
    gl->addWidget(desc);

    auto *btnRow = new QHBoxLayout;
    m_addUsbBtn = new QPushButton("Add USB Device");
    m_removeUsbBtn = new QPushButton("Remove Selected");
    btnRow->addWidget(m_addUsbBtn);
    btnRow->addWidget(m_removeUsbBtn);
    btnRow->addStretch();
    gl->addLayout(btnRow);

    m_usbTable = new QTableWidget(0, 4);
    m_usbTable->setHorizontalHeaderLabels({"Vendor ID", "Product ID", "Description", "Enabled"});
    m_usbTable->horizontalHeader()->setStretchLastSection(true);
    m_usbTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_usbTable->setAlternatingRowColors(true);
    m_usbTable->setMinimumHeight(100);
    gl->addWidget(m_usbTable);

    l->addWidget(group);
    l->addStretch();

    tabs->addTab(makeScrollPage(w), "USB");
}

void VMCreatorDialog::setupBootTab(QTabWidget *tabs) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(16, 12, 16, 12);
    l->setSpacing(12);

    auto *bootGroup = new QGroupBox("Boot Configuration");
    auto *bg = new QFormLayout(bootGroup);
    bg->setContentsMargins(12, 16, 12, 12);
    bg->setSpacing(8);

    auto *bootListRow = new QHBoxLayout;
    bootListRow->setSpacing(8);

    m_bootOrderList = new QListWidget;
    m_bootOrderList->setMinimumWidth(200);
    m_bootOrderList->setMaximumWidth(250);
    m_bootOrderList->addItems({"disk", "cdrom"});
    bootListRow->addWidget(m_bootOrderList, 1);

    auto *bootBtnCol = new QVBoxLayout;
    bootBtnCol->setSpacing(4);
    m_bootOrderUp = new QPushButton("▲");
    m_bootOrderUp->setFixedWidth(36);
    m_bootOrderUp->setFixedHeight(28);
    m_bootOrderUp->setToolTip("Move up");
    m_bootOrderDown = new QPushButton("▼");
    m_bootOrderDown->setFixedWidth(36);
    m_bootOrderDown->setFixedHeight(28);
    m_bootOrderDown->setToolTip("Move down");
    m_bootOrderRemove = new QPushButton("✕");
    m_bootOrderRemove->setFixedWidth(36);
    m_bootOrderRemove->setFixedHeight(28);
    m_bootOrderRemove->setToolTip("Remove");
    bootBtnCol->addWidget(m_bootOrderUp);
    bootBtnCol->addWidget(m_bootOrderDown);
    bootBtnCol->addWidget(m_bootOrderRemove);
    bootBtnCol->addStretch();
    bootListRow->addLayout(bootBtnCol);

    bg->addRow("Boot Order:", bootListRow);

    auto *addBootRow = new QHBoxLayout;
    m_bootOrderAdd = new QComboBox;
    m_bootOrderAdd->addItems({"disk", "cdrom", "floppy", "network", "usb"});
    m_bootOrderAdd->setMinimumWidth(120);
    auto *addBootBtn = new QPushButton("Add");
    addBootBtn->setFixedWidth(60);
    addBootRow->addWidget(m_bootOrderAdd);
    addBootRow->addWidget(addBootBtn);
    bg->addRow("", addBootRow);

    l->addWidget(bootGroup);

    auto *kernelGroup = new QGroupBox("Direct Kernel Boot");
    auto *kg = new QFormLayout(kernelGroup);
    kg->setContentsMargins(12, 16, 12, 12);
    kg->setSpacing(8);

    auto *kr = new QHBoxLayout;
    m_kernelPathEdit = new QLineEdit;
    m_kernelBrowseBtn = new QPushButton("Browse...");
    kr->addWidget(m_kernelPathEdit, 1);
    kr->addWidget(m_kernelBrowseBtn);
    kg->addRow("Kernel:", kr);

    auto *ir = new QHBoxLayout;
    m_initrdPathEdit = new QLineEdit;
    m_initrdBrowseBtn = new QPushButton("Browse...");
    ir->addWidget(m_initrdPathEdit, 1);
    ir->addWidget(m_initrdBrowseBtn);
    kg->addRow("Initrd:", ir);

    m_kernelCmdlineEdit = new QLineEdit;
    m_kernelCmdlineEdit->setPlaceholderText("console=ttyS0 root=/dev/sda1");
    kg->addRow("Cmdline:", m_kernelCmdlineEdit);

    l->addWidget(kernelGroup);
    l->addStretch();

    tabs->addTab(makeScrollPage(w), "Boot");
}

void VMCreatorDialog::setupAdvancedTab(QTabWidget *tabs) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(16, 12, 16, 12);
    l->setSpacing(12);

    auto *extraGroup = new QGroupBox("Extra QEMU Arguments");
    auto *eg = new QFormLayout(extraGroup);
    eg->setContentsMargins(12, 16, 12, 12);
    eg->setSpacing(8);

    m_extraArgsEdit = new QLineEdit;
    m_extraArgsEdit->setPlaceholderText("-device foo -object bar");
    eg->addRow("Extra Args:", m_extraArgsEdit);
    l->addWidget(extraGroup);

    auto *notesGroup = new QGroupBox("Notes");
    auto *ng = new QVBoxLayout(notesGroup);
    ng->setContentsMargins(12, 16, 12, 12);

    m_notesEdit = new QPlainTextEdit;
    m_notesEdit->setPlaceholderText("Notes about this VM...");
    m_notesEdit->setMaximumHeight(120);
    ng->addWidget(m_notesEdit);
    l->addWidget(notesGroup);

    l->addStretch();

    tabs->addTab(makeScrollPage(w), "Advanced");
}

// ============== IMPLEMENTATION ==============

void VMCreatorDialog::populateCombo(QComboBox *combo, const QStringList &items, const QString &current) {
    combo->blockSignals(true);
    combo->clear();
    for (const auto &s : items) combo->addItem(s, s);
    int idx = combo->findData(current);
    if (idx >= 0) combo->setCurrentIndex(idx);
    combo->blockSignals(false);
}

void VMCreatorDialog::populateLabeledCombo(QComboBox *combo,
    const QStringList &values, const QStringList &labels,
    const QString &current)
{
    combo->blockSignals(true);
    combo->clear();
    for (int i = 0; i < values.size(); i++) {
        QString label = (i < labels.size()) ? labels[i] : values[i];
        combo->addItem(label, values[i]);
    }
    int idx = combo->findData(current);
    if (idx >= 0) combo->setCurrentIndex(idx);
    combo->blockSignals(false);
}

void VMCreatorDialog::onOsTypeChanged(const QString &type) {
    m_productKeyEdit->setVisible(type == "windows");
    if (type == "windows") {
        bool changed = false;
        int idx = m_archCombo->findData("i386");
        if (idx >= 0) { m_archCombo->setCurrentIndex(idx); changed = true; }
        else {
            idx = m_archCombo->findData("x86_64");
            if (idx >= 0) { m_archCombo->setCurrentIndex(idx); changed = true; }
        }
        m_efiCheck->setChecked(false);
        m_tpmCheck->setChecked(false);
        // onArchChanged fires synchronously; now adjust after it
        int mi = m_machineCombo->findData("pc");
        if (mi >= 0) m_machineCombo->setCurrentIndex(mi);
        int ci = m_cpuModelCombo->findData("pentium3");
        if (ci >= 0) m_cpuModelCombo->setCurrentIndex(ci);
        else {
            ci = m_cpuModelCombo->findData("coreduo");
            if (ci >= 0) m_cpuModelCombo->setCurrentIndex(ci);
        }
    } else if (type == "macos") {
        int idx = m_archCombo->findData("aarch64");
        if (idx >= 0) m_archCombo->setCurrentIndex(idx);
        m_efiCheck->setChecked(true);
    } else {
        int idx = m_archCombo->findData("x86_64");
        if (idx >= 0) m_archCombo->setCurrentIndex(idx);
        m_efiCheck->setChecked(true);
    }
}

void VMCreatorDialog::onArchChanged() {
    QString arch = m_archCombo->currentData().toString();
    ArchInfo info = VMConfig::archInfo(arch);

    populateLabeledCombo(m_machineCombo, info.machines, info.machineLabels, info.defaultMachine);
    for (int i = 0; i < m_machineCombo->count(); i++)
        m_machineCombo->setItemData(i, machineTooltip(m_machineCombo->itemData(i).toString()), Qt::ToolTipRole);

    populateCombo(m_networkModelCombo,
        {info.defaultNetModel, "e1000", "e1000e", "rtl8139"}, info.defaultNetModel);
    populateCombo(m_vgaModelCombo, info.displayModels, info.defaultDisplay);

    m_soundModelCombo->clear();
    m_soundModelCombo->addItems(info.soundModels.isEmpty()
        ? QStringList{"hda"} : info.soundModels);

    m_efiCheck->setEnabled(info.supportsEfi);
    m_efiCheck->setChecked(info.supportsEfi);
    m_efiCheck->setText(info.supportsEfi ? "Enable UEFI firmware" : "UEFI not available");

    m_kvmCheck->setEnabled(info.supportsKvm);
    m_kvmCheck->setChecked(info.supportsKvm);
    m_kvmCheck->setText(info.supportsKvm ? "Enable KVM acceleration" : "KVM not available");

    m_tpmCheck->setEnabled(arch == "x86_64");

    // Query real CPU models from QEMU
    QStringList cpus = queryQemuCPUs(info.binary);
    m_cpuModelCombo->blockSignals(true);
    m_cpuModelCombo->clear();
    m_cpuModelCombo->setEditable(true);
    m_cpuModelCombo->setInsertPolicy(QComboBox::NoInsert);
    for (const QString &cpu : cpus)
        m_cpuModelCombo->addItem(cpu, cpu);
    int idx = m_cpuModelCombo->findData(info.defaultCpu);
    if (idx >= 0) m_cpuModelCombo->setCurrentIndex(idx);
    else m_cpuModelCombo->setCurrentText(info.defaultCpu);
    if (m_cpuModelCombo->completer())
        m_cpuModelCombo->completer()->setFilterMode(Qt::MatchContains);
    m_cpuModelCombo->blockSignals(false);
}

QStringList VMCreatorDialog::queryQemuCPUs(const QString &binary) {
    if (m_cpuCache.contains(binary))
        return m_cpuCache[binary];

    QStringList result;
    if (!QFileInfo::exists(binary)) {
        qWarning("QEMU binary not found: %s", qPrintable(binary));
        m_cpuCache[binary] = result;
        return result;
    }
    QProcess proc;
    proc.start(binary, {"-cpu", "help"});
    if (!proc.waitForStarted(2000) || !proc.waitForFinished(4000)) {
        qWarning("Failed to query %s -cpu help", qPrintable(binary));
        m_cpuCache[binary] = result;
        return result;
    }

    QString output = QString::fromUtf8(proc.readAllStandardOutput());
    for (const QString &line : output.split('\n')) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith("Available") || trimmed.startsWith("Recognized"))
            continue;
        int space = trimmed.indexOf(' ');
        if (space > 0) {
            QString name = trimmed.left(space).trimmed();
            if (!name.isEmpty() && name[0].isLetter())
                result.append(name);
        }
    }
    result.removeDuplicates();
    result.sort();
    m_cpuCache[binary] = result;
    return result;
}

QString VMCreatorDialog::machineTooltip(const QString &machine) {
    if (machine == "q35") return "Modern ICH9 chipset (recommended for most OS)";
    if (machine == "pc") return "Legacy i440FX chipset (compatible with older OS)";
    if (machine == "pc-i440fx-2.1") return "Very old i440FX chipset (pre-2014)";
    if (machine == "virt") return "Generic virtual platform (recommended for ARM/RISC-V)";
    if (machine == "raspi3b") return "Raspberry Pi 3 Model B";
    if (machine == "raspi4b") return "Raspberry Pi 4 Model B";
    if (machine == "raspi2b") return "Raspberry Pi 2 Model B";
    if (machine == "sbsa-ref") return "Server Base System Architecture reference platform";
    if (machine == "q800") return "Apple Macintosh Quadra 800 (68k Mac)";
    if (machine == "an5206") return "Arnewsh 5206 evaluation board";
    if (machine == "mcf5208evb") return "MCF5208EVB ColdFire evaluation board";
    if (machine == "pseries") return "IBM pSeries logical partition (PowerVM)";
    if (machine == "mac99") return "Apple PowerMac G5 (ppc64)";
    if (machine == "g3beige") return "Apple PowerMac G3 Beige (ppc)";
    if (machine == "malta") return "MIPS Malta platform (reference board)";
    if (machine == "fulong2e") return "Fulong 2E mini-PC (MIPS)";
    if (machine == "boston") return "MIPS Boston development board";
    if (machine == "sifive_u") return "SiFive Unleashed (RISC-V)";
    if (machine == "spike_v1.10") return "Spike RISC-V ISA simulator";
    if (machine == "sun4u") return "Sun UltraSPARC workstation (Enterprise)";
    if (machine == "sun4v") return "Sun SPARC virtualized platform";
    if (machine == "niagara") return "Sun Niagara (UltraSPARC T1)";
    if (machine == "realview-pbx-a9") return "ARM RealView PBX-A9 evaluation board";
    if (machine == "verdex") return "Gumstix Verdex PXA270 (ARM)";
    if (machine == "la32") return "Loongson 3A4000 (LoongArch)";
    return "QEMU virtual machine type";
}

void VMCreatorDialog::loadFromConfig(const VMConfig &cfg) {
    m_nameEdit->setText(cfg.name);
    for (int i = 0; i < m_archCombo->count(); i++) {
        if (m_archCombo->itemData(i).toString() == cfg.arch) {
            m_archCombo->setCurrentIndex(i);
            break;
        }
    }
    m_osTypeCombo->setCurrentText(cfg.osType);
    m_productKeyEdit->setText(cfg.productKey);
    m_productKeyEdit->setVisible(cfg.osType == "windows");
    {
        int idx = m_machineCombo->findData(cfg.machine);
        if (idx >= 0) m_machineCombo->setCurrentIndex(idx);
    }
    {
        int idx = m_cpuModelCombo->findData(cfg.cpuModel);
        if (idx >= 0) m_cpuModelCombo->setCurrentIndex(idx);
        else m_cpuModelCombo->setCurrentText(cfg.cpuModel);
    }
    m_iconName = cfg.iconName.isEmpty() ? VMConfig::defaultIconForOs(cfg.osType) : cfg.iconName;
    updateIconPreview();
    m_cpuCoresSpin->setValue(cfg.cpuCores);
    m_cpuSocketsSpin->setValue(cfg.cpuSockets);
    m_cpuThreadsSpin->setValue(cfg.cpuThreads);
    m_memorySpin->setValue(cfg.memoryMB);
    m_efiCheck->setChecked(cfg.efiBoot);
    m_kvmCheck->setChecked(cfg.kvmEnabled);
    m_tpmCheck->setChecked(cfg.tpmEnabled);

    m_displayModeCombo->setCurrentText(cfg.displayMode);
    {
        int idx = m_vgaModelCombo->findData(cfg.vgaModel);
        if (idx >= 0) m_vgaModelCombo->setCurrentIndex(idx);
    }
    m_vncPortSpin->setValue(cfg.vncPort);
    m_vga3dCheck->setChecked(cfg.vga3d);
    m_vgaQxlCheck->setChecked(cfg.vgaQxl);
    m_usbTabletCheck->setChecked(cfg.usbTablet);

    // Populate drives table: primary disk as first row, then extra drives
    m_extraDrives.clear();
    if (!cfg.disk.path.isEmpty()) {
        DiskDrive primary;
        primary.path = cfg.disk.path;
        primary.sizeMB = cfg.disk.sizeMB;
        primary.format = cfg.disk.format;
        primary.controller = cfg.disk.controller;
        primary.cache = cfg.disk.cache;
        primary.driveType = 0;
        primary.bootable = true;
        m_extraDrives.append(primary);
    }
    m_extraDrives.append(cfg.extraDrives);
    refreshExtraDrivesTable();

    m_networkModeCombo->setCurrentText(cfg.network.mode);
    m_networkModelCombo->setCurrentText(cfg.network.model);
    m_portForwards = cfg.portForwards;
    refreshPortForwardTable();

    m_audioCheck->setChecked(cfg.audioEnabled);
    m_soundModelCombo->setCurrentText(cfg.soundModel);

    m_sharedDirs = cfg.sharedDirs;
    refreshSharedDirTable();
    m_clipboardCheck->setChecked(cfg.clipboardShare);

    m_usbDevices = cfg.usbDevices;
    refreshUSBTable();

    for (int i = 0; i < m_bootOrderList->count(); ++i) {
        if (m_bootOrderList->item(i)->text() == cfg.bootOrder) {
            m_bootOrderList->setCurrentRow(i);
            break;
        }
    }
    m_kernelPathEdit->setText(cfg.kernelPath);
    m_initrdPathEdit->setText(cfg.initrdPath);
    m_kernelCmdlineEdit->setText(cfg.kernelCmdline);

    m_extraArgsEdit->setText(cfg.extraArgs);
    m_notesEdit->setPlainText(cfg.notes);
}

// ============== EXTRA DRIVES ==============

void VMCreatorDialog::onScanDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this, "Scan for Disk Images",
        "/", QFileDialog::ShowDirsOnly);
    if (dir.isEmpty()) return;

    QStringList exts = { "*.qcow2", "*.qcow", "*.raw", "*.img", "*.iso",
                         "*.vmdk", "*.vdi", "*.qed" };
    QStringList entries =QDir(dir).entryList(exts,QDir::Files,QDir::Name);
    if (entries.isEmpty()) {
        QMessageBox::information(this, "Scan Complete",
            "No disk images found in the selected directory.");
        return;
    }

    for (const auto &fn : entries) {
        DiskDrive dd;
        dd.path =QDir(dir).absoluteFilePath(fn);
        dd.format = "qcow2";
        dd.controller = "virtio";
        dd.driveType = fn.endsWith(".iso", Qt::CaseInsensitive) ? 1 : 0;
        m_extraDrives.append(dd);
    }
    refreshExtraDrivesTable();
}

void VMCreatorDialog::onAddExtraDrive() {
    QDialog dlg(this);
    dlg.setWindowTitle("Add Drive");
    dlg.setMinimumWidth(400);

    auto *form = new QFormLayout(&dlg);
    form->setSpacing(8);

    auto *typeCombo = new QComboBox;
    typeCombo->addItems({"Hard Disk", "CD/DVD", "Floppy"});
    form->addRow("Drive Type:", typeCombo);

    auto *pathRow = new QHBoxLayout;
    auto *pathEdit = new QLineEdit;
    pathEdit->setPlaceholderText("Leave empty to create new");
    auto *browseBtn = new QPushButton("Browse...");
    pathRow->addWidget(pathEdit, 1);
    pathRow->addWidget(browseBtn);
    form->addRow("Image File:", pathRow);

    auto *ctrlCombo = new QComboBox;
    ctrlCombo->addItems({"virtio", "ide", "ahci", "sata", "scsi", "nvme"});
    ctrlCombo->setCurrentText("virtio");
    form->addRow("Controller:", ctrlCombo);

    auto *fmtCombo = new QComboBox;
    fmtCombo->addItems({"qcow2", "raw", "qed", "vmdk", "vdi", "vhd", "vhdx"});
    fmtCombo->setCurrentText("qcow2");
    form->addRow("Format:", fmtCombo);

    auto *sizeSpin = new QSpinBox;
    sizeSpin->setRange(1, 1048576);
    sizeSpin->setValue(20480);
    sizeSpin->setSuffix(" MB");
    sizeSpin->setSingleStep(1024);
    form->addRow("Size:", sizeSpin);

    auto *bootCheck = new QCheckBox("Bootable");
    bootCheck->setChecked(true);
    form->addRow("", bootCheck);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(btns);

    connect(browseBtn, &QPushButton::clicked, [&]() {
        QString p = QFileDialog::getOpenFileName(&dlg, "Select Image",
            "/", "All Files (*.qcow2 *.raw *.img *.iso *.vmdk *.vdi *.vhd *.qed)");
        if (!p.isEmpty()) pathEdit->setText(p);
    });

    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    DiskDrive dd;
    dd.driveType = typeCombo->currentIndex();
    dd.controller = ctrlCombo->currentText();
    dd.format = fmtCombo->currentText();
    dd.sizeMB = sizeSpin->value();
    dd.bootable = bootCheck->isChecked();

    QString imgPath = pathEdit->text().trimmed();
    if (imgPath.isEmpty()) {
        imgPath = QFileDialog::getSaveFileName(&dlg, "Create Disk Image",
            "/", "QCOW2 (*.qcow2)");
        if (imgPath.isEmpty()) return;
        if (!imgPath.endsWith(".qcow2", Qt::CaseInsensitive))
            imgPath += ".qcow2";

        QProcess proc;
        proc.start("qemu-img", {"create", "-f", "qcow2", imgPath,
                                QString::number(dd.sizeMB) + "M"});
        if (!proc.waitForFinished(30000) || proc.exitCode() != 0) {
            QMessageBox::warning(&dlg, "Error",
                "Failed to create disk:\n"
                + QString::fromUtf8(proc.readAllStandardError()));
            return;
        }
        dd.path = imgPath;
    } else {
        dd.path = convertDiskImage(&dlg, imgPath);
        if (dd.path.endsWith(".iso", Qt::CaseInsensitive)) {
            dd.driveType = 1;
            dd.format = "raw";
        }
    }

    m_extraDrives.append(dd);
    refreshExtraDrivesTable();
}

void VMCreatorDialog::onRemoveExtraDrive() {
    int row = m_extraDrivesTable->currentRow();
    if (row >= 0 && row < m_extraDrives.size()) {
        m_extraDrives.removeAt(row);
        refreshExtraDrivesTable();
    }
}

void VMCreatorDialog::refreshExtraDrivesTable() {
    m_extraDrivesTable->setRowCount(m_extraDrives.size());
    for (int i = 0; i < m_extraDrives.size(); i++) {
        const auto &d = m_extraDrives[i];
        m_extraDrivesTable->setItem(i, 0, new QTableWidgetItem(
            d.driveType == 1 ? "CD/DVD" : (d.driveType == 2 ? "Floppy" : "Hard Disk")));
        m_extraDrivesTable->setItem(i, 1, new QTableWidgetItem(d.path));
        m_extraDrivesTable->setItem(i, 2, new QTableWidgetItem(d.controller));
        m_extraDrivesTable->setItem(i, 3, new QTableWidgetItem(d.format));
        m_extraDrivesTable->setItem(i, 4, new QTableWidgetItem(
            d.driveType == 0 ? QString::number(d.sizeMB) + " MB" : ""));
        auto *boot = new QTableWidgetItem;
        boot->setCheckState(d.bootable ? Qt::Checked : Qt::Unchecked);
        m_extraDrivesTable->setItem(i, 5, boot);
    }
}

// ============== PORT FORWARDING ==============

void VMCreatorDialog::onAddPortForward() {
    PortForwardRule pf;
    bool ok;
    pf.hostPort = QInputDialog::getInt(this, "Host Port", "Host port number:", 8080, 1, 65535, 1, &ok);
    if (!ok) return;
    pf.guestPort = QInputDialog::getInt(this, "Guest Port", "Guest port number:", 80, 1, 65535, 1, &ok);
    if (!ok) return;
    m_portForwards.append(pf);
    refreshPortForwardTable();
}

void VMCreatorDialog::onRemovePortForward() {
    int row = m_portForwardTable->currentRow();
    if (row >= 0 && row < m_portForwards.size()) {
        m_portForwards.removeAt(row);
        refreshPortForwardTable();
    }
}

void VMCreatorDialog::refreshPortForwardTable() {
    m_portForwardTable->setRowCount(m_portForwards.size());
    for (int i = 0; i < m_portForwards.size(); i++) {
        const auto &pf = m_portForwards[i];
        m_portForwardTable->setItem(i, 0, new QTableWidgetItem(pf.protocol));
        m_portForwardTable->setItem(i, 1, new QTableWidgetItem(QString::number(pf.hostPort)));
        m_portForwardTable->setItem(i, 2, new QTableWidgetItem(QString::number(pf.guestPort)));
        auto *check = new QTableWidgetItem;
        check->setCheckState(pf.enabled ? Qt::Checked : Qt::Unchecked);
        m_portForwardTable->setItem(i, 3, check);
    }
}

// ============== USB DEVICES ==============

void VMCreatorDialog::onAddUSBDevice() {
    bool ok;
    QString vidStr = QInputDialog::getText(this, "Vendor ID",
        "Vendor ID (hex, e.g. 0781):", QLineEdit::Normal, "", &ok);
    if (!ok || vidStr.isEmpty()) return;

    QString pidStr = QInputDialog::getText(this, "Product ID",
        "Product ID (hex, e.g. 5583):", QLineEdit::Normal, "", &ok);
    if (!ok || pidStr.isEmpty()) return;

    USBPassthrough usb;
    usb.vendorId = vidStr.toUShort(nullptr, 16);
    usb.productId = pidStr.toUShort(nullptr, 16);
    usb.vendorName = vidStr;
    usb.productName = pidStr;
    usb.enabled = true;
    m_usbDevices.append(usb);
    refreshUSBTable();
}

void VMCreatorDialog::onRemoveUSBDevice() {
    int row = m_usbTable->currentRow();
    if (row >= 0 && row < m_usbDevices.size()) {
        m_usbDevices.removeAt(row);
        refreshUSBTable();
    }
}

void VMCreatorDialog::refreshUSBTable() {
    m_usbTable->setRowCount(m_usbDevices.size());
    for (int i = 0; i < m_usbDevices.size(); i++) {
        const auto &u = m_usbDevices[i];
        m_usbTable->setItem(i, 0,
            new QTableWidgetItem(QString("0x%1").arg(u.vendorId, 4, 16, QLatin1Char('0'))));
        m_usbTable->setItem(i, 1,
            new QTableWidgetItem(QString("0x%1").arg(u.productId, 4, 16, QLatin1Char('0'))));
        m_usbTable->setItem(i, 2, new QTableWidgetItem(u.vendorName + ":" + u.productName));
        auto *check = new QTableWidgetItem;
        check->setCheckState(u.enabled ? Qt::Checked : Qt::Unchecked);
        m_usbTable->setItem(i, 3, check);
    }
}

// ============== SHARED DIRECTORIES ==============

void VMCreatorDialog::onAddSharedDir() {
    QString path = QFileDialog::getExistingDirectory(this, "Select Directory to Share");
    if (path.isEmpty()) return;

    bool ok;
    QString tag = QInputDialog::getText(this, "Mount Tag",
        "Mount tag (used inside guest to mount):",
        QLineEdit::Normal, "shared", &ok);
    if (!ok || tag.isEmpty()) return;

    bool readonly = QMessageBox::question(this, "Read Only",
        "Make this share read-only?",
        QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;

    SharedDirectory sd;
    sd.hostPath = path;
    sd.mountTag = tag;
    sd.readonly = readonly;
    m_sharedDirs.append(sd);
    refreshSharedDirTable();
}

void VMCreatorDialog::onRemoveSharedDir() {
    int row = m_sharedDirTable->currentRow();
    if (row >= 0 && row < m_sharedDirs.size()) {
        m_sharedDirs.removeAt(row);
        refreshSharedDirTable();
    }
}

void VMCreatorDialog::refreshSharedDirTable() {
    m_sharedDirTable->setRowCount(m_sharedDirs.size());
    for (int i = 0; i < m_sharedDirs.size(); i++) {
        const auto &s = m_sharedDirs[i];
        m_sharedDirTable->setItem(i, 0, new QTableWidgetItem(s.hostPath));
        m_sharedDirTable->setItem(i, 1, new QTableWidgetItem(s.mountTag));
        m_sharedDirTable->setItem(i, 2, new QTableWidgetItem(s.readonly ? "Yes" : "No"));
        auto *check = new QTableWidgetItem;
        check->setCheckState(s.enabled ? Qt::Checked : Qt::Unchecked);
        m_sharedDirTable->setItem(i, 3, check);
    }
}

void VMCreatorDialog::onBootOrderUp() {
    int row = m_bootOrderList->currentRow();
    if (row <= 0) return;
    QListWidgetItem *item = m_bootOrderList->takeItem(row);
    m_bootOrderList->insertItem(row - 1, item);
    m_bootOrderList->setCurrentRow(row - 1);
}

void VMCreatorDialog::onBootOrderDown() {
    int row = m_bootOrderList->currentRow();
    if (row < 0 || row >= m_bootOrderList->count() - 1) return;
    QListWidgetItem *item = m_bootOrderList->takeItem(row);
    m_bootOrderList->insertItem(row + 1, item);
    m_bootOrderList->setCurrentRow(row + 1);
}

void VMCreatorDialog::onBootOrderRemove() {
    int row = m_bootOrderList->currentRow();
    if (row >= 0 && m_bootOrderList->count() > 1) {
        delete m_bootOrderList->takeItem(row);
    }
}

// ============== ICON PICKER ==============

void VMCreatorDialog::onPickIcon() {
    QDialog dlg(this);
    dlg.setWindowTitle("Choose VM Icon");
    dlg.setMinimumSize(480, 420);
    dlg.resize(560, 500);

    auto *layout = new QVBoxLayout(&dlg);
    auto *listWidget = new QListWidget;
    listWidget->setViewMode(QListView::IconMode);
    listWidget->setIconSize(QSize(40, 40));
    listWidget->setGridSize(QSize(90, 80));
    listWidget->setResizeMode(QListView::Adjust);
    listWidget->setWordWrap(true);
    listWidget->setSpacing(4);
    listWidget->setFlow(QListView::LeftToRight);
    listWidget->setWrapping(true);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    QStringList icons = VMConfig::availableIcons();
    int selectedRow = -1;
    for (int i = 0; i < icons.size(); i++) {
        QIcon icon(QString(":/icons/%1.png").arg(icons[i]));
        auto *item = new QListWidgetItem(icon, icons[i]);
        item->setToolTip(icons[i]);
        item->setTextAlignment(Qt::AlignCenter);
        listWidget->addItem(item);
        if (icons[i] == m_iconName) selectedRow = i;
    }
    if (selectedRow >= 0) listWidget->setCurrentRow(selectedRow);

    layout->addWidget(listWidget);

    auto *btnLayout = new QHBoxLayout;
    auto *okBtn = new QPushButton("Select");
    okBtn->setObjectName("actionBtn");
    auto *cancelBtn = new QPushButton("Cancel");
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(listWidget, &QListWidget::itemActivated, &dlg, &QDialog::accept);
    connect(listWidget, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);

    if (dlg.exec() == QDialog::Accepted) {
        auto *item = listWidget->currentItem();
        if (item) {
            m_iconName = item->text();
            updateIconPreview();
        }
    }
}

void VMCreatorDialog::updateIconPreview() {
    QPixmap pm(QString(":/icons/%1.png").arg(m_iconName));
    if (!pm.isNull())
        m_iconLabel->setPixmap(pm.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// ============== HELPERS ==============

void VMCreatorDialog::browseDiskPath() {
    QString path = QFileDialog::getSaveFileName(this, "Select Disk Image",
        QDir::homePath() + "/" + m_nameEdit->text().trimmed() + ".qcow2",
        "Disk Images (*.qcow2 *.raw *.qed *.vmdk *.vdi);;All Files (*)");
    if (path.isEmpty()) return;
    if (path.endsWith(".iso", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, "ISO Detected",
            "ISO files should be added as CD/DVD drives in 'Extra Drives',\n"
            "not as the primary disk. Please use 'Add Drive' below.");
        return;
    }
    path = convertDiskImage(this, path);
}

void VMCreatorDialog::browseKernel() {
    QString p = QFileDialog::getOpenFileName(this, "Select Kernel", QDir::homePath());
    if (!p.isEmpty()) m_kernelPathEdit->setText(p);
}

void VMCreatorDialog::browseInitrd() {
    QString p = QFileDialog::getOpenFileName(this, "Select Initrd", QDir::homePath());
    if (!p.isEmpty()) m_initrdPathEdit->setText(p);
}

void VMCreatorDialog::createDiskImage() {
    // Standalone disk creation — used by the "Create Disk" button in the storage tab.
    // Opens a dialog to create a new qcow2 image and adds it as a drive.
    QString imgPath = QFileDialog::getSaveFileName(this, "Create Disk Image",
        "/", "QCOW2 (*.qcow2)");
    if (imgPath.isEmpty()) return;
    if (!imgPath.endsWith(".qcow2", Qt::CaseInsensitive))
        imgPath += ".qcow2";

    bool ok = false;
    int sizeMB = QInputDialog::getInt(this, "Disk Size",
        "Enter disk size in MB (e.g. 8192 for 8 GB):",
        8192, 1, 1048576, 1024, &ok);
    if (!ok) return;

    QProcess proc;
    proc.start("qemu-img", {"create", "-f", "qcow2", imgPath, QString::number(sizeMB) + "M"});
    if (!proc.waitForFinished(30000) || proc.exitCode() != 0) {
        QMessageBox::warning(this, "Error",
            "qemu-img failed:\n" + proc.errorString() + "\n"
            + QString::fromUtf8(proc.readAllStandardError()));
        return;
    }

    DiskDrive dd;
    dd.path = imgPath;
    dd.sizeMB = sizeMB;
    dd.format = "qcow2";
    dd.controller = "virtio";
    dd.driveType = 0;
    dd.bootable = true;
    m_extraDrives.append(dd);
    refreshExtraDrivesTable();

    QMessageBox::information(this, "Done",
        "Disk created and added:\n" + imgPath);
}

void VMCreatorDialog::browseISOPath() {}

void VMCreatorDialog::updatePortDisplay() {
    m_vncPortSpin->setEnabled(m_displayModeCombo->currentText() == "vnc");
}

QString VMCreatorDialog::diskPath() const {
    for (const auto &d : m_extraDrives) {
        if (d.driveType == 0 && !d.path.isEmpty())
            return d.path;
    }
    return {};
}

bool VMCreatorDialog::createDisk() const {
    return false;  // Disk creation is handled inline in the storage tab
}

VMConfig VMCreatorDialog::config() const {
    VMConfig cfg;
    cfg.id = m_editing ? m_originalId : VMConfig::generateId();
    cfg.arch = m_archCombo->currentData().toString();
    cfg.name = m_nameEdit->text().trimmed();
    cfg.osType = m_osTypeCombo->currentText();
    cfg.machine = m_machineCombo->currentData().toString();
    {
        QVariant cpuData = m_cpuModelCombo->currentData();
        cfg.cpuModel = cpuData.isValid() ? cpuData.toString() : m_cpuModelCombo->currentText();
    }
    cfg.cpuCores = m_cpuCoresSpin->value();
    cfg.cpuSockets = m_cpuSocketsSpin->value();
    cfg.cpuThreads = m_cpuThreadsSpin->value();
    cfg.memoryMB = m_memorySpin->value();
    cfg.efiBoot = m_efiCheck->isChecked() && m_efiCheck->isEnabled();
    cfg.kvmEnabled = m_kvmCheck->isChecked() && m_kvmCheck->isEnabled();
    cfg.tpmEnabled = m_tpmCheck->isChecked();

    // Extract primary disk from drives table (first hard disk entry)
    cfg.disk.path.clear();
    if (!m_extraDrives.empty() && m_extraDrives[0].driveType == 0) {
        cfg.disk = m_extraDrives[0];
        cfg.extraDrives = m_extraDrives;
        cfg.extraDrives.remove(0);
    } else {
        cfg.extraDrives = m_extraDrives;
    }
    cfg.displayMode = m_displayModeCombo->currentText();
    cfg.vgaModel = m_vgaModelCombo->currentData().toString();
    cfg.vncPort = m_vncPortSpin->value();
    cfg.vga3d = m_vga3dCheck->isChecked();
    cfg.vgaQxl = m_vgaQxlCheck->isChecked();
    cfg.usbTablet = m_usbTabletCheck->isChecked();

    cfg.network.mode = m_networkModeCombo->currentText();
    cfg.network.model = m_networkModelCombo->currentText();
    cfg.portForwards = m_portForwards;

    cfg.audioEnabled = m_audioCheck->isChecked();
    cfg.soundModel = m_soundModelCombo->currentText();

    cfg.sharedDirs = m_sharedDirs;
    cfg.clipboardShare = m_clipboardCheck->isChecked();

    cfg.usbDevices = m_usbDevices;

    cfg.bootOrder = m_bootOrderList->currentItem() ? m_bootOrderList->currentItem()->text() : "disk";
    cfg.kernelPath = m_kernelPathEdit->text().trimmed();
    cfg.initrdPath = m_initrdPathEdit->text().trimmed();
    cfg.kernelCmdline = m_kernelCmdlineEdit->text().trimmed();

    cfg.iconName = m_iconName;
    cfg.extraArgs = m_extraArgsEdit->text().trimmed();
    cfg.notes = m_notesEdit->toPlainText().trimmed();
    cfg.productKey = m_productKeyEdit->text().trimmed();

    return cfg;
}

QString VMCreatorDialog::convertDiskImage(QWidget *parent, const QString &path) {
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext != "vdi" && ext != "vmdk") return path;

    QString newPath = QFileInfo(path).absolutePath() + "/" +
                      QFileInfo(path).completeBaseName() + ".qcow2";
    if (QFile::exists(newPath)) {
        newPath = QFileInfo(path).absolutePath() + "/" +
                  QFileInfo(path).completeBaseName() + "_converted.qcow2";
    }

    QString fmt = (ext == "vdi") ? "vdi" : "vmdk";
    auto reply = QMessageBox::question(parent, "Convert Disk Image",
        QString("This is a %1 file. Convert to QCOW2 for better performance?\n\n"
                "Source: %2\nTarget: %3\n\n"
                "The original file will NOT be modified.")
            .arg(ext.toUpper(), QFileInfo(path).fileName(), QFileInfo(newPath).fileName()),
        QMessageBox::Yes | QMessageBox::No);

    if (reply != QMessageBox::Yes) return path;

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    QStringList args = {"convert", "-f", fmt, "-O", "qcow2", path, newPath};
    proc.start("qemu-img", args);

    // Show progress dialog
    QProgressDialog pd("Converting disk image...", "Cancel", 0, 0, parent);
    pd.setWindowTitle("Converting");
    pd.setWindowModality(Qt::WindowModal);
    pd.setMinimumDuration(0);
    connect(&proc, &QProcess::finished, &pd, &QProgressDialog::reset);
    connect(&pd, &QProgressDialog::canceled, &proc, &QProcess::kill);
    pd.show();
    proc.waitForStarted(5000);
    if (proc.state() != QProcess::Running) {
        QMessageBox::warning(parent, "Conversion Failed",
            "Could not start qemu-img. Make sure QEMU utilities are installed.");
        return path;
    }

    proc.waitForFinished(-1);
    if (proc.exitCode() != 0) {
        QMessageBox::warning(parent, "Conversion Failed",
            QString("qemu-img convert failed:\n%1").arg(QString::fromUtf8(proc.readAll())));
        return path;
    }

    QMessageBox::information(parent, "Conversion Complete",
        QString("Converted to QCOW2:\n%1").arg(newPath));
    return newPath;
}
