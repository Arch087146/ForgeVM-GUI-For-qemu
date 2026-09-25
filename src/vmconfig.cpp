#include "vmconfig.h"
#include <QFileInfo>
#include <QDateTime>

// Helper to build paired lists
static QStringList labelsFromValues(const QStringList &values, const QStringList &labels) {
    if (labels.isEmpty()) return values;
    return labels;
}

// === DiskDrive ===
QJsonObject DiskDrive::toJson() const {
    QJsonObject obj;
    obj["path"] = path;
    obj["sizeMB"] = sizeMB;
    obj["format"] = format;
    obj["controller"] = controller;
    obj["cache"] = cache;
    obj["driveType"] = driveType;
    obj["bootable"] = bootable;
    return obj;
}

DiskDrive DiskDrive::fromJson(const QJsonObject &obj) {
    DiskDrive d;
    d.path = obj["path"].toString();
    d.sizeMB = static_cast<qint64>(obj["sizeMB"].toDouble());
    d.format = obj["format"].toString("qcow2");
    d.controller = obj["controller"].toString(obj["interface"].toString("virtio"));
    d.cache = obj["cache"].toString("writeback");
    int dt = obj["driveType"].toInt(-1);
    if (dt >= 0) {
        d.driveType = dt;
    } else {
        d.driveType = obj["isCDROM"].toBool(false) ? 1 : 0;
    }
    d.bootable = obj["bootable"].toBool(true);
    return d;
}

// === NetworkConfig ===
QJsonObject NetworkConfig::toJson() const {
    QJsonObject obj;
    obj["mode"] = mode;
    obj["bridge"] = bridge;
    obj["model"] = model;
    obj["mac"] = mac;
    obj["enabled"] = enabled;
    return obj;
}

NetworkConfig NetworkConfig::fromJson(const QJsonObject &obj) {
    NetworkConfig n;
    n.mode = obj["mode"].toString("user");
    n.bridge = obj["bridge"].toString();
    n.model = obj["model"].toString("virtio-net");
    n.mac = obj["mac"].toString();
    n.enabled = obj["enabled"].toBool(true);
    return n;
}

// === PortForwardRule ===
QJsonObject PortForwardRule::toJson() const {
    QJsonObject obj;
    obj["protocol"] = protocol;
    obj["hostPort"] = hostPort;
    obj["guestPort"] = guestPort;
    obj["enabled"] = enabled;
    return obj;
}

PortForwardRule PortForwardRule::fromJson(const QJsonObject &obj) {
    PortForwardRule r;
    r.protocol = obj["protocol"].toString("tcp");
    r.hostPort = obj["hostPort"].toInt(0);
    r.guestPort = obj["guestPort"].toInt(0);
    r.enabled = obj["enabled"].toBool(true);
    return r;
}

// === USBPassthrough ===
QJsonObject USBPassthrough::toJson() const {
    QJsonObject obj;
    obj["vendorId"] = vendorId;
    obj["productId"] = productId;
    obj["vendorName"] = vendorName;
    obj["productName"] = productName;
    obj["enabled"] = enabled;
    return obj;
}

USBPassthrough USBPassthrough::fromJson(const QJsonObject &obj) {
    USBPassthrough u;
    u.vendorId = static_cast<quint16>(obj["vendorId"].toInt(0));
    u.productId = static_cast<quint16>(obj["productId"].toInt(0));
    u.vendorName = obj["vendorName"].toString();
    u.productName = obj["productName"].toString();
    u.enabled = obj["enabled"].toBool(false);
    return u;
}

// === SharedDirectory ===
QJsonObject SharedDirectory::toJson() const {
    QJsonObject obj;
    obj["hostPath"] = hostPath;
    obj["mountTag"] = mountTag;
    obj["readonly"] = readonly;
    obj["enabled"] = enabled;
    return obj;
}

SharedDirectory SharedDirectory::fromJson(const QJsonObject &obj) {
    SharedDirectory s;
    s.hostPath = obj["hostPath"].toString();
    s.mountTag = obj["mountTag"].toString("shared");
    s.readonly = obj["readonly"].toBool(false);
    s.enabled = obj["enabled"].toBool(true);
    return s;
}

