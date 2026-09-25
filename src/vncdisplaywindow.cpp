#include "vncdisplaywindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QScreen>
#include <QGuiApplication>

VncDisplayWindow::VncDisplayWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_viewer(new VncViewer)
    , m_scaleBtn(new QPushButton("Fit"))
    , m_fullscreenBtn(new QPushButton("Fullscreen"))
{
    setWindowTitle("VM Display");
    setMinimumSize(640, 480);
    resize(1024, 768);

    setAttribute(Qt::WA_DeleteOnClose, false);

    m_scaleBtn->setCheckable(true);
    m_scaleBtn->setChecked(true);
    m_scaleBtn->setFixedWidth(60);

    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(4, 4, 4, 0);
    toolbar->addWidget(m_scaleBtn);
    toolbar->addWidget(m_fullscreenBtn);
    toolbar->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(toolbar);
    layout->addWidget(m_viewer);

    connect(m_viewer, &VncViewer::connected, this, &VncDisplayWindow::onConnected);
    connect(m_viewer, &VncViewer::disconnected, this, &VncDisplayWindow::onDisconnected);
    connect(m_viewer, &VncViewer::connectionFailed, this, &VncDisplayWindow::onConnectionFailed);
    connect(m_scaleBtn, &QPushButton::clicked, this, &VncDisplayWindow::toggleScaleToFit);
    connect(m_fullscreenBtn, &QPushButton::clicked, this, &VncDisplayWindow::toggleFullscreen);
}

VncDisplayWindow::~VncDisplayWindow() = default;

void VncDisplayWindow::setPassword(const QString &password) {
    m_viewer->setPassword(password);
}

void VncDisplayWindow::connectToHost(const QString &host, quint16 port) {
    m_viewer->connectToHost(host, port);
    setWindowTitle(m_vmName.isEmpty() ? "VM Display" : m_vmName + " - Connecting...");
}

void VncDisplayWindow::disconnectFromHost() {
    m_viewer->disconnectFromHost();
}

bool VncDisplayWindow::isConnected() const {
    return m_viewer->isConnected();
}

void VncDisplayWindow::setScaleToFit(bool scale) {
    m_viewer->setScaleToFit(scale);
    m_scaleBtn->setChecked(scale);
}

bool VncDisplayWindow::scaleToFit() const {
    return m_viewer->scaleToFit();
}

void VncDisplayWindow::setVmName(const QString &name) {
    m_vmName = name;
}

void VncDisplayWindow::closeEvent(QCloseEvent *event) {
    hide();
    event->ignore();
}

void VncDisplayWindow::onConnected() {
    QString title = m_vmName.isEmpty() ? "VM Display" : m_vmName;
    setWindowTitle(title + " - Connected");

    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        QSize screenSize = screen->availableSize();
        int maxW = screenSize.width() * 4 / 5;
        int maxH = screenSize.height() * 4 / 5;
        int w = qMin(width(), maxW);
        int h = qMin(height(), maxH);
        resize(w, h);
    }

    emit connected();
}

void VncDisplayWindow::onDisconnected() {
    setWindowTitle(m_vmName.isEmpty() ? "VM Display" : m_vmName + " - Disconnected");
    emit disconnected();
}

void VncDisplayWindow::onConnectionFailed(const QString &err) {
    setWindowTitle(m_vmName.isEmpty() ? "VM Display" : m_vmName + " - Connection Failed");
    emit connectionFailed(err);
}

void VncDisplayWindow::toggleScaleToFit() {
    m_viewer->setScaleToFit(!m_viewer->scaleToFit());
    m_scaleBtn->setChecked(m_viewer->scaleToFit());
}

void VncDisplayWindow::toggleFullscreen() {
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}
