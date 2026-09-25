#include "vncviewer.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QDebug>
#include <openssl/des.h>
#include <openssl/md5.h>

VncViewer::VncViewer(QWidget *parent)
    : QWidget(parent)
    , m_socket(new QTcpSocket(this))
    , m_connected(false)
    , m_state(WaitingVersion)
    , m_fbWidth(0), m_fbHeight(0)
    , m_scaleToFit(true)
    , m_mouseX(0), m_mouseY(0)
    , m_buttonMask(0)
    , m_rectsRemaining(0)
    , m_rectState(NotReading)
    , m_rectBytesNeeded(0)
    , m_reconnectTimer(new QTimer(this))
    , m_reconnectAttempts(0)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(m_socket, &QTcpSocket::connected, this, &VncViewer::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &VncViewer::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &VncViewer::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &VncViewer::onError);
    connect(m_reconnectTimer, &QTimer::timeout, this, &VncViewer::onReconnect);

    setAttribute(Qt::WA_OpaquePaintEvent);
}

VncViewer::~VncViewer() {
    disconnectFromHost();
}

void VncViewer::setPassword(const QString &password) {
    m_vncPassword = password;
}

static QByteArray vncBitReverse(const QByteArray &data) {
    QByteArray result = data;
    for (int i = 0; i < result.size(); i++) {
        quint8 b = result[i];
        quint8 r = 0;
        for (int j = 0; j < 8; j++)
            r = (r << 1) | ((b >> j) & 1);
        result[i] = r;
    }
    return result;
}

static QByteArray vncDesEncrypt(const QByteArray &challenge, const QByteArray &key) {
    QByteArray k = key.left(8);
    while (k.size() < 8) k.append((char)0);
    k = vncBitReverse(k);

    DES_key_schedule schedule;
    DES_set_key_unchecked((const_DES_cblock*)k.constData(), &schedule);

    QByteArray out(16, 0);
    for (int i = 0; i < 2; i++) {
        DES_ecb_encrypt((const_DES_cblock*)(challenge.constData() + i * 8),
                        (DES_cblock*)(out.data() + i * 8),
                        &schedule, DES_ENCRYPT);
    }
    return out;
}

void VncViewer::connectToHost(const QString &host, quint16 port) {
    m_host = host;
    m_port = port;
    m_reconnectAttempts = 0;
    m_userDisconnect = true;
    m_reconnectTimer->stop();
    disconnect(m_socket, &QTcpSocket::disconnected, this, &VncViewer::onDisconnected);
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    m_connected = false;
    m_framebuffer = QImage();
    m_state = WaitingVersion;
    m_buffer.clear();
    m_rectState = NotReading;
    m_fbWidth = 0;
    m_fbHeight = 0;
    connect(m_socket, &QTcpSocket::disconnected, this, &VncViewer::onDisconnected);
    m_userDisconnect = false;
    m_socket->connectToHost(host, port);
}

void VncViewer::disconnectFromHost() {
    m_userDisconnect = true;
    m_reconnectTimer->stop();
    m_socket->disconnectFromHost();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->waitForDisconnected(1000);
    m_socket->abort();
    m_connected = false;
    m_framebuffer = QImage();
    update();
}

void VncViewer::setScaleToFit(bool scale) {
    m_scaleToFit = scale;
    update();
}

void VncViewer::onConnected() {
    m_reconnectAttempts = 0;
}

void VncViewer::onDisconnected() {
    m_connected = false;
    emit disconnected();
    if (m_userDisconnect) {
        m_userDisconnect = false;
        return;
    }
    if (m_reconnectAttempts < MaxReconnectAttempts) {
        m_reconnectAttempts++;
        m_reconnectTimer->start(ReconnectInterval);
    }
}

void VncViewer::onError(QAbstractSocket::SocketError) {
    if (m_state == WaitingVersion) {
        if (m_reconnectAttempts < MaxReconnectAttempts) {
            m_reconnectAttempts++;
            m_reconnectTimer->start(ReconnectInterval);
        } else {
            emit connectionFailed(m_socket->errorString());
        }
    }
}

