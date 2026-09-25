#include "vmtask.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>

VMTask::VMTask(const VMConfig &config, QObject *parent)
    : QObject(parent), m_config(config) {
}

VMTask::~VMTask() { finishStop(); }

QString VMTask::stateString() const {
    switch (m_state) {
    case Stopped: return "Stopped";
    case Starting: return "Starting";
    case Running: return "Running";
    case Paused: return "Paused";
    case Stopping: return "Stopping";
    case Error: return "Error";
    }
    return "Unknown";
}

qint64 VMTask::elapsedMs() const {
    if (m_state == Stopped) return 0;
    return m_timer.elapsed();
}

QStringList VMTask::buildCommandLine() {
    QStringList args;
    const auto &cfg = m_config;
    ArchInfo arch = VMConfig::archInfo(cfg.arch);

    // Acceleration
    if (cfg.kvmEnabled && arch.supportsKvm)
        args << "-accel" << "kvm";
    else
        args << "-accel" << "tcg";

    // Machine
    args << "-machine" << cfg.machine;

    // CPU topology
    if (cfg.cpuSockets > 1 || cfg.cpuThreads > 1) {
        args << "-smp"
             << QString("%1,sockets=%2,cores=%3,threads=%4")
                    .arg(cfg.cpuCores * cfg.cpuSockets * cfg.cpuThreads)
                    .arg(cfg.cpuSockets)
                    .arg(cfg.cpuCores)
                    .arg(cfg.cpuThreads);
    } else {
        args << "-smp" << QString::number(cfg.cpuCores);
    }

    // Only suppress SVM on hosts that actually expose it (AMD)
    QString cpuModel = cfg.cpuModel;
    if (cfg.arch == "x86_64" || cfg.arch == "i386") {
        QFile cpuinfo("/proc/cpuinfo");
        bool hasSVM = cpuinfo.exists() && cpuinfo.open(QIODevice::ReadOnly) &&
            QString::fromUtf8(cpuinfo.readAll()).contains(" svm ");
        cpuinfo.close();
        if (hasSVM && (cpuModel == "host" || cpuModel == "max"))
            cpuModel += ",-svm=off";
    }
    args << "-cpu" << cpuModel;
    args << "-m" << QString::number(cfg.memoryMB);

    // UEFI firmware
    if (cfg.efiBoot) {
        QString fwPath = VMConfig::probeFirmware(cfg.arch);
        if (!fwPath.isEmpty()) {
            args << "-drive"
                 << QString("if=pflash,format=raw,readonly=on,file=%1").arg(fwPath);
            QString varsPath = fwPath;
            varsPath.replace("CODE", "VARS", Qt::CaseInsensitive);
            if (QFileInfo::exists(varsPath)) {
                args << "-drive"
                     << QString("if=pflash,format=raw,file=%1").arg(varsPath);
            }
        } else {
            qWarning() << "UEFI firmware not found for arch:" << cfg.arch
                       << "Install OVMF/AAVMF package for UEFI support";
        }
    }

    // Kernel boot
    if (!cfg.kernelPath.isEmpty()) {
        args << "-kernel" << cfg.kernelPath;
        if (!cfg.initrdPath.isEmpty())
            args << "-initrd" << cfg.initrdPath;
        if (!cfg.kernelCmdline.isEmpty())
            args << "-append" << cfg.kernelCmdline;
    }

    // Display
    QString vgaModel = cfg.vgaModel;
    if (cfg.arch == "i386" && vgaModel == "virtio")
        vgaModel = "cirrus";

    if (cfg.displayMode == "spice") {
        // SPICE display with clipboard, drag-drop, shared folders
        if (cfg.vgaQxl) {
            args << "-vga" << "qxl";
        } else if (cfg.vga3d) {
            args << "-device" << "virtio-vga,gl=on";
        } else {
            args << "-vga" << vgaModel;
        }
        m_spicePort = cfg.spicePort > 0 ? cfg.spicePort : 5900;
        args << "-display" << QString("spice:port=%1").arg(m_spicePort - 5900);
        args << "-device" << "spice-vdagent,dock=true";
    } else if (cfg.displayMode == "vnc") {
        m_vncPort = cfg.vncPort;
        if (cfg.vgaQxl) {
            args << "-vga" << "qxl";
        } else if (cfg.vga3d) {
            args << "-device" << "virtio-vga,gl=on";
        } else {
            args << "-vga" << vgaModel;
        }
        QString vncArg = QString("vnc=:%1").arg(cfg.vncPort - 5900);
        args << "-display" << vncArg;
    } else if (cfg.displayMode == "gtk") {
        args << "-vga" << vgaModel;
        args << "-display" << "gtk";
    } else if (cfg.displayMode == "sdl") {
        args << "-vga" << vgaModel;
        args << "-display" << "sdl";
    }

    // USB controller and tablet for proper mouse handling
    if (cfg.usbTablet) {
        args << "-device" << "qemu-xhci";
        args << "-device" << "usb-tablet";
    }

    // SPICE clipboard and drag-drop
    if (cfg.clipboardShare) {
        args << "-device" << "spice-vdagent,dock=true";
    }

    // Primary disk
    if (!cfg.disk.path.isEmpty()) {
        QFileInfo fi(cfg.disk.path);
        if (!fi.exists()) {
            qWarning() << "Primary disk not found:" << cfg.disk.path;
        } else {
            QString diskPath = cfg.disk.path;
            QString diskFormat = cfg.disk.format;
            QString diskController = cfg.disk.controller;
            QString diskCache = cfg.disk.cache;

            // i386 + virtio = no good (XP has no virtio drivers)
            if (cfg.arch == "i386" && diskController == "virtio")
                diskController = "ide";

            // Build the -drive argument
            QString driveArg = QString("file=%1,format=%2").arg(diskPath, diskFormat);
            if (diskController == "virtio")
                driveArg += ",if=virtio";
            else if (diskController == "nvme")
                driveArg += ",if=none,serial=NVME" + cfg.id.left(8);
            else
                driveArg += ",if=" + diskController;

            if (cfg.disk.driveType == 1) {
                driveArg += ",media=cdrom";
            } else if (cfg.disk.driveType == 2) {
                driveArg += ",if=floppy,format=raw";
            } else {
                driveArg += ",cache=" + diskCache;
            }
            args << "-drive" << driveArg;
        }
    }

    // Extra drives + CDROMs
    int drvIdx = 1;
    for (const auto &ed : cfg.extraDrives) {
        if (ed.path.isEmpty()) continue;
        if (ed.path == cfg.disk.path) continue;
        QFileInfo fi(ed.path);
        if (!fi.exists()) continue;

        QString driveArg;
        driveArg += "file=" + ed.path;
        driveArg += ",format=" + ed.format;

        if (ed.driveType == 2) {
            driveArg += ",if=floppy,format=raw";
            args << "-drive" << driveArg;
            continue;
        }

        QString edCtrl = ed.controller;
        if (cfg.arch == "i386" && edCtrl == "virtio")
            edCtrl = "ide";
        if (edCtrl == "virtio")
            driveArg += ",if=virtio";
        else if (edCtrl == "nvme")
            driveArg += ",if=none,serial=NVME" + cfg.id.left(8);
        else
            driveArg += ",if=" + edCtrl;

        if (ed.driveType == 1) {
            driveArg += ",media=cdrom";
        } else {
            driveArg += ",cache=" + ed.cache;
        }
        args << "-drive" << driveArg;
        drvIdx++;
    }

    // Boot order
    QString bootOrder;
    if (cfg.bootOrder == "cdrom,disk")
        bootOrder = "order=dc";
    else if (cfg.bootOrder == "disk,cdrom")
        bootOrder = "order=cd";
    else
        bootOrder = "order=c";
    if (!cfg.extraArgs.isEmpty()) {
        auto tokens = QProcess::splitCommand(cfg.extraArgs);
        bool hasBoot = std::any_of(tokens.begin(), tokens.end(),
            [](const QString &t) { return t.startsWith("-boot"); });
        if (!hasBoot)
            args << "-boot" << bootOrder;
    } else {
        args << "-boot" << bootOrder;
    }

    // TPM
    if (cfg.tpmEnabled && QFileInfo::exists("/dev/tpm0")) {
        args << "-tpmdev" << "passthrough,id=tpm0,path=/dev/tpm0";
        args << "-device" << "tpm-tis,tpmdev=tpm0";
    } else if (cfg.tpmEnabled) {
        qWarning() << "/dev/tpm0 not found, TPM disabled";
    }

    // Network
    if (cfg.network.enabled) {
        QString netdevStr = "user,id=net0";
        // Port forwarding
        for (const auto &pf : cfg.portForwards) {
            if (!pf.enabled || pf.hostPort <= 0 || pf.guestPort <= 0) continue;
            netdevStr += QString(",hostfwd=%1::%2-:%3")
                             .arg(pf.protocol)
                             .arg(pf.hostPort)
                             .arg(pf.guestPort);
        }
        QString netModel = cfg.network.model;
        if (cfg.arch == "i386" && netModel == "virtio-net")
            netModel = "rtl8139";
        QString devStr = QString("%1,netdev=net0").arg(netModel);
        if (!cfg.network.mac.isEmpty())
            devStr += ",mac=" + cfg.network.mac;
        if (cfg.network.mode == "user") {
            args << "-netdev" << netdevStr;
            args << "-device" << devStr;
        } else if (cfg.network.mode == "bridge") {
            args << "-netdev"
                 << QString("bridge,id=net0,br=%1").arg(cfg.network.bridge);
            args << "-device" << devStr;
        } else if (cfg.network.mode == "tap") {
            QString tapStr = QString("tap,id=net0");
            if (!cfg.network.bridge.isEmpty())
                tapStr += ",ifname=" + cfg.network.bridge;
            args << "-netdev" << tapStr;
            args << "-device" << devStr;
        }
    }

    // USB passthrough
    for (const auto &usb : cfg.usbDevices) {
        if (!usb.enabled || usb.vendorId == 0 || usb.productId == 0) continue;
        args << "-device"
             << QString("usb-host,vendorid=0x%1,productid=0x%2")
                    .arg(usb.vendorId, 4, 16, QLatin1Char('0'))
                    .arg(usb.productId, 4, 16, QLatin1Char('0'));
    }

    // Shared directories (9p)
    for (const auto &sd : cfg.sharedDirs) {
        if (!sd.enabled || sd.hostPath.isEmpty()) continue;
        QString security = sd.readonly ? "mapped-xattr" : "none";
        args << "-fsdev"
             << QString("local,id=%1,path=%2,security_model=%3%4")
                    .arg(sd.mountTag, sd.hostPath, security,
                         sd.readonly ? ",readonly" : "");
        args << "-device"
             << QString("virtio-9p-pci,fsdev=%1,mount_tag=%2")
                    .arg(sd.mountTag, sd.mountTag);
    }

    // Audio
    if (cfg.audioEnabled) {
        args << "-audiodev" << "pa,id=audio0";
        if (cfg.soundModel == "ac97")
            args << "-device" << "AC97,audiodev=audio0";
        else if (cfg.soundModel == "es1370")
            args << "-device" << "ES1370,audiodev=audio0";
        else if (cfg.soundModel == "sb16")
            args << "-device" << "sb16,audiodev=audio0";
        else
            args << "-device" << "intel-hda" << "-device" << "hda-duplex,audiodev=audio0";
    }

    // Clipboard sharing via spice
    if (cfg.clipboardShare && cfg.displayMode == "vnc") {
        // SPICE agent for clipboard requires spice, not vnc
        // For VNC, no clipboard sharing support
    }

    args << "-serial" << "mon:stdio";
    args << "-name" << cfg.name;

    // Normalize UUID to standard format (insert dashes if missing)
    QString uuid = cfg.id;
    if (!uuid.contains('-') && uuid.length() == 32) {
        uuid = uuid.mid(0, 8) + '-' + uuid.mid(8, 4) + '-' + uuid.mid(12, 4)
             + '-' + uuid.mid(16, 4) + '-' + uuid.mid(20);
    }
    args << "-uuid" << uuid;

    // Extra args
    if (!cfg.extraArgs.isEmpty()) {
        args << QProcess::splitCommand(cfg.extraArgs);
    }

    return args;
}

