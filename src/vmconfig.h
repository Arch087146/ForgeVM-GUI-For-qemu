#ifndef VMCONFIG_H
#define VMCONFIG_H

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QVector>
#include <QStringList>

struct DiskDrive {
    QString path;
    qint64 sizeMB = 0;
    QString format = "qcow2";
    QString controller = "virtio";  // ide, ahci, sata, scsi, nvme, virtio
    QString cache = "writeback";
    int driveType = 0;  // 0=Hard Disk, 1=CD/DVD, 2=Floppy
    bool bootable = true;

    bool isEmpty() const { return path.isEmpty(); }

    QJsonObject toJson() const;
    static DiskDrive fromJson(const QJsonObject &obj);
};

struct NetworkConfig {
    QString mode = "user";
    QString bridge;
    QString model = "virtio-net";
    QString mac;
    bool enabled = true;

    QJsonObject toJson() const;
    static NetworkConfig fromJson(const QJsonObject &obj);
};

struct PortForwardRule {
    QString protocol = "tcp";
    int hostPort = 0;
    int guestPort = 0;
    bool enabled = true;

    QJsonObject toJson() const;
    static PortForwardRule fromJson(const QJsonObject &obj);
};

struct USBPassthrough {
    quint16 vendorId = 0;
    quint16 productId = 0;
    QString vendorName;
    QString productName;
    bool enabled = false;

    QJsonObject toJson() const;
    static USBPassthrough fromJson(const QJsonObject &obj);
};

struct SharedDirectory {
    QString hostPath;
    QString mountTag = "shared";
    bool readonly = false;
    bool enabled = true;

    QJsonObject toJson() const;
    static SharedDirectory fromJson(const QJsonObject &obj);
};

struct Snapshot {
    QString name;
    QString description;
    QString dateTime;

    QJsonObject toJson() const;
    static Snapshot fromJson(const QJsonObject &obj);
};

struct ArchInfo {
    QString arch;
    QString label;
    QString binary;
    QStringList machines;
    QStringList machineLabels;
    QStringList cpus;
    QStringList cpuLabels;
    QString defaultMachine;
    QString defaultCpu;
    bool supportsEfi = false;
    QString firmwarePath;
    QStringList displayModels;
    QString defaultDisplay = "virtio";
    QStringList diskInterfaces;
    QString defaultDiskInterface = "virtio";
    QString defaultNetModel = "virtio-net";
    bool supportsKvm = true;
    QStringList soundModels;
    QString defaultSound = "hda";
};

struct VMConfig {
    QString id;
    QString name;
    QString arch = "x86_64";
    QString osType = "linux";
    QString machine = "q35";
    QString cpuModel = "host";
    int cpuCores = 2;
    int cpuSockets = 1;
    int cpuThreads = 1;
    int memoryMB = 2048;
    bool efiBoot = true;
    bool kvmEnabled = true;
    bool audioEnabled = false;
    QString soundModel = "hda";

    // Display
    QString displayMode = "vnc";
    int vncPort = 5900;
    int spicePort = 0;
    QString vgaModel = "virtio";
    bool vga3d = false;
    bool vgaQxl = false;

    // Boot
    QString bootOrder = "disk";
    bool tpmEnabled = false;

    // Drives
    DiskDrive disk;
    QVector<DiskDrive> extraDrives;

    // Network
    NetworkConfig network;
    QVector<PortForwardRule> portForwards;

    // USB
    QVector<USBPassthrough> usbDevices;

    // Sharing
    QVector<SharedDirectory> sharedDirs;
    bool clipboardShare = false;

    // Input
    bool usbTablet = true;

    // Kernel
    QString kernelPath;
    QString initrdPath;
    QString kernelCmdline;

    // Icon
    QString iconName = "linux";

    // Advanced
    QString extraArgs;
    QString notes;
    QString productKey;

    QJsonObject toJson() const;
    static VMConfig fromJson(const QJsonObject &obj);
    static QString generateId();

    static QList<ArchInfo> supportedArchs();
    static ArchInfo archInfo(const QString &arch);
    static QString defaultIconForOs(const QString &osType);
    static QStringList availableIcons();
    static QString probeFirmware(const QString &arch);
};

#endif