void VncViewer::onReconnect() {
    m_reconnectTimer->stop();
    m_userDisconnect = true;
    disconnect(m_socket, &QTcpSocket::disconnected, this, &VncViewer::onDisconnected);
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    m_connected = false;
    m_framebuffer = QImage();
    m_state = WaitingVersion;
    m_buffer.clear();
    m_rectState = NotReading;
    m_fbWidth = 0;
    m_fbHeight = 0;
    connect(m_socket, &QTcpSocket::disconnected, this, &VncViewer::onDisconnected);
    m_userDisconnect = false;
    m_socket->connectToHost(m_host, m_port);
}

void VncViewer::onReadyRead() {
    m_buffer.append(m_socket->readAll());
    processData();
}

void VncViewer::processData() {
    while (m_buffer.size() > 0) {
        if (m_rectState == ReadingHeader) {
            if (m_buffer.size() < 12) return;
            beginRect();
            continue;
        }
        if (m_rectState == ReadingHextile) {
            int used = readHextileRect(m_rectX, m_rectY, m_rectW, m_rectH);
            if (used < 0) return;
            m_buffer.remove(0, used);
            finishRect();
            continue;
        }
        if (m_rectState == ReadingData) {
            if (m_buffer.size() < m_rectBytesNeeded) return;
            m_rectData = m_buffer.left(m_rectBytesNeeded);
            m_buffer.remove(0, m_rectBytesNeeded);
            readRectData();
            continue;
        }

        switch (m_state) {
        case WaitingVersion:
            if (m_buffer.size() < 12) return;
            processVersion();
            break;
        case WaitingAuth:
            if (m_buffer.size() < 1) return;
            processAuth();
            break;
        case WaitingAuthResult:
            if (m_buffer.size() < 4) return;
            processAuthResult();
            break;
        case WaitingVncChallenge:
            if (m_buffer.size() < 16) return;
            processVncChallenge();
            break;
        case WaitingServerInit:
            if (m_buffer.size() < 20) return;
            processServerInit();
            break;
        case Ready:
            if (m_buffer.size() < 3) return;
            processFramebufferUpdate();
            break;
        }
    }
}

void VncViewer::processVersion() {
    QByteArray ver = m_buffer.left(12);
    m_buffer.remove(0, 12);
    if (ver.startsWith("RFB 003.008") || ver.startsWith("RFB 003.007")) {
        m_socket->write("RFB 003.008\n");
        m_rfb33 = false;
    } else {
        m_socket->write("RFB 003.003\n");
        m_rfb33 = true;
    }
    m_state = WaitingAuth;
}

void VncViewer::processAuth() {
    if (m_rfb33) {
        // RFB 3.3: server sends a single u32 security type
        if (m_buffer.size() < 4) return;
        quint32 secType = ((quint8)m_buffer[0] << 24)
                        | ((quint8)m_buffer[1] << 16)
                        | ((quint8)m_buffer[2] << 8)
                        | (quint8)m_buffer[3];
        m_buffer.remove(0, 4);
        if (secType == 1) {
            // None auth — no result message in 3.3, go straight to ServerInit
            m_state = WaitingServerInit;
        } else if (secType == 2 && !m_vncPassword.isEmpty()) {
            // VNC password auth
            m_vncChallenge.clear();
            m_state = WaitingVncChallenge;
        } else {
            m_socket->disconnectFromHost();
        }
        return;
    }

    // RFB 3.7+: server sends u8 count, then u8[count] security types
    quint8 count = (quint8)m_buffer[0];
    m_buffer.remove(0, 1);
    if (count == 0) {
        // No auth types available — server will follow with a failure string
        m_socket->disconnectFromHost();
        return;
    }
    if (m_buffer.size() < count) return;

    // Pick best type: prefer 1 (None), fall back to 2 (VNC) if password set
    quint8 selectedType = 0;
    for (int i = 0; i < (int)count; i++) {
        if ((quint8)m_buffer[i] == 1) { selectedType = 1; break; }
        if ((quint8)m_buffer[i] == 2 && !m_vncPassword.isEmpty())
            selectedType = 2;
    }
    m_buffer.remove(0, count);

    // Send selected security type
    QByteArray sel;
    sel.append((char)selectedType);
    m_socket->write(sel);

    if (selectedType == 1) {
        // None auth — server will send SecurityResult
        m_state = WaitingAuthResult;
    } else if (selectedType == 2) {
        // VNC password auth — read 16-byte challenge
        m_vncChallenge.clear();
        m_state = WaitingVncChallenge;
    } else {
        m_socket->disconnectFromHost();
    }
}

