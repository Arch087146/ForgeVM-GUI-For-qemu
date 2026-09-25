#ifndef VNCVIEWER_H
#define VNCVIEWER_H

#include <QWidget>
#include <QImage>
#include <QPixmap>
#include <QTcpSocket>
#include <QTimer>

class VncViewer : public QWidget {
    Q_OBJECT
public:
    explicit VncViewer(QWidget *parent = nullptr);
    ~VncViewer() override;

    void connectToHost(const QString &host, quint16 port);
    void disconnectFromHost();
    bool isConnected() const { return m_connected; }
    void setScaleToFit(bool scale);
    bool scaleToFit() const { return m_scaleToFit; }
    void setPassword(const QString &password);

signals:
    void connected();
    void disconnected();
    void connectionFailed(const QString &error);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);
    void onReconnect();

private:
    enum State { WaitingVersion, WaitingAuth, WaitingAuthResult,
                 WaitingServerInit, Ready, WaitingVncChallenge };
    enum RectState { NotReading, ReadingHeader, ReadingData, ReadingHextile };

    void processData();
    void processVersion();
    void processAuth();
    void processAuthResult();
    void processServerInit();
    void processFramebufferUpdate();
    void beginRect();
    void readRectData();
    void readRawRect(int x, int y, int w, int h);
    void readCopyRect(int x, int y, int w, int h);
    int readHextileRect(int x, int y, int w, int h);
    QRgb pixelFromBuffer(const QByteArray &data, int offset);
    void finishRect();
    void requestFullUpdate();
    void sendKeyEvent(quint32 keysym, bool down);
    void sendPointerEvent(int x, int y, int buttonMask);
    void sendFramebufferUpdateRequest(bool incremental, int x, int y, int w, int h);
    void sendSetEncodings();
    void processVncChallenge();
    quint32 keysymFromQt(int qtKey, Qt::KeyboardModifiers mods);

    QTcpSocket *m_socket;
    QImage m_framebuffer;
    QPixmap m_scaledPixmap;
    QByteArray m_buffer;
    QString m_host;
    quint16 m_port;
    bool m_connected;

    State m_state;
    quint16 m_fbWidth, m_fbHeight;
    quint8 m_bpp, m_depth;
    bool m_bigEndian, m_trueColor;
    quint16 m_redMax, m_greenMax, m_blueMax;
    quint8 m_redShift, m_greenShift, m_blueShift;
    QString m_desktopName;

    bool m_scaleToFit;
    int m_mouseX, m_mouseY;
    quint8 m_buttonMask;
    quint16 m_rectsRemaining;
    int m_rectX, m_rectY, m_rectW, m_rectH;
    int m_rectEncoding;
    RectState m_rectState;
    QByteArray m_rectData;
    int m_rectBytesNeeded;
    int m_rawBpp = 4;

    QTimer *m_reconnectTimer;
    int m_reconnectAttempts;
    bool m_userDisconnect = false;
    bool m_rfb33 = false;
    QString m_vncPassword;
    QByteArray m_vncChallenge;
    static const int MaxReconnectAttempts = 5;
    static const int ReconnectInterval = 2000;
};

#endif // VNCVIEWER_H