// === Snapshot ===
QJsonObject Snapshot::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["description"] = description;
    obj["dateTime"] = dateTime;
    return obj;
}

Snapshot Snapshot::fromJson(const QJsonObject &obj) {
    Snapshot s;
    s.name = obj["name"].toString();
    s.description = obj["description"].toString();
    s.dateTime = obj["dateTime"].toString();
    return s;
}

// === VMConfig ===
QJsonObject VMConfig::toJson() const {
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["arch"] = arch;
    obj["osType"] = osType;
    obj["machine"] = machine;
    obj["cpuModel"] = cpuModel;
    obj["cpuCores"] = cpuCores;
    obj["cpuSockets"] = cpuSockets;
    obj["cpuThreads"] = cpuThreads;
    obj["memoryMB"] = memoryMB;
    obj["efiBoot"] = efiBoot;
    obj["kvmEnabled"] = kvmEnabled;
    obj["audioEnabled"] = audioEnabled;
    obj["soundModel"] = soundModel;
    obj["displayMode"] = displayMode;
    obj["vncPort"] = vncPort;
    obj["spicePort"] = spicePort;
    obj["vgaModel"] = vgaModel;
    obj["vga3d"] = vga3d;
    obj["vgaQxl"] = vgaQxl;

    obj["bootOrder"] = bootOrder;
    obj["tpmEnabled"] = tpmEnabled;
    obj["usbTablet"] = usbTablet;
    obj["clipboardShare"] = clipboardShare;
    obj["disk"] = disk.toJson();
    QJsonArray extraArr;
    for (const auto &d : extraDrives) extraArr.append(d.toJson());
    obj["extraDrives"] = extraArr;
    obj["network"] = network.toJson();
    QJsonArray pfArr;
    for (const auto &pf : portForwards) pfArr.append(pf.toJson());
    obj["portForwards"] = pfArr;
    QJsonArray usbArr;
    for (const auto &u : usbDevices) usbArr.append(u.toJson());
    obj["usbDevices"] = usbArr;
    QJsonArray shareArr;
    for (const auto &s : sharedDirs) shareArr.append(s.toJson());
    obj["sharedDirs"] = shareArr;
    obj["kernelPath"] = kernelPath;
    obj["initrdPath"] = initrdPath;
    obj["kernelCmdline"] = kernelCmdline;
    obj["iconName"] = iconName;
    obj["extraArgs"] = extraArgs;
    obj["notes"] = notes;
    obj["productKey"] = productKey;
    return obj;
}

VMConfig VMConfig::fromJson(const QJsonObject &obj) {
    VMConfig c;
    c.id = obj["id"].toString();
    c.name = obj["name"].toString();
    c.arch = obj["arch"].toString("x86_64");
    c.osType = obj["osType"].toString("linux");
    c.machine = obj["machine"].toString("q35");
    c.cpuModel = obj["cpuModel"].toString("host");
    c.cpuCores = obj["cpuCores"].toInt(2);
    c.cpuSockets = obj["cpuSockets"].toInt(1);
    c.cpuThreads = obj["cpuThreads"].toInt(1);
    c.memoryMB = obj["memoryMB"].toInt(2048);
    c.efiBoot = obj["efiBoot"].toBool(true);
    c.kvmEnabled = obj["kvmEnabled"].toBool(true);
    c.audioEnabled = obj["audioEnabled"].toBool(false);
    c.soundModel = obj["soundModel"].toString("hda");
    c.displayMode = obj["displayMode"].toString("vnc");
    c.vncPort = obj["vncPort"].toInt(5900);
    c.spicePort = obj["spicePort"].toInt(0);
    c.vgaModel = obj["vgaModel"].toString("virtio");
    c.vga3d = obj["vga3d"].toBool(false);
    c.vgaQxl = obj["vgaQxl"].toBool(false);
    c.bootOrder = obj["bootOrder"].toString("disk");
    c.tpmEnabled = obj["tpmEnabled"].toBool(false);
    c.usbTablet = obj["usbTablet"].toBool(true);
    c.clipboardShare = obj["clipboardShare"].toBool(false);
    c.disk = DiskDrive::fromJson(obj["disk"].toObject());
    auto extraArr = obj["extraDrives"].toArray();
    for (const auto &v : extraArr)
        c.extraDrives.append(DiskDrive::fromJson(v.toObject()));
    c.network = NetworkConfig::fromJson(obj["network"].toObject());
    auto pfArr = obj["portForwards"].toArray();
    for (const auto &v : pfArr)
        c.portForwards.append(PortForwardRule::fromJson(v.toObject()));
    auto usbArr = obj["usbDevices"].toArray();
    for (const auto &v : usbArr)
        c.usbDevices.append(USBPassthrough::fromJson(v.toObject()));
    auto shareArr = obj["sharedDirs"].toArray();
    for (const auto &v : shareArr)
        c.sharedDirs.append(SharedDirectory::fromJson(v.toObject()));
    c.iconName = obj["iconName"].toString(defaultIconForOs(c.osType));
    c.kernelPath = obj["kernelPath"].toString();
    c.initrdPath = obj["initrdPath"].toString();
    c.kernelCmdline = obj["kernelCmdline"].toString();
    c.extraArgs = obj["extraArgs"].toString();
    c.notes = obj["notes"].toString();
    c.productKey = obj["productKey"].toString();
    return c;
}