void VncViewer::processVncChallenge() {
    if (m_buffer.size() < 16) return;
    m_vncChallenge = m_buffer.left(16);
    m_buffer.remove(0, 16);

    QByteArray response = vncDesEncrypt(m_vncChallenge, m_vncPassword.toUtf8());
    m_socket->write(response);
    m_state = WaitingAuthResult;
}

void VncViewer::processAuthResult() {
    if (m_buffer.size() < 4) return;
    quint32 result = ((quint8)m_buffer[0] << 24)
                   | ((quint8)m_buffer[1] << 16)
                   | ((quint8)m_buffer[2] << 8)
                   | (quint8)m_buffer[3];
    m_buffer.remove(0, 4);
    if (result == 0) {
        // Send ClientInit (shared flag = 1)
        QByteArray msg;
        msg.append((char)1);
        m_socket->write(msg);
        m_state = WaitingServerInit;
    } else {
        emit connectionFailed("VNC authentication failed — wrong password?");
        m_socket->disconnectFromHost();
    }
}

void VncViewer::processServerInit() {
    if (m_buffer.size() < 20) return;

    m_fbWidth = ((quint8)m_buffer[0] << 8) | (quint8)m_buffer[1];
    m_fbHeight = ((quint8)m_buffer[2] << 8) | (quint8)m_buffer[3];

    if (m_fbWidth == 0 || m_fbHeight == 0 || m_fbWidth > 8192 || m_fbHeight > 8192) {
        m_socket->disconnectFromHost();
        return;
    }

    m_bpp = (quint8)m_buffer[4];
    m_depth = (quint8)m_buffer[5];
    m_bigEndian = (quint8)m_buffer[6] != 0;
    m_trueColor = (quint8)m_buffer[7] != 0;
    m_redMax = ((quint8)m_buffer[8] << 8) | (quint8)m_buffer[9];
    m_greenMax = ((quint8)m_buffer[10] << 8) | (quint8)m_buffer[11];
    m_blueMax = ((quint8)m_buffer[12] << 8) | (quint8)m_buffer[13];
    m_redShift = (quint8)m_buffer[14];
    m_greenShift = (quint8)m_buffer[15];
    m_blueShift = (quint8)m_buffer[16];
    m_buffer.remove(0, 20);

    if (m_buffer.size() < 4) return;
    quint32 nameLen = ((quint8)m_buffer[0] << 24)
                    | ((quint8)m_buffer[1] << 16)
                    | ((quint8)m_buffer[2] << 8)
                    | (quint8)m_buffer[3];
    m_buffer.remove(0, 4);
    if (m_buffer.size() < (int)nameLen) return;
    m_desktopName = QString::fromUtf8(m_buffer.left(nameLen));
    m_buffer.remove(0, nameLen);

    m_framebuffer = QImage(m_fbWidth, m_fbHeight, QImage::Format_RGB32);
    m_framebuffer.fill(Qt::black);

    m_connected = true;
    m_state = Ready;

    sendSetEncodings();
    requestFullUpdate();
    emit connected();
}

void VncViewer::requestFullUpdate() {
    sendFramebufferUpdateRequest(false, 0, 0, m_fbWidth, m_fbHeight);
}

void VncViewer::sendSetEncodings() {
    QByteArray msg;
    msg.append((char)2);
    msg.append((char)0);
    quint16 count = 3;
    msg.append(count >> 8);
    msg.append(count & 0xFF);
    // encodings: raw=0, copyrect=1, hextile=5
    quint32 encs[] = { 0, 1, 5 };
    for (auto e : encs) {
        msg.append((e >> 24) & 0xFF);
        msg.append((e >> 16) & 0xFF);
        msg.append((e >> 8) & 0xFF);
        msg.append(e & 0xFF);
    }
    m_socket->write(msg);
}

