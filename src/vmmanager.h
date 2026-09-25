#ifndef VMMANAGER_H
#define VMMANAGER_H

#include <QObject>
#include <QMap>
#include <QString>
#include "vmconfig.h"
#include "vmtask.h"

class VMManager : public QObject {
    Q_OBJECT
public:
    explicit VMManager(QObject *parent = nullptr);
    ~VMManager();

    void loadConfigs();
    void saveConfig(const VMConfig &config);
    void deleteConfig(const QString &id);

    bool configExists(const QString &id) const;
    VMConfig getConfig(const QString &id) const;
    QList<VMConfig> allConfigs() const;

    VMTask *taskForVM(const QString &id) const;
    bool isVMRunning(const QString &id) const;

    VMTask *startVM(const QString &id);
    void stopVM(const QString &id);
    void pauseVM(const QString &id);
    void resumeVM(const QString &id);
    void resetVM(const QString &id);

    void stopAll();

    QString configDir() const { return m_configDir; }

signals:
    void configListChanged();
    void vmStarted(const QString &id);
    void vmStopped(const QString &id);
    void vmError(const QString &id, const QString &error);
    void vmStateChanged(const QString &id, VMTask::State state);

private:
    void ensureConfigDir();
    QString configFilePath(const QString &id) const;
    void onTaskStateChanged(VMTask::State state);
    void cleanStalePidFiles();

    QString m_configDir;
    QMap<QString, VMConfig> m_configs;
    QMap<QString, VMTask *> m_tasks;
};

#endif