bool VMTask::freeVncPort() {
    QProcess fuser;
    fuser.start("fuser", {QString("%1/tcp").arg(m_vncPort)});
    fuser.waitForFinished(3000);
    QStringList lines = QString::fromUtf8(fuser.readAllStandardOutput())
        .trimmed().split('\n', Qt::SkipEmptyParts);

    bool killed = false;
    for (const auto &line : lines) {
        QString pidStr = line.trimmed();
        bool ok;
        qint64 pid = pidStr.toLongLong(&ok);
        if (!ok) continue;
        QProcess ps;
        ps.start("ps", {"-p", QString::number(pid), "-o", "comm="});
        ps.waitForFinished(1000);
        QString cmd = QString::fromUtf8(ps.readAllStandardOutput()).trimmed();
        if (cmd.contains("qemu", Qt::CaseInsensitive)) {
            QProcess kill;
            kill.start("kill", {pidStr});
            kill.waitForFinished(1000);
            killed = true;
        } else if (!cmd.isEmpty()) {
            return false;
        }
    }

    if (killed)
        QThread::msleep(500);

    // Verify port is now free
    QProcess check;
    check.start("fuser", {QString("%1/tcp").arg(m_vncPort)});
    check.waitForFinished(2000);
    return check.readAllStandardOutput().trimmed().isEmpty();
}