void VncViewer::sendFramebufferUpdateRequest(bool incremental, int x, int y, int w, int h) {
    QByteArray msg;
    msg.append((char)3);
    msg.append((char)(incremental ? 1 : 0));
    msg.append((x >> 8) & 0xFF);
    msg.append(x & 0xFF);
    msg.append((y >> 8) & 0xFF);
    msg.append(y & 0xFF);
    msg.append((w >> 8) & 0xFF);
    msg.append(w & 0xFF);
    msg.append((h >> 8) & 0xFF);
    msg.append(h & 0xFF);
    m_socket->write(msg);
}

void VncViewer::processFramebufferUpdate() {
    if (m_buffer.size() < 3) return;
    m_buffer.remove(0, 1); // message-type
    m_rectsRemaining = ((quint8)m_buffer[0] << 8) | (quint8)m_buffer[1];
    m_buffer.remove(0, 2);
    if (m_rectsRemaining > 0)
        m_rectState = ReadingHeader;
}

void VncViewer::beginRect() {
    if (m_buffer.size() < 12) return;

    m_rectX = ((quint8)m_buffer[0] << 8) | (quint8)m_buffer[1];
    m_rectY = ((quint8)m_buffer[2] << 8) | (quint8)m_buffer[3];
    m_rectW = ((quint8)m_buffer[4] << 8) | (quint8)m_buffer[5];
    m_rectH = ((quint8)m_buffer[6] << 8) | (quint8)m_buffer[7];
    m_rectEncoding = (int)((quint8)m_buffer[8] << 24)
                   | ((quint8)m_buffer[9] << 16)
                   | ((quint8)m_buffer[10] << 8)
                   | (quint8)m_buffer[11];

    // Clamp rectangle to framebuffer bounds
    if (m_rectX < 0) m_rectX = 0;
    if (m_rectY < 0) m_rectY = 0;
    if (m_rectX + m_rectW > (int)m_fbWidth) m_rectW = (int)m_fbWidth - m_rectX;
    if (m_rectY + m_rectH > (int)m_fbHeight) m_rectH = (int)m_fbHeight - m_rectY;
    if (m_rectW < 0) m_rectW = 0;
    if (m_rectH < 0) m_rectH = 0;

    m_buffer.remove(0, 12);

    m_rectData.clear();

    switch (m_rectEncoding) {
    case 0: {
        m_rawBpp = m_bpp / 8;
        if (m_rawBpp < 1 || m_rawBpp > 4) m_rawBpp = 4;
        m_rectBytesNeeded = m_rectW * m_rectH * m_rawBpp;
        break;
    }
    case 1: m_rectBytesNeeded = 4; break;
    case 5: m_rectState = ReadingHextile; return;
    default: m_rectBytesNeeded = 0; break;
    }

    if (m_rectBytesNeeded <= 0 || m_rectW == 0 || m_rectH == 0) {
        finishRect();
        return;
    }

    m_rectState = ReadingData;
}

void VncViewer::readRectData() {
    switch (m_rectEncoding) {
    case 0: readRawRect(m_rectX, m_rectY, m_rectW, m_rectH); break;
    case 1: readCopyRect(m_rectX, m_rectY, m_rectW, m_rectH); break;
    default: break;
    }
    finishRect();
}

void VncViewer::finishRect() {
    m_rectData.clear();
    m_rectsRemaining--;
    if (m_rectsRemaining > 0) {
        m_rectState = ReadingHeader;
        // Check if we already have header data
        if (m_buffer.size() >= 12)
            beginRect();
    } else {
        m_rectState = NotReading;
        update();
        // Request next incremental update
        sendFramebufferUpdateRequest(true, 0, 0, m_fbWidth, m_fbHeight);
    }
}