QString VMConfig::generateId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString VMConfig::probeFirmware(const QString &arch) {
    QMap<QString, QStringList> candidates;
    candidates["x86_64"] = {
        "/home/linuxbrew/.linuxbrew/share/qemu/edk2-x86_64-code.fd",
        "/usr/share/OVMF/OVMF_CODE_4M.fd",
        "/usr/share/OVMF/OVMF_CODE.fd",
        "/usr/share/qemu/OVMF.fd",
        "/usr/share/ovmf/OVMF.fd",
    };
    candidates["i386"] = {
        "/home/linuxbrew/.linuxbrew/share/qemu/edk2-i386-code.fd",
        "/usr/share/OVMF/OVMF_CODE_4M.fd",
        "/usr/share/OVMF/OVMF_CODE.fd",
    };
    candidates["aarch64"] = {
        "/home/linuxbrew/.linuxbrew/share/qemu/edk2-aarch64-code.fd",
    };
    candidates["arm"] = {
        "/home/linuxbrew/.linuxbrew/share/qemu/edk2-arm-code.fd",
    };
    candidates["riscv64"] = {
        "/home/linuxbrew/.linuxbrew/share/qemu/edk2-riscv-code.fd",
    };
    candidates["loongarch64"] = {
        "/home/linuxbrew/.linuxbrew/share/qemu/edk2-loongarch64-code.fd",
    };
    candidates["ppc64"] = {
        "/home/linuxbrew/.linuxbrew/share/qemu/edk2-ppc64-code.fd",
    };
    for (const auto &path : candidates.value(arch)) {
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

QList<ArchInfo> VMConfig::supportedArchs() {
    return {
        {"x86_64", "x86_64", "qemu-system-x86_64",
         {"q35", "pc", "pc-i440fx-2.1"},
         {"q35 (modern)", "pc (legacy)", "pc-i440fx (old)"},
         {"host", "max", "qemu64", "qemu32", "kvm64", "core2duo", "pentium3"},
         {"Host native", "Max features", "x86-64 generic", "x86 generic",
          "AMD KVM64", "Intel Core 2", "Pentium III"},
         "q35", "host", true, "/usr/share/OVMF/OVMF_CODE.fd",
         {"virtio", "cirrus", "std", "qxl", "vmvga"}, "virtio",
         {"virtio", "ide", "ahci", "nvme", "sata", "scsi"}, "virtio",
         "virtio-net", true,
         {"hda", "ac97", "es1370", "sb16"}, "hda"},

        {"aarch64", "ARM (64-bit)", "qemu-system-aarch64",
         {"virt", "raspi3b", "raspi4b", "sbsa-ref"},
         {"Generic (virt)", "Raspberry Pi 3", "Raspberry Pi 4", "Server"},
         {"host", "max", "cortex-a72", "cortex-a57", "cortex-a53", "neoverse-n1"},
         {"Host native", "Max features", "Cortex-A72", "Cortex-A57",
          "Cortex-A53", "Neoverse N1"},
         "virt", "host", true, "/usr/share/AAVMF/AAVMF_CODE.fd",
         {"virtio", "std", "cirrus"}, "virtio",
         {"virtio", "sd", "nvme"}, "virtio",
         "virtio-net-device", true,
         {"hda", "ac97"}, "hda"},

        {"arm", "ARM (32-bit)", "qemu-system-arm",
         {"virt", "raspi2b", "verdex", "realview-pbx-a9"},
         {"Generic (virt)", "Raspberry Pi 2", "Verdex PXA270", "Realview PBX"},
         {"max", "cortex-a15", "cortex-a9", "cortex-a8", "arm1176"},
         {"Max features", "Cortex-A15", "Cortex-A9", "Cortex-A8", "ARM1176"},
         "virt", "max", false, "",
         {"virtio", "std"}, "virtio",
         {"virtio", "sd"}, "virtio",
         "virtio-net-device", false,
         {"hda"}, "hda"},

        {"riscv64", "RISC-V (64-bit)", "qemu-system-riscv64",
         {"virt", "sifive_u", "spike_v1.10"},
         {"Generic (virt)", "SiFive Unleashed", "Spike"},
         {"rv64", "sifive-u54", "shakti-c"},
         {"RV64GC", "SiFive U54", "Shakti C"},
         "virt", "rv64", true, "/usr/share/opensbi/fw_jump.bin",
         {"virtio", "std"}, "virtio",
         {"virtio", "nvme"}, "virtio",
         "virtio-net-device", false,
         {"hda"}, "hda"},

        {"m68k", "m68k (68000)", "qemu-system-m68k",
         {"q800", "an5206", "mcf5208evb"},
         {"Mac Quadra 800", "Arnewsh 5206", "MCF5208EVB"},
         {"68040", "68060", "m5206", "m68000"},
         {"68040", "68060", "MCF5206", "MC68000"},
         "q800", "68040", false, "",
         {"std", "cirrus", "virtio"}, "std",
         {"ide", "virtio", "sd"}, "ide",
         "virtio-net", false,
         {"hda"}, "hda"},

        {"sparc", "SPARC (32-bit)", "qemu-system-sparc",
         {"SS-5", "SS-10", "SS-20", "LX", "SPARCClassic", "Voyager"},
         {"SS-5 (default)", "SS-10", "SS-20", "LX", "SPARCClassic", "Voyager"},
         {"TI-UltraSparc", "max"},
         {"TI UltraSPARC", "Max"},
         "SS-5", "TI-UltraSparc", false, "",
         {"std", "virtio"}, "std",
         {"ide", "scsi"}, "ide",
         "virtio-net", false,
         {"hda"}, "hda"},

        {"ppc64", "PowerPC (64-bit)", "qemu-system-ppc64",
         {"pseries", "mac99", "g3beige"},
         {"pseries (POWER)", "Mac99 (G4)", "G3 Beige"},
         {"POWER9", "POWER8", "970fx", "power7"},
         {"POWER9", "POWER8", "PowerPC 970", "POWER7"},
         "pseries", "POWER9", true, "/usr/share/edk2-ppc64/edk2-ppc64-code.bin",
         {"std", "virtio", "cirrus"}, "virtio",
         {"virtio", "ide", "nvme"}, "virtio",
         "virtio-net", false,
         {"hda", "ac97"}, "hda"},

        {"i386", "x86 (32-bit)", "qemu-system-i386",
         {"q35", "pc", "pc-i440fx-2.1"},
         {"q35 (modern)", "pc (legacy)", "pc-i440fx (old)"},
         {"max", "qemu64", "qemu32", "coreduo", "pentium3"},
         {"Max features", "x86-64 generic", "x86 generic",
          "Intel Core Duo", "Pentium III"},
         "pc", "qemu32", true, "/usr/share/OVMF/OVMF_CODE.fd",
         {"virtio", "cirrus", "std", "qxl"}, "virtio",
         {"virtio", "ide", "ahci"}, "virtio",
         "virtio-net", true,
         {"hda", "ac97", "es1370", "sb16"}, "hda"},

        {"mips64el", "MIPS (64-bit)", "qemu-system-mips64el",
         {"malta", "fulong2e", "boston"},
         {"Malta", "Fulong 2E", "Boston"},
         {"MIPS64R6-generic", "5KEc", "5KEf", "20Kc"},
         {"MIPS64 R6", "5KEc", "5KEf", "20Kc"},
         "malta", "MIPS64R6-generic", false, "",
         {"virtio", "std"}, "virtio",
         {"virtio", "ide"}, "virtio",
         "virtio-net", false,
         {"hda"}, "hda"},

        {"loongarch64", "LoongArch (64-bit)", "qemu-system-loongarch64",
         {"virt", "la32"},
         {"Generic (virt)", "LA132"},
         {"la464", "max"},
         {"Loongson LA464", "Max features"},
         "virt", "la464", true, "",
         {"virtio", "std"}, "virtio",
         {"virtio"}, "virtio",
         "virtio-net-device", false,
         {"hda"}, "hda"},

        {"sparc64", "SPARC (64-bit)", "qemu-system-sparc64",
         {"sun4u", "niagara", "sun4v"},
         {"Sun4u (Ultra)", "Niagara", "Sun4v"},
         {"UltraSparc-IIi", "UltraSparc-III", "TI-UltraSparc", "max"},
         {"UltraSPARC IIi", "UltraSPARC III", "TI UltraSPARC", "Max"},
         "sun4u", "max", false, "",
         {"std", "virtio"}, "std",
         {"ide", "virtio"}, "ide",
         "virtio-net", false,
         {"hda"}, "hda"},
    };
}

ArchInfo VMConfig::archInfo(const QString &arch) {
    for (const auto &a : supportedArchs()) {
        if (a.arch == arch) return a;
    }
    return supportedArchs().first();
}

static const QMap<QString, QString> s_osIconMap = {
    {"linux", "linux"},
    {"windows", "windows-11"},
    {"windows-11", "windows-11"},
    {"windows-xp", "windows-xp"},
    {"freebsd", "freebsd"},
    {"openbsd", "openbsd"},
    {"netbsd", "netbsd"},
    {"dragonfly", "linux"},
    {"macos", "macos"},
    {"other", "linux"},
    {"aix", "SUSE"},
    {"solaris", "solaris"},
    {"indiana", "solaris"},
    {"backtrack", "backtrack"},
    {"arch-linux", "arch-linux"},
    {"kubuntu", "kubuntu"},
    {"xubuntu", "xubuntu"},
    {"lubuntu", "lubuntu"},
    {"dos", "windows"},
    {"haiku-os", "haiku-os"},
    {"os2", "os2"},
};

QString VMConfig::defaultIconForOs(const QString &osType) {
    return s_osIconMap.value(osType.toLower(), "linux");
}

QStringList VMConfig::availableIcons() {
    return {"AIX", "IOS", "SUSE", "Windows7", "almalinux", "alpine", "amigaos",
        "android", "apple-tv", "arch-linux", "backtrack", "bada", "beos",
        "centos", "chrome-os", "cyanogenmod", "debian", "elementary-os",
        "fedora", "firefox-os", "freebsd", "gentoo", "haiku-os", "hp-ux",
        "kaios", "knoppix", "kubuntu", "linux", "lubuntu", "macos", "mac",
        "maemo", "mandriva", "meego", "mint", "netbsd", "nintendo", "nixos",
        "openbsd", "opensuse", "openwrt", "os2", "palmos", "pardus", "pisi",
        "playstation", "playstation-portable", "pop-os", "red-hat", "remix-os",
        "risc-os", "rocky-linux", "sabayon", "sailfish-os", "slackware",
        "solaris", "syllable", "symbian", "threadx", "tizen", "ubuntu",
        "webos", "windows-11", "windows-9x", "windows", "windows-xp", "xbox",
        "xubuntu", "yunos"};
}
