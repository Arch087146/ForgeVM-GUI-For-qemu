#ifndef VMTASK_H
#define VMTASK_H

#include <QObject>
#include <QProcess>
#include <QElapsedTimer>
#include <QTimer>
#include "vmconfig.h"

class VMTask : public QObject {
    Q_OBJECT
public:
    enum State { Stopped, Starting, Running, Paused, Stopping, Error };
    Q_ENUM(State)

    explicit VMTask(const VMConfig &config, QObject *parent = nullptr);
    ~VMTask();

    bool start();
    void stop();
    void forceStop();
    void pause();
    void resume();
    void reset();
    void sendMonitorCommand(const QString &cmd);
    void sendSerialText(const QString &text);

    bool isRunning() const { return m_state == Running || m_state == Paused; }
    bool isPaused() const { return m_state == Paused; }
    State state() const { return m_state; }
    const VMConfig &config() const { return m_config; }
    int vncPort() const { return m_vncPort; }
    int spicePort() const { return m_spicePort; }
    QByteArray serialBuffer() const { return m_serialBuffer; }
    qint64 elapsedMs() const;
    QString stateString() const;
    void querySnapshots();

signals:
    void outputReceived(const QString &text);
    void serialReceived(const QString &text);
    void monitorReply(const QString &text);
    void snapshotListReceived(const QStringList &snapshots);
    void stateChanged(VMTask::State state);
    void errorOccurred(const QString &error);
    void vmExited(int exitCode);

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void readMonitorOutput();

private:
    QStringList buildCommandLine();
    bool freeVncPort();
    void finishStop();

    QProcess *m_process = nullptr;
    QProcess *m_monitorProcess = nullptr;
    bool m_muxMonitor = false;
    VMConfig m_config;
    State m_state = Stopped;
    int m_vncPort = -1;
    int m_spicePort = -1;
    QElapsedTimer m_timer;
    QByteArray m_serialBuffer;
    QTimer *m_stopTimer = nullptr;
    QTimer *m_forceStopTimer = nullptr;
    int m_stopAttempt = 0;
    bool m_pendingSnapshotQuery = false;
    QByteArray m_snapshotBuffer;
};

#endif