void VncViewer::readRawRect(int x, int y, int w, int h) {
    if (m_framebuffer.isNull()) return;
    int bpp = m_rawBpp;
    if (m_rectData.size() < w * h * bpp) return;
    int bpl = m_framebuffer.bytesPerLine();
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= m_fbHeight) continue;
        uint *dst = (uint*)(m_framebuffer.bits() + py * bpl);
        for (int col = 0; col < w; col++) {
            int px = x + col;
            if (px < 0 || px >= m_fbWidth) continue;
            int off = (row * w + col) * bpp;
            if (off + bpp > m_rectData.size()) return;
            quint32 pixel = 0;
            if (m_bigEndian) {
                for (int i = 0; i < bpp; i++)
                    pixel = (pixel << 8) | (quint8)m_rectData[off + i];
            } else {
                for (int i = bpp - 1; i >= 0; i--)
                    pixel = (pixel << 8) | (quint8)m_rectData[off + i];
            }
            quint8 r = (pixel >> m_redShift) & 0xFF;
            quint8 g = (pixel >> m_greenShift) & 0xFF;
            quint8 b = (pixel >> m_blueShift) & 0xFF;
            dst[px] = qRgb(r, g, b);
        }
    }
}

void VncViewer::readCopyRect(int x, int y, int w, int h) {
    if (m_framebuffer.isNull()) return;
    if (m_rectData.size() < 4) return;
    int srcX = ((quint8)m_rectData[0] << 8) | (quint8)m_rectData[1];
    int srcY = ((quint8)m_rectData[2] << 8) | (quint8)m_rectData[3];

    int bpl = m_framebuffer.bytesPerLine();
    int maxRow = qMin(y + h, (int)m_fbHeight);
    int maxCol = qMin(x + w, (int)m_fbWidth);
    for (int row = y; row < maxRow; row++) {
        int sy = srcY + (row - y);
        if (sy < 0 || sy >= m_fbHeight) continue;
        uint *dst = (uint*)(m_framebuffer.bits() + row * bpl);
        uint *src = (uint*)(m_framebuffer.bits() + sy * bpl);
        for (int col = x; col < maxCol; col++) {
            int sx = srcX + (col - x);
            if (sx < 0 || sx >= m_fbWidth) continue;
            dst[col] = src[sx];
        }
    }
}

QRgb VncViewer::pixelFromBuffer(const QByteArray &data, int offset) {
    int bpp = m_rawBpp;
    if (offset < 0 || offset + bpp > data.size()) return 0;
    quint32 pixel = 0;
    if (m_bigEndian) {
        for (int i = 0; i < bpp; i++)
            pixel = (pixel << 8) | (quint8)data[offset + i];
    } else {
        for (int i = bpp - 1; i >= 0; i--)
            pixel = (pixel << 8) | (quint8)data[offset + i];
    }
    quint8 r = (pixel >> m_redShift) & 0xFF;
    quint8 g = (pixel >> m_greenShift) & 0xFF;
    quint8 b = (pixel >> m_blueShift) & 0xFF;
    return qRgb(r, g, b);
}

