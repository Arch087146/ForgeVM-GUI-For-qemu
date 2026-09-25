#include "vmmanager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QDebug>

VMManager::VMManager(QObject *parent) : QObject(parent) {
    m_configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                  + "/vms";
    ensureConfigDir();
    loadConfigs();
    cleanStalePidFiles();
}

void VMManager::cleanStalePidFiles() {
    QDir tmp("/tmp");
    auto files = tmp.entryList({"qemu-vm-*.pid"}, QDir::Files);
    for (const auto &f : files) {
        QFile file(tmp.absoluteFilePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        QByteArray pidData = file.readAll().trimmed();
        bool ok;
        qint64 pid = pidData.toLongLong(&ok);
        if (!ok) { file.remove(); continue; }
        QProcess ps;
        ps.start("ps", {"-p", QString::number(pid), "-o", "comm="});
        ps.waitForFinished(1000);
        QString comm = QString::fromUtf8(ps.readAllStandardOutput()).trimmed();
        if (!comm.contains("qemu", Qt::CaseInsensitive))
            file.remove();
    }
}

VMManager::~VMManager() {
    stopAll();
}

void VMManager::ensureConfigDir() {
    QDir dir(m_configDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
}

QString VMManager::configFilePath(const QString &id) const {
    return m_configDir + "/" + id + ".json";
}

void VMManager::loadConfigs() {
    m_configs.clear();
    QDir dir(m_configDir);
    auto files = dir.entryList({"*.json"}, QDir::Files, QDir::Name);
    for (const auto &file : files) {
        QFile f(dir.absoluteFilePath(file));
        if (!f.open(QIODevice::ReadOnly)) continue;
        auto doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject()) {
            auto cfg = VMConfig::fromJson(doc.object());
            if (!cfg.name.isEmpty() && !cfg.id.isEmpty())
                m_configs[cfg.id] = cfg;
        }
    }
    emit configListChanged();
}

void VMManager::saveConfig(const VMConfig &config) {
    m_configs[config.id] = config;
    QString path = configFilePath(config.id);
    QString tmpPath = path + ".tmp";
    QFile file(tmpPath);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(config.toJson());
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        QFile::remove(path);
        QFile::rename(tmpPath, path);
    }
    emit configListChanged();
}

void VMManager::deleteConfig(const QString &id) {
    m_configs.remove(id);
    QFile::remove(configFilePath(id));
    if (m_tasks.contains(id)) {
        auto *task = m_tasks.take(id);
        task->stop();
        task->deleteLater();
    }
    emit configListChanged();
}

bool VMManager::configExists(const QString &id) const {
    return m_configs.contains(id);
}

VMConfig VMManager::getConfig(const QString &id) const {
    return m_configs.value(id);
}

QList<VMConfig> VMManager::allConfigs() const {
    return m_configs.values();
}

VMTask *VMManager::taskForVM(const QString &id) const {
    return m_tasks.value(id, nullptr);
}

bool VMManager::isVMRunning(const QString &id) const {
    auto *task = m_tasks.value(id, nullptr);
    return task && task->isRunning();
}

VMTask *VMManager::startVM(const QString &id) {
    if (!m_configs.contains(id)) return nullptr;
    if (m_tasks.contains(id)) {
        auto *existing = m_tasks[id];
        if (existing->state() == VMTask::Error || existing->state() == VMTask::Stopped) {
            existing->disconnect();
            existing->deleteLater();
            m_tasks.remove(id);
        } else {
            return existing;
        }
    }

    auto cfg = m_configs[id];
    // Kill only stale QEMU processes holding the disk lock
    if (!cfg.disk.path.isEmpty()) {
        QProcess fuser;
        fuser.start("fuser", {cfg.disk.path});
        fuser.waitForFinished(3000);
        QStringList pids = QString::fromUtf8(fuser.readAllStandardOutput())
            .trimmed().split(' ', Qt::SkipEmptyParts);
        for (const auto &pidStr : pids) {
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
            }
        }
    }
    auto *task = new VMTask(cfg);

    connect(task, &VMTask::errorOccurred, this, [this, id](const QString &error) {
        emit vmError(id, error);
    });
    connect(task, &VMTask::stateChanged, this, [this, id](VMTask::State state) {
        if (state == VMTask::Stopped || state == VMTask::Error) {
            auto *t = m_tasks.take(id);
            if (t) t->deleteLater();
            if (state == VMTask::Stopped)
                emit vmStopped(id);
        }
        emit vmStateChanged(id, state);
    });

    if (!task->start()) {
        task->deleteLater();
        return nullptr;
    }
    m_tasks[id] = task;
    emit vmStarted(id);
    return task;
}

void VMManager::stopVM(const QString &id) {
    auto *task = m_tasks.value(id, nullptr);
    if (task) {
        task->stop();
    }
}

void VMManager::pauseVM(const QString &id) {
    auto *task = m_tasks.value(id, nullptr);
    if (task) task->pause();
}

void VMManager::resumeVM(const QString &id) {
    auto *task = m_tasks.value(id, nullptr);
    if (task) task->resume();
}

void VMManager::resetVM(const QString &id) {
    auto *task = m_tasks.value(id, nullptr);
    if (task) task->reset();
}

void VMManager::stopAll() {
    auto ids = m_tasks.keys();
    for (const auto &id : ids) {
        stopVM(id);
    }
}