bool VMTask::start() {
    if (m_state != Stopped) return false;
    m_muxMonitor = false;
    m_state = Starting;
    emit stateChanged(m_state);

    auto args = buildCommandLine();
    ArchInfo arch = VMConfig::archInfo(m_config.arch);
    QString qemuPath = QStandardPaths::findExecutable(arch.binary);

    // Fallback: search common directories if PATH didn't find it
    if (qemuPath.isEmpty()) {
        QStringList searchDirs = {
            "/usr/bin", "/usr/local/bin", "/usr/sbin",
            "/home/linuxbrew/.linuxbrew/bin",
            "/opt/homebrew/bin", "/opt/local/bin",
            QDir::homePath() + "/.local/bin"
        };
        for (const auto &dir : searchDirs) {
            QString candidate = dir + "/" + arch.binary;
            if (QFileInfo::exists(candidate)) {
                qemuPath = candidate;
                break;
            }
        }
    }

    if (qemuPath.isEmpty())
        qemuPath = arch.binary;

    if (!QFileInfo::exists(qemuPath)) {
        QString msg = QString("QEMU binary not found: '%1'. "
            "Install QEMU for %2 and ensure it is in your PATH.")
            .arg(qemuPath, arch.label);
        emit errorOccurred(msg);
        m_state = Stopped;
        emit stateChanged(m_state);
        return false;
    }

    // Check if VNC port is available; try to free stale QEMU if not
    if (m_config.displayMode == "vnc" && m_vncPort > 0) {
        if (!freeVncPort()) {
            QString msg = QString("VNC port %1 is already in use. "
                "Change the VNC port in VM settings or stop the other "
                "application using it.").arg(m_vncPort);
            emit errorOccurred(msg);
            m_state = Stopped;
            emit stateChanged(m_state);
            return false;
        }
    }

    // Clean up any previous QProcess to prevent stale signal corruption
    if (m_process) {
        m_process->disconnect();
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &VMTask::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &VMTask::onReadyReadStderr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &VMTask::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &VMTask::onProcessError);

    qDebug().noquote() << "QEMU CMD:" << qemuPath << args;
    qDebug().noquote() << "QEMU LINE:" << (qemuPath + " " + args.join(" "));
    connect(m_process, &QProcess::started, this, [this]() {
        m_state = Running;
        m_timer.start();
        emit stateChanged(m_state);
    });

    m_process->start(qemuPath, args);
    return true;
}