int VncViewer::readHextileRect(int x, int y, int w, int h) {
    if (m_framebuffer.isNull()) return 0;
    int bpp = m_rawBpp;
    int pos = 0;
    int tilesX = (w + 15) / 16;
    int tilesY = (h + 15) / 16;
    int bpl = m_framebuffer.bytesPerLine();

    for (int ty = 0; ty < tilesY; ty++) {
        for (int tx = 0; tx < tilesX; tx++) {
            int tileX = x + tx * 16;
            int tileY = y + ty * 16;
            int tileW = qMin(16, w - tx * 16);
            int tileH = qMin(16, h - ty * 16);

            if (pos + 1 > m_buffer.size()) return -1;
            quint8 subenc = (quint8)m_buffer[pos++];

            if (subenc & 1) {
                int rawBytes = tileW * tileH * bpp;
                if (pos + rawBytes > m_buffer.size()) return -1;
                for (int row = 0; row < tileH; row++) {
                    int py = tileY + row;
                    if (py < 0 || py >= m_fbHeight) continue;
                    uint *dst = (uint*)(m_framebuffer.bits() + py * bpl);
                    for (int col = 0; col < tileW; col++) {
                        int px = tileX + col;
                        if (px < 0 || px >= m_fbWidth) continue;
                        int off = (row * tileW + col) * bpp;
                        dst[px] = pixelFromBuffer(m_buffer, pos + off);
                    }
                }
                pos += rawBytes;
                continue;
            }

            QRgb bg = 0;
            if (subenc & 2) {
                if (pos + bpp > m_buffer.size()) return -1;
                bg = pixelFromBuffer(m_buffer, pos);
                pos += bpp;
            }

            for (int row = 0; row < tileH; row++) {
                int py = tileY + row;
                if (py < 0 || py >= m_fbHeight) continue;
                uint *dst = (uint*)(m_framebuffer.bits() + py * bpl);
                for (int col = 0; col < tileW; col++) {
                    int px = tileX + col;
                    if (px < 0 || px >= m_fbWidth) continue;
                    dst[px] = bg;
                }
            }

            QRgb fg = 0;
            if (subenc & 4) {
                if (pos + bpp > m_buffer.size()) return -1;
                fg = pixelFromBuffer(m_buffer, pos);
                pos += bpp;
            }

            if (subenc & 8) {
                if (pos + 1 > m_buffer.size()) return -1;
                int nSub = (quint8)m_buffer[pos++];
                for (int s = 0; s < nSub; s++) {
                    if (pos + 2 > m_buffer.size()) return -1;
                    quint8 xy = (quint8)m_buffer[pos];
                    quint8 wh = (quint8)m_buffer[pos + 1];
                    pos += 2;
                    int sx = (xy >> 4) & 0xF;
                    int sy = xy & 0xF;
                    int sw = ((wh >> 4) & 0xF) + 1;
                    int sh = (wh & 0xF) + 1;

                    QRgb subFg = fg;
                    if (subenc & 16) {
                        if (pos + bpp > m_buffer.size()) return -1;
                        subFg = pixelFromBuffer(m_buffer, pos);
                        pos += bpp;
                    }

                    for (int row = 0; row < sh; row++) {
                        int py = tileY + sy + row;
                        if (py < 0 || py >= m_fbHeight) continue;
                        uint *dst = (uint*)(m_framebuffer.bits() + py * bpl);
                        for (int col = 0; col < sw; col++) {
                            int px = tileX + sx + col;
                            if (px < 0 || px >= m_fbWidth) continue;
                            dst[px] = subFg;
                        }
                    }
                }
            }
        }
    }

    return pos;
}

void VncViewer::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    if (!m_connected) {
        p.fillRect(rect(), palette().window());
        p.setPen(palette().text().color());
        QString msg;
        if (m_reconnectAttempts > 0 && m_reconnectAttempts <= MaxReconnectAttempts)
            msg = QString("Connecting (attempt %1/%2)...").arg(m_reconnectAttempts).arg(MaxReconnectAttempts);
        else if (m_reconnectAttempts > MaxReconnectAttempts)
            msg = "Connection failed";
        else if (m_state == WaitingVersion)
            msg = "Contacting display server...";
        else if (m_state == WaitingAuth || m_state == WaitingAuthResult)
            msg = "Authenticating...";
        else if (m_state == WaitingServerInit)
            msg = "Initializing display...";
        else
            msg = "No display";
        p.drawText(rect(), Qt::AlignCenter, msg);
        return;
    }

    if (m_framebuffer.isNull()) {
        p.fillRect(rect(), palette().window());
        p.setPen(palette().text().color());
        p.drawText(rect(), Qt::AlignCenter, "Waiting for framebuffer...");
        return;
    }

    if (m_scaleToFit) {
        p.drawImage(rect(), m_framebuffer, m_framebuffer.rect());
    } else {
        int ox = qMax(0, (width() - (int)m_fbWidth) / 2);
        int oy = qMax(0, (height() - (int)m_fbHeight) / 2);
        p.drawImage(ox, oy, m_framebuffer);
    }
}

void VncViewer::mouseMoveEvent(QMouseEvent *event) {
    if (!m_connected) return;
    if (m_scaleToFit) {
        m_mouseX = event->position().x() * m_fbWidth / width();
        m_mouseY = event->position().y() * m_fbHeight / height();
    } else {
        m_mouseX = event->position().x() - qMax(0, (width() - (int)m_fbWidth) / 2);
        m_mouseY = event->position().y() - qMax(0, (height() - (int)m_fbHeight) / 2);
    }
    m_mouseX = qBound(0, m_mouseX, (int)m_fbWidth - 1);
    m_mouseY = qBound(0, m_mouseY, (int)m_fbHeight - 1);
    sendPointerEvent(m_mouseX, m_mouseY, m_buttonMask);
}

