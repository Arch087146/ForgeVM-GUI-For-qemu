#ifndef VNCDISPLAYWINDOW_H
#define VNCDISPLAYWINDOW_H

#include <QWidget>
#include <QPushButton>
#include "vncviewer.h"

class VncDisplayWindow : public QWidget {
    Q_OBJECT
public:
    explicit VncDisplayWindow(QWidget *parent = nullptr);
    ~VncDisplayWindow() override;

    void connectToHost(const QString &host, quint16 port);
    void disconnectFromHost();
    bool isConnected() const;
    void setScaleToFit(bool scale);
    bool scaleToFit() const;
    void setVmName(const QString &name);
    void setPassword(const QString &password);

signals:
    void connected();
    void disconnected();
    void connectionFailed(const QString &error);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onConnected();
    void onDisconnected();
    void onConnectionFailed(const QString &err);
    void toggleScaleToFit();
    void toggleFullscreen();

private:
    VncViewer *m_viewer;
    QPushButton *m_scaleBtn;
    QPushButton *m_fullscreenBtn;
    QString m_vmName;
};

#endif