void VMTask::stop() {
    if (m_state == Stopped) return;
    m_state = Stopping;
    emit stateChanged(m_state);

    sendMonitorCommand("system_powerdown\n");

    if (!m_process || m_process->state() == QProcess::NotRunning) {
        finishStop();
        return;
    }

    if (m_stopTimer) m_stopTimer->stop();
    m_stopTimer = new QTimer(this);
    m_stopTimer->setSingleShot(false);
    m_stopAttempt = 0;
    connect(m_stopTimer, &QTimer::timeout, this, [this]() {
        if (m_state == Stopped) { if (m_stopTimer) m_stopTimer->stop(); return; }
        if (!m_process) { if (m_stopTimer) m_stopTimer->stop(); finishStop(); return; }
        m_stopAttempt++;
        if (m_process->state() == QProcess::NotRunning) {
            m_stopTimer->stop();
            finishStop();
            return;
        }
        if (m_stopAttempt == 1) {
            // After 3s, try quit
            sendMonitorCommand("quit\n");
        } else if (m_stopAttempt == 4) {
            // After 12s, force kill
            if (m_process->state() != QProcess::NotRunning)
                m_process->kill();
        } else if (m_stopAttempt >= 7) {
            // After 21s, give up and finish
            m_stopTimer->stop();
            finishStop();
        }
    });
    m_stopTimer->start(3000);
}