void VncViewer::mousePressEvent(QMouseEvent *event) {
    if (!m_connected) return;
    if (event->button() == Qt::LeftButton) m_buttonMask |= 1;
    if (event->button() == Qt::MiddleButton) m_buttonMask |= 2;
    if (event->button() == Qt::RightButton) m_buttonMask |= 4;
    mouseMoveEvent(event);
}

void VncViewer::mouseReleaseEvent(QMouseEvent *event) {
    if (!m_connected) return;
    if (event->button() == Qt::LeftButton) m_buttonMask &= ~1;
    if (event->button() == Qt::MiddleButton) m_buttonMask &= ~2;
    if (event->button() == Qt::RightButton) m_buttonMask &= ~4;
    mouseMoveEvent(event);
}

void VncViewer::keyPressEvent(QKeyEvent *event) {
    if (!m_connected) return;
    quint32 ks = keysymFromQt(event->key(), event->modifiers());
    if (ks) sendKeyEvent(ks, true);
}

void VncViewer::keyReleaseEvent(QKeyEvent *event) {
    if (!m_connected) return;
    quint32 ks = keysymFromQt(event->key(), event->modifiers());
    if (ks) sendKeyEvent(ks, false);
}

void VncViewer::sendKeyEvent(quint32 keysym, bool down) {
    QByteArray msg;
    msg.append((char)(down ? 4 : 5));
    msg.append((char)0);
    msg.append((keysym >> 24) & 0xFF);
    msg.append((keysym >> 16) & 0xFF);
    msg.append((keysym >> 8) & 0xFF);
    msg.append(keysym & 0xFF);
    m_socket->write(msg);
}

void VncViewer::sendPointerEvent(int x, int y, int buttonMask) {
    QByteArray msg;
    msg.append((char)5);
    msg.append((char)buttonMask);
    msg.append((x >> 8) & 0xFF);
    msg.append(x & 0xFF);
    msg.append((y >> 8) & 0xFF);
    msg.append(y & 0xFF);
    m_socket->write(msg);
}

quint32 VncViewer::keysymFromQt(int qtKey, Qt::KeyboardModifiers mods) {
    if (qtKey == Qt::Key_Shift) return 0xFFE1;
    if (qtKey == Qt::Key_Control) return 0xFFE3;
    if (qtKey == Qt::Key_Alt) return 0xFFE9;
    if (qtKey == Qt::Key_Meta) return 0xFFE7;
    if (qtKey == Qt::Key_Super_L) return 0xFFEB;
    if (qtKey == Qt::Key_Super_R) return 0xFFEC;
    if (qtKey == Qt::Key_Return || qtKey == Qt::Key_Enter) return 0xFF0D;
    if (qtKey == Qt::Key_Backspace) return 0xFF08;
    if (qtKey == Qt::Key_Tab) return 0xFF09;
    if (qtKey == Qt::Key_Escape) return 0xFF1B;
    if (qtKey == Qt::Key_Delete) return 0xFFFF;
    if (qtKey == Qt::Key_Home) return 0xFF50;
    if (qtKey == Qt::Key_End) return 0xFF57;
    if (qtKey == Qt::Key_Left) return 0xFF51;
    if (qtKey == Qt::Key_Up) return 0xFF52;
    if (qtKey == Qt::Key_Right) return 0xFF53;
    if (qtKey == Qt::Key_Down) return 0xFF54;
    if (qtKey == Qt::Key_PageUp) return 0xFF55;
    if (qtKey == Qt::Key_PageDown) return 0xFF56;
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12)
        return 0xFFBE + (qtKey - Qt::Key_F1);
    if (qtKey >= Qt::Key_Space && qtKey <= Qt::Key_AsciiTilde)
        return (quint32)qtKey;
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        if (mods & Qt::ShiftModifier)
            return qtKey;
        else
            return qtKey + 32;
    }
    return 0;
}