void VMTask::forceStop() {
    if (m_state == Stopped) return;
    m_state = Stopping;
    emit stateChanged(m_state);
    if (m_process) {
        m_process->kill();
        if (m_stopTimer) {
            m_stopTimer->stop();
            m_stopTimer->deleteLater();
            m_stopTimer = nullptr;
        }
        m_stopAttempt = 0;
        m_forceStopTimer = new QTimer(this);
        m_forceStopTimer->setSingleShot(false);
        connect(m_forceStopTimer, &QTimer::timeout, this, [this]() {
            if (m_state == Stopped) { if (m_forceStopTimer) { m_forceStopTimer->stop(); } return; }
            if (!m_process) { if (m_forceStopTimer) { m_forceStopTimer->stop(); } finishStop(); return; }
            m_stopAttempt++;
            if (m_process->state() == QProcess::NotRunning) {
                m_forceStopTimer->stop();
                finishStop();
            } else if (m_stopAttempt >= 4) {
                m_forceStopTimer->stop();
                finishStop();
            }
        });
        m_forceStopTimer->start(2000);
    } else {
        finishStop();
    }
}

void VMTask::finishStop() {
    if (m_state == Stopped) return;
    if (m_stopTimer) { m_stopTimer->stop(); m_stopTimer->deleteLater(); m_stopTimer = nullptr; }
    if (m_forceStopTimer) { m_forceStopTimer->stop(); m_forceStopTimer->deleteLater(); m_forceStopTimer = nullptr; }
    if (m_monitorProcess) {
        m_monitorProcess->kill();
        m_monitorProcess->deleteLater();
        m_monitorProcess = nullptr;
    }
    if (m_process) {
        m_process->disconnect();
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_state = Stopped;
    emit stateChanged(m_state);
}

void VMTask::pause() {
    if (m_state == Running) {
        sendMonitorCommand("stop\n");
        m_state = Paused;
        emit stateChanged(m_state);
    }
}

void VMTask::resume() {
    if (m_state == Paused) {
        sendMonitorCommand("cont\n");
        m_state = Running;
        emit stateChanged(m_state);
    }
}

void VMTask::reset() {
    if (m_state == Running || m_state == Paused)
        sendMonitorCommand("system_reset\n");
}

void VMTask::sendMonitorCommand(const QString &cmd) {
    if (!m_process || m_process->state() == QProcess::NotRunning) return;
    if (!m_muxMonitor) {
        m_process->write(QByteArray("\x01c"));
        m_muxMonitor = true;
    }
    m_process->write(cmd.toUtf8());
}

void VMTask::sendSerialText(const QString &text) {
    if (!m_process || m_process->state() == QProcess::NotRunning) return;
    if (m_muxMonitor) {
        m_process->write(QByteArray("\x01c"));
        m_muxMonitor = false;
    }
    m_process->write(text.toUtf8());
}

void VMTask::querySnapshots() {
    if (!m_process || m_process->state() == QProcess::NotRunning) return;
    m_pendingSnapshotQuery = true;
    m_snapshotBuffer.clear();
    sendMonitorCommand("info snapshots\n");
}

void VMTask::onReadyReadStdout() {
    if (!m_process) return;
    QByteArray data = m_process->readAllStandardOutput();
    m_serialBuffer.append(data);
    if (m_serialBuffer.size() > 10 * 1024 * 1024)
        m_serialBuffer.remove(0, m_serialBuffer.size() - 5 * 1024 * 1024);
    emit outputReceived(QString::fromUtf8(data));
    emit serialReceived(QString::fromUtf8(data));

    if (m_pendingSnapshotQuery) {
        m_snapshotBuffer.append(data);
        // Check if snapshot list response is complete (no more data for a moment)
        // The snapshot list ends when we see the prompt or a blank line after data
        if (m_snapshotBuffer.contains("Snapshot list:") || m_snapshotBuffer.size() > 0) {
            // Wait a bit for more data, then process
            QTimer::singleShot(300, this, [this]() {
                if (!m_pendingSnapshotQuery) return;
                m_pendingSnapshotQuery = false;

                QStringList snapshots;
                QString output = QString::fromUtf8(m_snapshotBuffer);
                // Parse snapshot lines: lines with ID numbers like "     1  name ..."
                for (const auto &line : output.split('\n')) {
                    QString trimmed = line.trimmed();
                    if (trimmed.isEmpty() || trimmed.startsWith("Snapshot") || trimmed.startsWith("ID"))
                        continue;
                    // Check if line starts with spaces then a number (snapshot entry)
                    if (line.startsWith(" ") && !trimmed.isEmpty() && trimmed[0].isDigit()) {
                        // Extract snapshot name: first non-space token after the number
                        QStringList parts = trimmed.split(' ', Qt::SkipEmptyParts);
                        if (parts.size() >= 2) {
                            snapshots.append(parts[1]); // TAG is second field
                        }
                    }
                }
                emit snapshotListReceived(snapshots);
                m_snapshotBuffer.clear();
            });
        }
    }
}

void VMTask::onReadyReadStderr() {
    if (!m_process) return;
    QString text = QString::fromUtf8(m_process->readAllStandardError());
    emit outputReceived(text);

    // Detect VNC port conflict from QEMU stderr (fallback if proactive check missed it)
    if (m_config.displayMode == "vnc"
        && (text.contains("Failed to find an available port")
            || text.contains("Address already in use"))) {
        QString msg = QString("VNC port %1 is already in use. "
            "Change the VNC port in VM settings or stop the other "
            "application using it.").arg(m_vncPort);
        emit errorOccurred(msg);
    }
}

void VMTask::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    Q_UNUSED(status)
    if (m_state == Stopped || m_state == Error) return;
    if (m_stopTimer) { m_stopTimer->stop(); m_stopTimer->deleteLater(); m_stopTimer = nullptr; }
    if (m_forceStopTimer) { m_forceStopTimer->stop(); m_forceStopTimer->deleteLater(); m_forceStopTimer = nullptr; }
    if (m_monitorProcess) {
        m_monitorProcess->kill();
        m_monitorProcess->deleteLater();
        m_monitorProcess = nullptr;
    }
    if (m_process) {
        m_process->disconnect();
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (m_state == Stopping) {
        m_state = Stopped;
    } else {
        m_state = Error;
        emit errorOccurred(QString("QEMU exited unexpectedly with code %1").arg(exitCode));
    }
    emit vmExited(exitCode);
    emit stateChanged(m_state);
    qDebug() << "QEMU exited with code:" << exitCode;
}

void VMTask::onProcessError(QProcess::ProcessError error) {
    QString binary = m_process ? m_process->program() : "?";
    QString detail = m_process ? m_process->errorString() : QString();
    QString msg;
    switch (error) {
    case QProcess::FailedToStart:
        msg = QString("Cannot launch '%1': %2").arg(binary, detail);
        break;
    case QProcess::Crashed: msg = "Process crashed"; break;
    case QProcess::Timedout: msg = "Operation timed out"; break;
    case QProcess::WriteError: msg = "Write error"; break;
    case QProcess::ReadError: msg = "Read error"; break;
    default: msg = "Unknown error"; break;
    }
    m_state = Error;
    if (m_stopTimer) { m_stopTimer->stop(); m_stopTimer->deleteLater(); m_stopTimer = nullptr; }
    if (m_forceStopTimer) { m_forceStopTimer->stop(); m_forceStopTimer->deleteLater(); m_forceStopTimer = nullptr; }
    if (m_monitorProcess) {
        m_monitorProcess->kill();
        m_monitorProcess->deleteLater();
        m_monitorProcess = nullptr;
    }
    if (m_process) {
        m_process->disconnect();
        m_process->deleteLater();
        m_process = nullptr;
    }
    emit errorOccurred(msg);
    emit stateChanged(m_state);
}

void VMTask::readMonitorOutput() {
    if (m_monitorProcess)
        emit monitorReply(QString::fromUtf8(m_monitorProcess->readAllStandardOutput()));
}
