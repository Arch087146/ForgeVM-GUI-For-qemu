#include "mainwindow.h"
#include "vmcreatordialog.h"
#include "appsettingsdialog.h"

#include <QApplication>
#include <QPainter>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QProcess>
#include <QFileInfo>
#include <QDateTime>
#include <QScrollBar>
#include <QDialogButtonBox>
#include <QStyleHints>
#include <QFrame>
#include <QDesktopServices>
#include <QUrl>
#include <QMenuBar>
#include <QStatusBar>
#include <QInputDialog>
#include <QIcon>
#include <QProgressBar>
#include <QTimer>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QKeySequence>
#include <QMenu>
#include <QMimeData>
#include <QDropEvent>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

static QString formatSizeMB(qint64 mb) {
    if (mb >= 1024)
        return QString("%1 GB").arg(mb / 1024.0, 0, 'f', mb >= 10240 ? 1 : 2);
    return QString("%1 MB").arg(mb);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_manager = new VMManager(this);

    detectSystemTheme();
    setupUI();
    applyStyles();
    refreshVMList();

    connect(m_manager, &VMManager::configListChanged, this, &MainWindow::refreshVMList);
    connect(m_manager, &VMManager::vmStateChanged, this, &MainWindow::onVMStateChanged);
    connect(m_manager, &VMManager::vmError, this, [this](const QString &id, const QString &error) {
        QString msg = QString("VM [%1] error: %2").arg(id, error);
        statusBar()->showMessage(msg, 5000);
        if (id == selectedVMId())
            m_consoleWidget->appendOutput("\n*** ERROR: " + error + "\n");
        if (m_aiDock->isVisible()) {
            m_aiWidget->appendMessage("user",
                QString("My VM \"%1\" got this error: %2\nCan you help me fix it?")
                    .arg(id, error));
        }
    });

    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged,
            this, &MainWindow::onThemeChanged);

    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(1000);
    connect(m_updateTimer, &QTimer::timeout, this, &MainWindow::updateDetailsPanel);
    m_updateTimer->start();

    setupTrayIcon();
}

MainWindow::~MainWindow() {
    m_updateTimer->stop();
    m_manager->stopAll();
}

void MainWindow::detectSystemTheme() {
    auto scheme = QApplication::styleHints()->colorScheme();
    m_darkTheme = (scheme == Qt::ColorScheme::Dark);
}

void MainWindow::onThemeChanged() {
    detectSystemTheme();
    applyStyles();
    if (m_aiWidget) m_aiWidget->setDarkMode(m_darkTheme);
}

void MainWindow::setupUI() {
    setWindowTitle("VM Manager");
    resize(1200, 750);
    setMinimumSize(900, 550);

    setupToolbar();
    setupAI();

    m_statusVMsLabel = new QLabel("0 VMs");
    m_statusVMsLabel->setStyleSheet("padding: 0 8px; font-size: 12px; opacity: 0.7;");
    statusBar()->addPermanentWidget(m_statusVMsLabel);

    auto *central = new QWidget;
    setCentralWidget(central);
    auto *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setupSidebar();
    setupEmptyState();
    setupDetailsPanel();

    m_contentStack = new QStackedWidget;
    m_contentStack->addWidget(m_detailsPage);
    m_contentStack->addWidget(m_emptyPage);
    m_contentStack->setCurrentWidget(m_emptyPage);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(m_sidebarWidget);
    splitter->addWidget(m_contentStack);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({260, 940});
    splitter->setHandleWidth(1);

    mainLayout->addWidget(splitter);

    updateActionStates();
}

void MainWindow::setupToolbar() {
    m_toolbar = addToolBar("Main");
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(16, 16));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_newAction = m_toolbar->addAction("New VM");
    m_newAction->setToolTip("Create a new virtual machine (Ctrl+N)");
    connect(m_newAction, &QAction::triggered, this, &MainWindow::onNewVM);

    m_importAction = m_toolbar->addAction("Import");
    m_importAction->setToolTip("Import a VM from disk image or other format (Ctrl+O)");
    connect(m_importAction, &QAction::triggered, this, &MainWindow::onImportVM);

    m_exportAction = m_toolbar->addAction("Export");
    m_exportAction->setToolTip("Export VM configuration and disk (Ctrl+Shift+E)");
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::onExportVM);

    // Home button
    QAction *homeAction = m_toolbar->addAction("Home");
    homeAction->setToolTip("Return to home view");
    connect(homeAction, &QAction::triggered, this, [this]() {
        m_vmList->clearSelection();
        m_contentStack->setCurrentWidget(m_emptyPage);
    });

    m_toolbar->addSeparator();

    m_startAction = m_toolbar->addAction("Start");
    connect(m_startAction, &QAction::triggered, this, &MainWindow::onStartVM);

    m_stopAction = m_toolbar->addAction("Stop");
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::onStopVM);

    m_pauseResumeAction = m_toolbar->addAction("Pause");
    connect(m_pauseResumeAction, &QAction::triggered, this, &MainWindow::onPauseResumeVM);

    auto *forceStopAction = m_toolbar->addAction("Force Stop");
    connect(forceStopAction, &QAction::triggered, this, &MainWindow::onForceStopVM);

    m_toolbar->addSeparator();

    m_settingsAction = m_toolbar->addAction("Settings");
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::onEditVM);

    auto *cloneAction = m_toolbar->addAction("Clone");
    connect(cloneAction, &QAction::triggered, this, &MainWindow::onCloneVM);

    m_deleteAction = m_toolbar->addAction("Delete");
    connect(m_deleteAction, &QAction::triggered, this, &MainWindow::onDeleteVM);

    m_toolbar->addSeparator();

    auto *snapAction = m_toolbar->addAction("Snapshots");
    connect(snapAction, &QAction::triggered, this, &MainWindow::onSnapshots);

    auto *prebuiltAction = m_toolbar->addAction("Pre-built");
    connect(prebuiltAction, &QAction::triggered, this, &MainWindow::onPrebuiltVM);

    m_toolbar->addSeparator();

    m_aiAction = m_toolbar->addAction("AI");
    m_aiAction->setCheckable(true);
    connect(m_aiAction, &QAction::triggered, this, &MainWindow::onShowAI);

    // Tooltips and keyboard shortcuts
    m_newAction->setToolTip("Create a new virtual machine (Ctrl+N)");
    m_importAction->setToolTip("Import a VM from disk image or other format (Ctrl+O)");
    m_exportAction->setToolTip("Export VM configuration and disk (Ctrl+Shift+E)");
    m_startAction->setToolTip("Start the selected VM (Ctrl+R)");
    m_stopAction->setToolTip("Stop the selected VM (Ctrl+Shift+S)");
    m_pauseResumeAction->setToolTip("Pause or resume the selected VM (Ctrl+P)");
    forceStopAction->setToolTip("Force stop the selected VM");
    m_settingsAction->setToolTip("Configure selected VM (Ctrl+E)");
    cloneAction->setToolTip("Clone the selected VM");
    m_deleteAction->setToolTip("Delete the selected VM (Delete)");
    snapAction->setToolTip("Manage snapshots for the selected VM");
    prebuiltAction->setToolTip("Download and create from pre-built VM image");
    m_aiAction->setToolTip("Toggle AI Assistant panel");

    m_newAction->setShortcut(QKeySequence("Ctrl+N"));
    m_startAction->setShortcut(QKeySequence("Ctrl+R"));
    m_stopAction->setShortcut(QKeySequence("Ctrl+Shift+S"));
    m_pauseResumeAction->setShortcut(QKeySequence("Ctrl+P"));
    m_settingsAction->setShortcut(QKeySequence("Ctrl+E"));
    m_deleteAction->setShortcut(QKeySequence::Delete);
    m_importAction->setShortcut(QKeySequence("Ctrl+O"));
    m_exportAction->setShortcut(QKeySequence("Ctrl+Shift+E"));

    // Extra shortcuts
    m_startAction->setShortcut(QKeySequence("F5"));
    m_stopAction->setShortcut(QKeySequence("Shift+F5"));

    // Additional shortcuts
    QAction *renameAction = m_toolbar->addAction("Rename");
    renameAction->setToolTip("Rename selected VM (F2)");
    renameAction->setShortcut(QKeySequence("F2"));
    connect(renameAction, &QAction::triggered, this, &MainWindow::onRenameVM);

    QAction *duplicateAction = m_toolbar->addAction("Duplicate");
    duplicateAction->setToolTip("Duplicate selected VM (Ctrl+D)");
    duplicateAction->setShortcut(QKeySequence("Ctrl+D"));
    connect(duplicateAction, &QAction::triggered, this, &MainWindow::onCloneVM);

    // Refresh shortcut
    QAction *refreshAction = m_toolbar->addAction("Refresh");
    refreshAction->setToolTip("Refresh VM list (F5)");
    refreshAction->setShortcut(QKeySequence("F5"));
    connect(refreshAction, &QAction::triggered, this, [this]() {
        refreshVMList();
        statusBar()->showMessage("VM list refreshed", 2000);
    });

    // File menu
    auto *fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_importAction);
    fileMenu->addAction(m_exportAction);
    fileMenu->addSeparator();
    auto *utmImportAction = fileMenu->addAction("Import UTM VM...");
    connect(utmImportAction, &QAction::triggered, this, &MainWindow::onImportUTM);
    auto *convertAction = fileMenu->addAction("Convert Disk Image...");
    connect(convertAction, &QAction::triggered, this, &MainWindow::onTools);
    fileMenu->addSeparator();
    auto *prefsAction = fileMenu->addAction("Preferences...");
    prefsAction->setShortcut(QKeySequence::Preferences);
    connect(prefsAction, &QAction::triggered, this, &MainWindow::onPreferences);
    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction("Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    // View menu
    auto *viewMenu = menuBar()->addMenu("View");

    m_toggleSidebarAction = viewMenu->addAction("Sidebar");
    m_toggleSidebarAction->setCheckable(true);
    m_toggleSidebarAction->setChecked(true);
    connect(m_toggleSidebarAction, &QAction::triggered, this, &MainWindow::toggleSidebar);

    auto *toggleAI = viewMenu->addAction("AI Panel");
    toggleAI->setCheckable(true);
    toggleAI->setToolTip("Toggle AI Assistant panel (Ctrl+Shift+I)");
    toggleAI->setShortcut(QKeySequence("Ctrl+Shift+I"));
    connect(toggleAI, &QAction::triggered, this, &MainWindow::toggleAI);

    m_toggleToolbarAction = viewMenu->addAction("Toolbar");
    m_toggleToolbarAction->setCheckable(true);
    m_toggleToolbarAction->setChecked(true);
    connect(m_toggleToolbarAction, &QAction::triggered, this, &MainWindow::toggleToolbar);

    // Help menu
    auto *helpMenu = menuBar()->addMenu("Help");
    auto *guideAction = helpMenu->addAction("User Guide");
    guideAction->setShortcut(QKeySequence("F1"));
    connect(guideAction, &QAction::triggered, this, &MainWindow::onUserGuide);
    auto *aboutHelpAction = helpMenu->addAction("About VM Manager");
    connect(aboutHelpAction, &QAction::triggered, this, &MainWindow::onAbout);
    helpMenu->addSeparator();
    auto *checkUpdates = helpMenu->addAction("Check for Updates");
    connect(checkUpdates, &QAction::triggered, this, [this]() {
        QDesktopServices::openUrl(QUrl("https://github.com/Kilo-Org/qemu-vm-manager/releases"));
    });
}

void MainWindow::setupSidebar() {
    m_sidebarWidget = new QWidget;
    auto *sidebarLayout = new QVBoxLayout(m_sidebarWidget);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    auto *searchContainer = new QWidget;
    auto *searchLayout = new QHBoxLayout(searchContainer);
    searchLayout->setContentsMargins(8, 8, 8, 8);

    m_filterEdit = new QLineEdit;
    m_filterEdit->setPlaceholderText("Search VMs...");
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setObjectName("vmSearch");
    connect(m_filterEdit, &QLineEdit::textChanged, this, &MainWindow::filterVMList);
    searchLayout->addWidget(m_filterEdit, 1);

    sidebarLayout->addWidget(searchContainer);

    m_vmList = new QTreeWidget;
    m_vmList->setMinimumWidth(200);
    m_vmList->setMaximumWidth(350);
    m_vmList->setFocusPolicy(Qt::NoFocus);
    m_vmList->setIconSize(QSize(28, 28));
    m_vmList->setHeaderHidden(true);
    m_vmList->setIndentation(16);
    m_vmList->setAnimated(true);
    m_vmList->setRootIsDecorated(false);
    m_vmList->setExpandsOnDoubleClick(false);

    m_vmList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_vmList->setDragDropMode(QAbstractItemView::DropOnly);
    m_vmList->setAcceptDrops(true);
    connect(m_vmList, &QWidget::customContextMenuRequested, this, &MainWindow::onVMContextMenu);
    connect(m_vmList, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *, QTreeWidgetItem *) {
        onVMSelected();
    });
    m_vmList->viewport()->setAcceptDrops(true);
    m_vmList->installEventFilter(this);
    sidebarLayout->addWidget(m_vmList, 1);
}

void MainWindow::setupAI() {
    m_aiDock = new QDockWidget("AI Assistant", this);
    m_aiDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_aiDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    m_aiWidget = new AIChatWidget;
    m_aiWidget->setDarkMode(m_darkTheme);
    m_aiDock->setWidget(m_aiWidget);
    m_aiDock->setMinimumWidth(300);
    m_aiDock->resize(380, 500);
    addDockWidget(Qt::RightDockWidgetArea, m_aiDock);
    m_aiDock->hide();

    connect(m_aiWidget, &AIChatWidget::requestSent, this, [this]() {
        m_aiDock->show();
        m_aiAction->setChecked(true);
        m_aiWidget->setFocus();
    });

    connect(m_aiWidget, &AIChatWidget::configGenerated,
            this, &MainWindow::onNewVMFromConfig);
    connect(m_aiWidget, &AIChatWidget::statusMessage, this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 3000);
    });
}

void MainWindow::setupDetailsPanel() {
    m_detailsPage = new QWidget;
    auto *detailsLayout = new QVBoxLayout(m_detailsPage);
    detailsLayout->setContentsMargins(20, 16, 20, 16);
    detailsLayout->setSpacing(10);

    // Header
    auto *headerWidget = new QWidget;
    headerWidget->setObjectName("detailHeader");
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);

    m_vmIconLabel = new QLabel;
    m_vmIconLabel->setFixedSize(48, 48);
    m_vmIconLabel->setScaledContents(true);
    headerLayout->addWidget(m_vmIconLabel);
    headerLayout->addSpacing(12);

    m_vmNameLabel = new QLabel;
    m_vmNameLabel->setStyleSheet("font-size: 24px; font-weight: bold;");
    headerLayout->addWidget(m_vmNameLabel);
    headerLayout->addStretch();

    m_vmStatusLabel = new QLabel;
    headerLayout->addWidget(m_vmStatusLabel);
    detailsLayout->addWidget(headerWidget);

    // Startup progress bar
    m_vmProgressBar = new QProgressBar;
    m_vmProgressBar->setRange(0, 0);
    m_vmProgressBar->setTextVisible(false);
    m_vmProgressBar->setFixedHeight(4);
    m_vmProgressBar->setObjectName("vmProgressBar");
    m_vmProgressBar->hide();
    detailsLayout->addWidget(m_vmProgressBar);

    // Info cards with resource bars
    m_detailsInfoPanel = new QWidget;
    m_detailsInfoPanel->setObjectName("infoCards");
    auto *infoGrid = new QGridLayout(m_detailsInfoPanel);
    infoGrid->setContentsMargins(0, 0, 0, 0);
    infoGrid->setSpacing(1);

    auto makeInfoCard = [](const QString &title, QLabel *&value, QProgressBar *&bar) -> QWidget* {
        auto *card = new QFrame;
        card->setObjectName("infoCard");
        card->setFrameShape(QFrame::StyledPanel);
        auto *vl = new QVBoxLayout(card);
        vl->setContentsMargins(14, 10, 14, 10);
        vl->setSpacing(4);
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        auto *tl = new QLabel(title);
        tl->setObjectName("infoCardTitle");
        value = new QLabel("-");
        value->setObjectName("infoCardValue");
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(tl);
        row->addWidget(value, 1);
        vl->addLayout(row);
        bar = new QProgressBar;
        bar->setRange(0, 100);
        bar->setTextVisible(false);
        bar->setFixedHeight(4);
        bar->setObjectName("infoCardBar");
        bar->hide();
        vl->addWidget(bar);
        return card;
    };

    infoGrid->addWidget(makeInfoCard("CPU", m_vmCpuLabel, m_vmCpuBar), 0, 0);
    infoGrid->addWidget(makeInfoCard("Memory", m_vmMemoryLabel, m_vmMemoryBar), 0, 1);
    infoGrid->addWidget(makeInfoCard("Disk", m_vmDiskLabel, m_vmDiskBar), 0, 2);
    infoGrid->addWidget(makeInfoCard("Network", m_vmNetworkLabel, m_vmNetworkBar), 1, 0);
    infoGrid->addWidget(makeInfoCard("Display", m_vmDisplayLabel, m_vmDisplayBar), 1, 1);
    infoGrid->addWidget(makeInfoCard("Uptime", m_vmUptimeLabel, m_vmUptimeBar), 1, 2);
    detailsLayout->addWidget(m_detailsInfoPanel);

    // VM control buttons in a styled bar
    m_vmControlBar = new QFrame;
    m_vmControlBar->setObjectName("vmControlBar");
    auto *vmControlRow = new QHBoxLayout(m_vmControlBar);
    vmControlRow->setContentsMargins(12, 8, 12, 8);
    vmControlRow->setSpacing(6);

    m_vmStartBtn = new QPushButton("\xE2\x96\xB6 Start");
    m_vmStartBtn->setObjectName("actionBtn");
    m_vmStartBtn->setToolTip("Start this VM (Ctrl+R)");
    connect(m_vmStartBtn, &QPushButton::clicked, this, &MainWindow::onStartVM);

    m_vmStopBtn = new QPushButton("\xE2\x96\xA0 Stop");
    m_vmStopBtn->setObjectName("actionBtnDanger");
    m_vmStopBtn->setToolTip("Stop this VM (Ctrl+Shift+S)");
    connect(m_vmStopBtn, &QPushButton::clicked, this, &MainWindow::onStopVM);

    m_vmPauseBtn = new QPushButton("\xE2\x9D\xB4 Pause");
    m_vmPauseBtn->setObjectName("actionBtn");
    m_vmPauseBtn->setToolTip("Pause or resume this VM (Ctrl+P)");
    connect(m_vmPauseBtn, &QPushButton::clicked, this, &MainWindow::onPauseResumeVM);

    m_vmForceStopBtn = new QPushButton("\xE2\x8F\xB9 Force Stop");
    m_vmForceStopBtn->setObjectName("actionBtnDanger");
    m_vmForceStopBtn->setFixedWidth(120);
    m_vmForceStopBtn->setToolTip("Force kill this VM process");
    connect(m_vmForceStopBtn, &QPushButton::clicked, this, &MainWindow::onForceStopVM);

    vmControlRow->addWidget(m_vmStartBtn);
    vmControlRow->addWidget(m_vmStopBtn);
    vmControlRow->addWidget(m_vmPauseBtn);
    vmControlRow->addWidget(m_vmForceStopBtn);
    vmControlRow->addStretch();

    detailsLayout->addWidget(m_vmControlBar);

    // Display controls
    auto *displayRow = new QHBoxLayout;
    displayRow->setSpacing(6);

    auto *displayLabel = new QLabel("Display:");
    displayLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #888;");
    displayRow->addWidget(displayLabel);

    m_displayModeCombo = new QComboBox;
    m_displayModeCombo->addItems({"Embedded Tab", "External VNC", "GTK Window"});
    m_displayModeCombo->setToolTip("Display mode: built-in viewer or external window");
    m_displayModeCombo->setFixedWidth(150);
    displayRow->addWidget(m_displayModeCombo);

    m_vncViewerBtn = new QPushButton("Open Display");
    m_vncViewerBtn->setObjectName("actionBtn");
    connect(m_vncViewerBtn, &QPushButton::clicked, this, [this]() {
        QString mode = m_displayModeCombo->currentText();
        if (mode == "Embedded Tab") {
            m_rightTabs->setCurrentWidget(m_vncViewer);
            if (!m_vncViewer->isConnected())
                connectVNCViewer();
        } else {
            openVNCViewer();
        }
    });
    displayRow->addWidget(m_vncViewerBtn);

    auto *askAIBtn = new QPushButton("Ask AI");
    askAIBtn->setObjectName("actionBtnTertiary");
    connect(askAIBtn, &QPushButton::clicked, this, [this]() {
        onShowAI();
        QString id = selectedVMId();
        if (!id.isEmpty()) {
            auto cfg = m_manager->getConfig(id);
            m_aiWidget->appendMessage("user",
                QString("Help me with my VM \"%1\":\n"
                        "- OS: %2\n- CPU: %3 cores (%4)\n- RAM: %5\n"
                        "- Disk: %6\n- Display: %7")
                    .arg(cfg.name, cfg.osType)
                    .arg(cfg.cpuCores).arg(cfg.cpuModel)
                    .arg(formatSizeMB(cfg.memoryMB))
                    .arg(cfg.disk.path.isEmpty() ? "None" : cfg.disk.path)
                    .arg(cfg.displayMode)
                    + (cfg.productKey.isEmpty() ? "" : "\n- Product Key: " + cfg.productKey));
        }
    });
    displayRow->addWidget(askAIBtn);
    displayRow->addStretch();

    detailsLayout->addLayout(displayRow);

    // Right tabs: Console + Display
    m_rightTabs = new QTabWidget;
    m_rightTabs->setObjectName("vmTabs");
    m_rightTabs->setDocumentMode(true);

    m_consoleWidget = new ConsoleWidget;
    m_rightTabs->addTab(m_consoleWidget, "Console");

    m_vncViewer = new VncViewer;
    m_vncViewer->setObjectName("vmDisplay");
    m_rightTabs->addTab(m_vncViewer, "Display");

    m_vncDisplayWindow = new VncDisplayWindow(this);

    connect(m_consoleWidget, &ConsoleWidget::commandSent, this, [this](const QString &cmd) {
        auto *task = m_manager->taskForVM(selectedVMId());
        if (task) task->sendSerialText(cmd);
    });

    connect(m_vncViewer, &VncViewer::connected, this, [this]() {
        m_rightTabs->setTabText(1, "Display (Connected)");
    });
    connect(m_vncViewer, &VncViewer::disconnected, this, [this]() {
        m_rightTabs->setTabText(1, "Display");
    });
    connect(m_vncViewer, &VncViewer::connectionFailed, this, [this](const QString &err) {
        statusBar()->showMessage("VNC: " + err, 5000);
        m_rightTabs->setTabText(1, "Display (Disconnected)");
    });

    connect(m_vncDisplayWindow, &VncDisplayWindow::connected, this, [this]() {
    });
    connect(m_vncDisplayWindow, &VncDisplayWindow::disconnected, this, [this]() {
    });
    connect(m_vncDisplayWindow, &VncDisplayWindow::connectionFailed, this, [this](const QString &err) {
        statusBar()->showMessage("VNC: " + err, 5000);
    });

    detailsLayout->addWidget(m_rightTabs, 1);

    // Notes
    m_vmNotesLabel = new QLabel;
    m_vmNotesLabel->setWordWrap(true);
    m_vmNotesLabel->setObjectName("vmNotes");
    m_vmNotesLabel->setVisible(false);
    detailsLayout->addWidget(m_vmNotesLabel);
}

void MainWindow::setupEmptyState() {
    m_emptyPage = new QWidget;
    auto *emptyLayout = new QVBoxLayout(m_emptyPage);
    emptyLayout->setAlignment(Qt::AlignCenter);
emptyLayout->setSpacing(12);

    auto *iconLabel = new QLabel("\xF0\x9F\x96\xA5");
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet("font-size: 36px;");
    emptyLayout->addWidget(iconLabel);

    auto *welcomeLabel = new QLabel("ForgeVM");
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLabel->setStyleSheet("font-size: 24px; font-weight: bold;");
    emptyLayout->addWidget(welcomeLabel);

    auto *subtitleLabel = new QLabel("Manage and run QEMU virtual machines");
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setStyleSheet("font-size: 13px; opacity: 0.6;");
    emptyLayout->addWidget(subtitleLabel);

    emptyLayout->addSpacing(12);

    // Cards row
    auto *cardsWidget = new QWidget;
    cardsWidget->setObjectName("emptyCards");
    auto *cardsLayout = new QHBoxLayout(cardsWidget);
    cardsLayout->setSpacing(12);
    cardsLayout->setContentsMargins(20, 0, 20, 0);

    struct CardDef { QString icon; QString title; QString desc; QString btnText; std::function<void()> slot; };
    QList<CardDef> cards = {
        {"\xE2\x9E\x95", "New VM", "Create from scratch", "Create", [this]() { onNewVM(); }},
        {"\xE2\xAC\x87", "Presets", "Pre-configured templates", "Browse", [this]() { onPresetDownload(); }},
        {"\xE2\x96\xB6", "Pre-built", "Ready-to-run images", "Download", [this]() { onPrebuiltVM(); }},
        {"\xE2\x9D\x93", "Guide", "Tips & FAQ", "Open", [this]() { onUserGuide(); }},
        {"\xE2\x86\x93", "Import", "Import existing VM", "Import", [this]() { onImportVM(); }},
    };

    for (const auto &cd : cards) {
        auto *card = new QFrame;
        card->setObjectName("emptyCard");
        card->setFrameShape(QFrame::StyledPanel);
        card->setCursor(Qt::PointingHandCursor);
        card->setToolTip(cd.desc);
        auto *cl = new QVBoxLayout(card);
        cl->setAlignment(Qt::AlignCenter);
        cl->setContentsMargins(8, 10, 8, 8);
        cl->setSpacing(4);

        auto *ci = new QLabel(cd.icon);
        ci->setAlignment(Qt::AlignCenter);
        ci->setStyleSheet("font-size: 22px;");
        cl->addWidget(ci);

        auto *ct = new QLabel(cd.title);
        ct->setAlignment(Qt::AlignCenter);
        ct->setStyleSheet("font-size: 13px; font-weight: bold;");
        ct->setWordWrap(true);
        cl->addWidget(ct);

        auto *cdLbl = new QLabel(cd.desc);
        cdLbl->setAlignment(Qt::AlignCenter);
        cdLbl->setStyleSheet("font-size: 10px; opacity: 0.6;");
        cdLbl->setWordWrap(true);
        cl->addWidget(cdLbl);

        auto *clickBtn = new QPushButton(cd.btnText);
        clickBtn->setObjectName("cardBtn");
        clickBtn->setCursor(Qt::PointingHandCursor);
        clickBtn->setToolTip(cd.desc);
        connect(clickBtn, &QPushButton::clicked, this, cd.slot);
        cl->addWidget(clickBtn, 0, Qt::AlignCenter);

        cardsLayout->addWidget(card);
    }

    emptyLayout->addWidget(cardsWidget);

    auto *hintLabel = new QLabel("Tip: Use the AI button in the toolbar for help creating VMs");
    hintLabel->setAlignment(Qt::AlignCenter);
    hintLabel->setStyleSheet("font-size: 12px; opacity: 0.4; margin-top: 8px;");
    emptyLayout->addWidget(hintLabel);
}

void MainWindow::applyStyles() {
    if (m_darkTheme) {
        applyDarkStyles();
        m_consoleWidget->setDarkMode(true);
    } else {
        applyLightStyles();
        m_consoleWidget->setDarkMode(false);
    }
}

void MainWindow::applyDarkStyles() {
    setStyleSheet(R"(
        QMainWindow { background: #0d0d1a; }
        QMainWindow::separator { background: #2a2a3e; width: 1px; }
        QToolBar {
            background: #12122a;
            border-bottom: 1px solid #2a2a3e;
            padding: 6px 8px;
            spacing: 6px;
        }
        QToolBar QToolButton {
            color: #c0c0c0;
            background: #1a1a3e;
            border: 1px solid #2a2a4e;
            padding: 6px 16px;
            border-radius: 6px;
            font-size: 12px;
            font-weight: bold;
            min-height: 22px;
        }
        QToolBar QToolButton:hover { background: #2a2a5e; border-color: #4a4a7e; color: #fff; }
        QToolBar QToolButton:pressed { background: #3a3a6e; }
        QToolBar QToolButton:disabled { color: #444; background: #12122a; border-color: #1a1a2e; }
        QDockWidget {
            color: #ccc;
            background: #12122a;
            border-left: 1px solid #2a2a3e;
            titlebar-close-icon: url(none);
        }
        QDockWidget::title {
            background: #12122a;
            padding: 8px 12px;
            border-bottom: 1px solid #2a2a3e;
        }
        #aiHeader { background: #12122a; border-bottom: 1px solid #2a2a3e; }
        #aiSettings { background: #0d0d1a; border-bottom: 1px solid #2a2a3e; }
        #aiInput { background: #12122a; border-top: 1px solid #2a2a3e; }
        QListWidget {
            background: #12122a;
            border: none;
            border-right: 1px solid #2a2a3e;
            outline: none;
            padding: 4px;
        }
        QListWidget::item {
            color: #c0c0c0;
            padding: 10px 14px;
            border-radius: 6px;
            margin: 2px 0;
        }
        QListWidget::item:selected { background: #2a2a5e; color: #fff; }
        QListWidget::item:hover:!selected { background: #1e1e3e; }
        QSplitter::handle { background: #2a2a3e; }
        #detailHeader { background: transparent; }
        #infoCards { background: transparent; }
        #infoCard {
            background: #12122a; border-radius: 4px; border: none;
        }
        #infoCardTitle {
            font-size: 11px; color: #888;
        }
        #infoCardValue { font-size: 13px; font-weight: bold; color: #e0e0e0; }
        #infoCardBar {
            background: #0d0d1a; border: none; border-radius: 2px; max-height: 4px;
        }
        #infoCardBar::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #7c7cf0, stop:1 #5bc0de);
            border-radius: 2px;
        }
        #vmProgressBar {
            background: transparent; border: none; max-height: 4px;
        }
        #vmProgressBar::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #7c7cf0, stop:0.5 #5bc0de, stop:1 #7c7cf0);
            border-radius: 2px;
        }
        #vmControlBar {
            background: #12122a; border: 1px solid #2a2a3e;
            border-radius: 6px;
        }
        QToolTip {
            background: #1a1a3e; color: #e0e0e0;
            border: 1px solid #3a3a5e; padding: 6px 10px;
            border-radius: 4px; font-size: 12px;
        }
        QTreeWidget {
            background: #12122a; border: none;
            border-right: 1px solid #2a2a3e; outline: none;
            padding: 4px;
        }
        QTreeWidget::item {
            color: #c0c0c0; padding: 8px 10px;
            border-radius: 6px; margin: 1px 4px;
        }
        QTreeWidget::item:selected {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #2a2a5e, stop:1 #22224a);
            color: #fff;
        }
        QTreeWidget::item:hover:!selected {
            background: #1e1e3e;
        }
        QScrollBar:horizontal {
            background: #0d0d1a; height: 8px; margin: 0;
        }
        QScrollBar::handle:horizontal {
            background: #3a3a5e; border-radius: 4px; min-width: 30px;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
        #actionBtn {
            background: #3a3a7e; color: #fff; border: none;
            padding: 8px 22px; border-radius: 6px; font-weight: bold; font-size: 13px;
        }
        #actionBtn:hover { background: #4a4a9e; }
        #actionBtn:pressed { background: #5a5aae; }
        #actionBtn:disabled { background: #2a2a3e; color: #666; }
        #actionBtnDanger {
            background: #7e2a2a; color: #fff; border: none;
            padding: 8px 22px; border-radius: 6px; font-weight: bold; font-size: 13px;
        }
        #actionBtnDanger:hover { background: #9e3a3a; }
        #actionBtnDanger:pressed { background: #ae4a4a; }
        #actionBtnDanger:disabled { background: #2a2a3e; color: #666; }
        #actionBtnSecondary {
            background: transparent; color: #aaa; border: 1px solid #3a3a4e;
            padding: 8px 22px; border-radius: 6px; font-size: 13px;
        }
        #actionBtnSecondary:hover { background: #2a2a3e; color: #fff; }
        #actionBtnTertiary {
            background: #2a2a4e; color: #b0b0e0; border: none;
            padding: 8px 18px; border-radius: 6px; font-size: 13px;
        }
        #actionBtnTertiary:hover { background: #3a3a6e; color: #fff; }
        #actionBtnTertiary:pressed { background: #4a4a8e; }
        #actionBtnTertiary:disabled { background: #1a1a2e; color: #555; }
        #vmNotes { color: #888; font-size: 12px; padding: 4px 0; }
        #emptyCards QFrame#emptyCard {
            background: #1a1a2e; border: 1px solid #2a2a3e;
            border-radius: 10px; padding: 8px 10px;
            min-width: 150px; max-width: 170px;
        }
        #emptyCard:hover { border-color: #4a4a7e; background: #1e1e3e; }
        #cardBtn {
            background: #3a3a7e; color: #fff; border: none;
            padding: 4px 14px; border-radius: 6px; font-size: 11px; font-weight: bold;
        }
        #cardBtn:hover { background: #4a4a9e; }
        QTextEdit {
            background: #0d0d1a; color: #e0e0e0;
            border: none; padding: 8px; font-size: 12px;
        }
        QTextEdit#aiMessages { background: #0d0d1a; }
        QLineEdit, QSpinBox, QComboBox, QPlainTextEdit {
            background: #1a1a2e; color: #e0e0e0;
            border: 1px solid #2a2a3e; border-radius: 4px;
            padding: 6px 10px; font-size: 13px;
        }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
            border-color: #5a5aae;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            border: none; background: #2a2a5e;
            width: 16px; border-radius: 2px; margin: 1px;
        }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: #3a3a7e; }
        QSpinBox::up-arrow { image: none; width: 0; height: 0; border-left: 5px solid transparent; border-right: 5px solid transparent; border-bottom: 5px solid #e0e0e0; }
        QSpinBox::down-arrow { image: none; width: 0; height: 0; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 5px solid #e0e0e0; }
        #vmSearch {
            background: transparent; border: 1px solid #2a2a3e;
            border-radius: 6px; padding: 6px 10px; font-size: 13px;
        }
        #vmSearch:focus { border-color: #5a5aae; }
        QComboBox::drop-down { border: none; padding-right: 8px; }
        QComboBox QAbstractItemView {
            background: #1a1a2e; color: #e0e0e0;
            border: 1px solid #2a2a3e;
            selection-background-color: #2a2a5e;
        }
        QCheckBox { color: #ccc; spacing: 8px; font-size: 13px; }
        QCheckBox::indicator {
            width: 16px; height: 16px;
            border: 1px solid #3a3a5e; border-radius: 3px;
            background: #1a1a2e;
        }
        QCheckBox::indicator:checked { background: #5a5aae; border-color: #5a5aae; }
        QPushButton {
            color: #e0e0e0; background: #2a2a5e;
            border: none; padding: 6px 16px; border-radius: 6px; font-size: 13px;
        }
        QPushButton:hover { background: #3a3a7e; color: #fff; }
        QPushButton:pressed { background: #4a4a8e; }
        QPushButton:disabled { background: #1a1a2e; color: #555; }
        QDialog { background: #0d0d1a; }
        QLabel { color: #ccc; }
        QStatusBar { background: #12122a; border-top: 1px solid #2a2a3e; color: #888; font-size: 12px; }
        QGroupBox {
            color: #ccc; border: 1px solid #2a2a3e;
            border-radius: 8px; margin-top: 14px; padding-top: 18px; font-size: 13px;
            background: transparent;
        }
        QGroupBox::title {
            subcontrol-origin: margin; left: 12px; padding: 0 8px; font-weight: bold;
        }
        QTabWidget::pane { background: transparent; border: none; }
        QTabWidget#vmTabs::pane { background: transparent; border: none; }
        QTabBar::tab {
            color: #888; background: transparent;
            padding: 10px 20px; border: none;
            border-bottom: 2px solid transparent; font-size: 12px;
            font-weight: bold; letter-spacing: 0.5px;
        }
        QTabBar::tab:selected { color: #fff; border-bottom: 2px solid #7c7cf0; }
        QTabBar::tab:hover:!selected { color: #ccc; }
        QDialog QTabWidget::pane { background: transparent; border: none; }
        QScrollBar:vertical {
            background: #0d0d1a; width: 8px; margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #3a3a5e; border-radius: 4px; min-height: 30px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QDialogButtonBox QPushButton { padding: 8px 28px; font-weight: bold; }
    )");
}

void MainWindow::applyLightStyles() {
    setStyleSheet(R"(
        QMainWindow { background: #f5f5f8; }
        QMainWindow::separator { background: #ddd; width: 1px; }
        QToolBar {
            background: #fff; border-bottom: 1px solid #e0e0e0;
            padding: 6px 8px; spacing: 6px;
        }
        QToolBar QToolButton {
            color: #444; background: #f5f5f8;
            border: 1px solid #ddd; padding: 6px 16px;
            border-radius: 6px; font-size: 12px; font-weight: bold; min-height: 22px;
        }
        QToolBar QToolButton:hover { background: #e8e8f0; border-color: #ccc; color: #222; }
        QToolBar QToolButton:pressed { background: #ddd; }
        QToolBar QToolButton:disabled { color: #bbb; background: #fafafe; border-color: #eee; }
        QDockWidget {
            color: #333; background: #fff;
            border-left: 1px solid #e0e0e0;
        }
        QDockWidget::title {
            background: #fff; padding: 8px 12px;
            border-bottom: 1px solid #e0e0e0;
        }
        #aiHeader { background: #fff; border-bottom: 1px solid #e0e0e0; }
        #aiSettings { background: #f5f5f8; border-bottom: 1px solid #e0e0e0; }
        #aiInput { background: #fff; border-top: 1px solid #e0e0e0; }
        QListWidget {
            background: #fff; border: none;
            border-right: 1px solid #e0e0e0; outline: none; padding: 4px;
        }
        QListWidget::item { color: #333; padding: 10px 14px; border-radius: 6px; margin: 2px 0; }
        QListWidget::item:selected { background: #e8e8f0; color: #222; font-weight: bold; }
        QListWidget::item:hover:!selected { background: #f0f0f5; }
        QSplitter::handle { background: #e0e0e0; }
        #detailHeader { background: transparent; }
        #infoCards { background: transparent; }
        #infoCard { background: #f8f8fc; border-radius: 4px; border: 1px solid #eee; }
        #infoCardTitle { font-size: 11px; color: #999; }
        #infoCardValue { font-size: 13px; font-weight: bold; color: #222; }
        #infoCardBar {
            background: #eee; border: none; border-radius: 2px; max-height: 4px;
        }
        #infoCardBar::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #6a6aaa, stop:1 #4a90d9);
            border-radius: 2px;
        }
        #vmProgressBar {
            background: transparent; border: none; max-height: 4px;
        }
        #vmProgressBar::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #6a6aaa, stop:0.5 #4a90d9, stop:1 #6a6aaa);
            border-radius: 2px;
        }
        #vmControlBar {
            background: #fff; border: 1px solid #e0e0e0;
            border-radius: 6px;
        }
        QToolTip {
            background: #fff; color: #333;
            border: 1px solid #ccc; padding: 6px 10px;
            border-radius: 4px; font-size: 12px;
        }
        QTreeWidget {
            background: #fff; border: none;
            border-right: 1px solid #e0e0e0; outline: none;
            padding: 4px;
        }
        QTreeWidget::item {
            color: #333; padding: 8px 10px;
            border-radius: 6px; margin: 1px 4px;
        }
        QTreeWidget::item:selected {
            background: #e8e8f0; color: #222; font-weight: bold;
        }
        QTreeWidget::item:hover:!selected {
            background: #f0f0f5;
        }
        #actionBtn { background: #4a4a8a; color: #fff; border: none; padding: 8px 22px; border-radius: 6px; font-weight: bold; font-size: 13px; }
        #actionBtn:hover { background: #5a5a9a; }
        #actionBtn:pressed { background: #6a6aaa; }
        #actionBtn:disabled { background: #eee; color: #999; }
        #actionBtnDanger { background: #c0392b; color: #fff; border: none; padding: 8px 22px; border-radius: 6px; font-weight: bold; font-size: 13px; }
        #actionBtnDanger:hover { background: #e74c3c; }
        #actionBtnDanger:pressed { background: #a93226; }
        #actionBtnDanger:disabled { background: #eee; color: #999; }
        #actionBtnSecondary { background: transparent; color: #555; border: 1px solid #ccc; padding: 8px 22px; border-radius: 6px; font-size: 13px; }
        #actionBtnSecondary:hover { background: #f0f0f5; color: #222; border-color: #aaa; }
        #actionBtnTertiary {
            background: #e8ecf4; color: #444; border: none;
            padding: 8px 18px; border-radius: 6px; font-size: 13px;
        }
        #actionBtnTertiary:hover { background: #dce0ee; color: #222; }
        #actionBtnTertiary:pressed { background: #d0d5e8; }
        #actionBtnTertiary:disabled { background: #f5f5f8; color: #aaa; }
        #vmNotes { color: #666; font-size: 12px; padding: 4px 0; }
        #emptyCards QFrame#emptyCard {
            background: #fff; border: 1px solid #e0e0e0;
            border-radius: 12px; padding: 10px;
            min-width: 200px; max-width: 260px;
        }
        #emptyCard:hover { border-color: #aaa; background: #fafafe; }
        #cardBtn { background: #4a4a8a; color: #fff; border: none; padding: 6px 20px; border-radius: 6px; font-size: 12px; font-weight: bold; }
        #cardBtn:hover { background: #5a5a9a; }
        QTextEdit { background: #fafafe; color: #333; border: none; padding: 8px; font-size: 12px; }
        QLineEdit, QSpinBox, QComboBox, QPlainTextEdit {
            background: #fff; color: #333;
            border: 1px solid #ddd; border-radius: 4px; padding: 6px 10px; font-size: 13px;
        }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: #7a7aaa; }
        QSpinBox::up-button, QSpinBox::down-button {
            border: none; background: #e8ecf4;
            width: 16px; border-radius: 2px; margin: 1px;
        }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: #dce0ec; }
        QSpinBox::up-arrow { image: none; width: 0; height: 0; border-left: 5px solid transparent; border-right: 5px solid transparent; border-bottom: 5px solid #555; }
        QSpinBox::down-arrow { image: none; width: 0; height: 0; border-left: 5px solid transparent; border-right: 5px solid transparent; border-top: 5px solid #555; }
        #vmSearch {
            background: #f5f5f8; border: 1px solid #ddd;
            border-radius: 6px; padding: 6px 10px; font-size: 13px;
        }
        #vmSearch:focus { border-color: #7a7aaa; background: #fff; }
        QComboBox::drop-down { border: none; padding-right: 8px; }
        QComboBox QAbstractItemView {
            background: #fff; color: #333; border: 1px solid #ddd;
            selection-background-color: #e8e8f0;
        }
        QCheckBox { color: #444; spacing: 8px; font-size: 13px; }
        QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #ccc; border-radius: 3px; background: #fff; }
        QCheckBox::indicator:checked { background: #4a4a8a; border-color: #4a4a8a; }
        QPushButton { color: #333; background: #e8ecf4; border: 1px solid #d0d5e0; padding: 6px 16px; border-radius: 6px; font-size: 13px; }
        QPushButton:hover { background: #dce0ec; border-color: #b8bcc8; color: #111; }
        QPushButton:pressed { background: #d0d4e0; }
        QPushButton:disabled { background: #f5f5f8; color: #aaa; border-color: #e8e8ee; }
        QDialog { background: #f5f5f8; }
        QLabel { color: #333; }
        QStatusBar { background: #fff; border-top: 1px solid #eee; color: #888; font-size: 12px; }
        QGroupBox {
            color: #444; border: 1px solid #ddd;
            border-radius: 8px; margin-top: 14px; padding-top: 18px; font-size: 13px;
            background: transparent;
        }
        QGroupBox::title {
            subcontrol-origin: margin; left: 12px; padding: 0 8px; font-weight: bold;
        }
        QTabWidget::pane { background: transparent; border: none; }
        QTabWidget#vmTabs::pane { background: transparent; border: none; }
        QTabBar::tab {
            color: #888; background: transparent;
            padding: 10px 20px; border: none;
            border-bottom: 2px solid transparent; font-size: 12px;
            font-weight: bold; letter-spacing: 0.5px;
        }
        QTabBar::tab:selected { color: #222; border-bottom: 2px solid #4a4a8a; }
        QTabBar::tab:hover:!selected { color: #555; }
        QScrollBar:vertical { background: #f5f5f8; width: 8px; margin: 0; }
        QScrollBar::handle:vertical { background: #ccc; border-radius: 4px; min-height: 30px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QDialogButtonBox QPushButton { padding: 8px 28px; font-weight: bold; }
    )");
}

void MainWindow::refreshVMList() {
    auto *currentItem = m_vmList->currentItem();
    QString selectedId;
    if (currentItem)
        selectedId = currentItem->data(0, Qt::UserRole).toString();

    m_vmList->blockSignals(true);
    m_vmList->clear();

    auto configs = m_manager->allConfigs();
    QTreeWidgetItem *selectedItem = nullptr;

    for (const auto &cfg : configs) {
        auto *item = new QTreeWidgetItem;
        bool running = m_manager->isVMRunning(cfg.id);
        auto *task = m_manager->taskForVM(cfg.id);
        QString status;
        if (task && task->isRunning())
            status = " [" + task->stateString() + "]";

        // Status indicator color
        QString statusColor = running ? "#6fcf97" : (m_darkTheme ? "#888" : "#aaa");
        if (task && task->state() == VMTask::Error) statusColor = "#e74c3c";
        else if (task && task->state() == VMTask::Paused) statusColor = "#f0ad4e";

        QString iconFile = cfg.iconName.isEmpty() ? "linux" : cfg.iconName;
        QIcon icon(QString(":/icons/%1.png").arg(iconFile));
        item->setIcon(0, icon);

        // Build text with status indicator
        QString bullet = QString("●");
        item->setText(0, QString("%1 %2%3")
            .arg(bullet, cfg.name, status));
        item->setData(0, Qt::UserRole, cfg.id);
        item->setForeground(0, QColor(statusColor));
        item->setToolTip(0, QString("%1\nStatus: %2\nCPU: %3 cores · RAM: %4\nOS: %5")
            .arg(cfg.name)
            .arg(running ? "Running" : (task && task->state() == VMTask::Paused ? "Paused" : "Stopped"))
            .arg(cfg.cpuCores)
            .arg(formatSizeMB(cfg.memoryMB))
            .arg(cfg.osType));
        m_vmList->addTopLevelItem(item);

        if (cfg.id == selectedId)
            selectedItem = item;
    }

    m_vmList->blockSignals(false);

    if (selectedItem)
        m_vmList->setCurrentItem(selectedItem);

    updateVMCount();
    if (m_vmList->topLevelItemCount() == 0)
        m_contentStack->setCurrentWidget(m_emptyPage);

    updateActionStates();
}

void MainWindow::updateVMCount() {
    int total = m_manager->allConfigs().size();
    int running = 0;
    for (const auto &cfg : m_manager->allConfigs()) {
        if (m_manager->isVMRunning(cfg.id))
            running++;
    }
    m_statusVMsLabel->setText(QString("%1 / %2 running").arg(running).arg(total));
    if (m_trayIcon)
        m_trayIcon->setToolTip(QString("VM Manager - %1 of %2 VMs running").arg(running).arg(total));
}

void MainWindow::onVMSelected() {
    auto *item = m_vmList->currentItem();
    if (!item) {
        if (m_vmList->topLevelItemCount() == 0)
            m_contentStack->setCurrentWidget(m_emptyPage);
        updateActionStates();
        return;
    }
    m_contentStack->setCurrentWidget(m_detailsPage);
    updateDetailsPanel();
    updateActionStates();
}

void MainWindow::updateDetailsPanel() {
    QString id = selectedVMId();
    if (id.isEmpty()) return;

    VMConfig cfg = m_manager->getConfig(id);
    auto *task = m_manager->taskForVM(id);

    m_vmNameLabel->setText(cfg.name);


    QString iconFile = cfg.iconName.isEmpty() ? "linux" : cfg.iconName;
    QPixmap pm(QString(":/icons/%1.png").arg(iconFile));
    if (!pm.isNull())
        m_vmIconLabel->setPixmap(pm.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    if (task && task->isRunning()) {
        QString stateColor;
        QString stateText;
        switch (task->state()) {
        case VMTask::Running: stateColor = "#6fcf97"; stateText = "Running"; break;
        case VMTask::Paused: stateColor = "#f0ad4e"; stateText = "Paused"; break;
        case VMTask::Starting: stateColor = "#5bc0de"; stateText = "Starting"; break;
        default: stateColor = "#e74c3c"; stateText = "Error"; break;
        }
        m_vmStatusLabel->setText(stateText);
        m_vmStatusLabel->setStyleSheet(
            QString("QLabel { padding: 4px 14px; border-radius: 10px; font-size: 12px; "
                    "font-weight: bold; background: %1; color: %2; }")
                .arg(stateColor, m_darkTheme ? "#0d0d1a" : "#fff"));
    } else {
        m_vmStatusLabel->setText("Stopped");
        m_vmStatusLabel->setStyleSheet(
            QString("QLabel { padding: 4px 14px; border-radius: 10px; font-size: 12px; "
                    "font-weight: bold; background: %1; color: %2; }")
                .arg(m_darkTheme ? "#3a3a3a" : "#ddd",
                     m_darkTheme ? "#888" : "#666"));
    }

    QString diskInfo = cfg.disk.path.isEmpty()
        ? "No disk"
        : QFileInfo(cfg.disk.path).fileName() + " (" + cfg.disk.format + ")";

    m_vmCpuLabel->setText(QString("%1 cores (%2)").arg(cfg.cpuCores).arg(cfg.cpuModel));
    m_vmMemoryLabel->setText(formatSizeMB(cfg.memoryMB));
    m_vmDiskLabel->setText(diskInfo);
    m_vmNetworkLabel->setText(cfg.network.mode);
    m_vmDisplayLabel->setText(cfg.displayMode);

    if (task && task->isRunning()) {
        qint64 secs = task->elapsedMs() / 1000;
        int h = static_cast<int>(secs / 3600);
        int m = static_cast<int>((secs % 3600) / 60);
        int s = static_cast<int>(secs % 60);
        m_vmUptimeLabel->setText(QString("%1h %2m %3s").arg(h).arg(m).arg(s));
    } else {
        m_vmUptimeLabel->setText("-");
    }

    // Update resource bars (visual gauge only — real usage would need QEMU monitor)
    m_vmMemoryBar->setValue(qMin(100, static_cast<int>(cfg.memoryMB / 16)));
    m_vmDiskBar->setValue(40);
    m_vmCpuBar->setValue(0);
    m_vmNetworkBar->setValue(0);
    m_vmDisplayBar->setValue(0);
    m_vmUptimeBar->hide();

    bool running = task && task->isRunning();
    bool hasTask = task != nullptr;
    bool paused = task && task->isPaused();
    bool starting = task && task->state() == VMTask::Starting;
    bool stopping = task && task->state() == VMTask::Stopping;

    // Show/hide startup progress bar
    m_vmProgressBar->setVisible(starting);

    m_vmControlBar->setVisible(hasTask);
    m_vmStartBtn->setEnabled(!running && !starting && !stopping);
    m_vmStopBtn->setEnabled(running);
    m_vmPauseBtn->setText(paused ? "\xE2\x96\xAE Resume" : "\xE2\x9D\xB4 Pause");
    m_vmPauseBtn->setEnabled(running);
    m_vmForceStopBtn->setEnabled(running || starting || stopping);

    m_vncViewerBtn->setEnabled(hasTask);
    if (!running && hasTask)
        m_vncViewerBtn->setText("Starting...");
    else
        m_vncViewerBtn->setText("Open Display");
}

void MainWindow::onVMContextMenu(const QPoint &pos) {
    QTreeWidgetItem *item = m_vmList->itemAt(pos);
    if (!item) return;
    m_vmList->setCurrentItem(item);

    QMenu menu(this);
    QString id = item->data(0, Qt::UserRole).toString();
    VMConfig cfg = m_manager->getConfig(id);
    auto *task = m_manager->taskForVM(id);
    bool running = task && task->isRunning();
    bool active = task && (task->isRunning() || task->state() == VMTask::Starting || task->state() == VMTask::Stopping);
    bool paused = task && task->isPaused();

    menu.addAction("Start", this, &MainWindow::onStartVM)->setEnabled(!active);
    menu.addAction("Stop", this, &MainWindow::onStopVM)->setEnabled(running);
    menu.addAction(paused ? "Resume" : "Pause", this, &MainWindow::onPauseResumeVM)->setEnabled(running);
    menu.addSeparator();
    menu.addAction("Open Display", [this]() {
        QString mode = m_displayModeCombo->currentText();
        if (mode == "Embedded Tab") {
            m_rightTabs->setCurrentWidget(m_vncViewer);
            if (!m_vncViewer->isConnected())
                connectVNCViewer();
        } else {
            openVNCViewer();
        }
    })->setEnabled(running);
    menu.addSeparator();
    menu.addAction("Settings", this, &MainWindow::onEditVM)->setEnabled(!running);
    menu.addAction("Clone", this, &MainWindow::onCloneVM);
    menu.addAction("Export", this, &MainWindow::onExportVM);
    menu.addSeparator();
    menu.addAction("Delete", this, &MainWindow::onDeleteVM)->setEnabled(!running);
    menu.exec(m_vmList->mapToGlobal(pos));
}

void MainWindow::onVMStateChanged(const QString &id, VMTask::State state) {
    if (id == selectedVMId()) {
        updateDetailsPanel();
    }
    if (state == VMTask::Stopped || state == VMTask::Error) {
        if (m_vncViewer->isConnected())
            m_vncViewer->disconnectFromHost();
        if (m_vncDisplayWindow->isConnected())
            m_vncDisplayWindow->disconnectFromHost();
    }

    // Tray notifications for state changes
    if (m_trayIcon && !isVisible()) {
        auto cfg = m_manager->getConfig(id);
        QStringList notableStates = {"Running", "Stopped", "Error"};
        QString stateStr;
        auto *t = m_manager->taskForVM(id);
        if (t) stateStr = t->stateString();
        if (notableStates.contains(stateStr)) {
            m_trayIcon->showMessage(
                cfg.name,
                QString("VM state changed to: %1").arg(stateStr),
                state == VMTask::Error ? QSystemTrayIcon::Critical : QSystemTrayIcon::Information,
                3000);
        }
    }

    refreshVMList();
}

QString MainWindow::selectedVMId() const {
    auto *item = m_vmList->currentItem();
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

void MainWindow::updateActionStates() {
    QString id = selectedVMId();
    bool hasSelection = !id.isEmpty();
    bool hasConfig = hasSelection && m_manager->configExists(id);
    bool running = hasSelection && m_manager->isVMRunning(id);
    auto *task = hasSelection ? m_manager->taskForVM(id) : nullptr;
    bool paused = task && task->isPaused();
    bool active = task && (task->isRunning() || task->state() == VMTask::Starting || task->state() == VMTask::Stopping);

    m_startAction->setEnabled(hasConfig && !active);
    m_stopAction->setEnabled(running);
    m_pauseResumeAction->setEnabled(running);
    m_pauseResumeAction->setText(paused ? "Resume" : "Pause");
    m_settingsAction->setEnabled(hasConfig && !running);
    m_deleteAction->setEnabled(hasConfig && !running);
    m_aiAction->setChecked(m_aiDock->isVisible());
}

void MainWindow::onStartVM() {
    QString id = selectedVMId();
    if (id.isEmpty()) return;

    auto *task = m_manager->startVM(id);
    if (task) {
        disconnect(task, nullptr, this, nullptr);
        connect(task, &VMTask::serialReceived, this, [this, id](const QString &text) {
            if (id == selectedVMId())
                m_consoleWidget->appendSerial(text);
        });
        connect(task, &VMTask::outputReceived, this, [this, id](const QString &text) {
            if (id == selectedVMId())
                m_consoleWidget->appendOutput(text);
        });
        // Auto-connect built-in viewer once QEMU is running
        connect(task, &VMTask::stateChanged, this,
            [this, id](VMTask::State state) {
                if (state == VMTask::Running) {
                    QTimer::singleShot(1500, this, [this, id]() {
                        if (id == selectedVMId())
                            connectVNCViewer();
                    });
                }
            });
    }
}

void MainWindow::connectVNCViewer() {
    QString id = selectedVMId();
    if (id.isEmpty()) return;
    auto cfg = m_manager->getConfig(id);
    QString mode = m_displayModeCombo->currentText();

    if (cfg.displayMode == "spice") {
        openVNCViewer();
        return;
    }

    if (cfg.displayMode != "vnc") return;
    if (mode == "Embedded Tab") {
        if (m_vncViewer->isConnected()) return;
        m_vncViewer->connectToHost("127.0.0.1", cfg.vncPort);
        m_rightTabs->setCurrentWidget(m_vncViewer);
    } else if (mode == "External VNC") {
        openVNCViewer();
    }
}

void MainWindow::onStopVM() {
    QString id = selectedVMId();
    if (!id.isEmpty()) {
        m_manager->stopVM(id);
        statusBar()->showMessage("VM stopping...", 2000);
    }
}

void MainWindow::onPauseResumeVM() {
    QString id = selectedVMId();
    if (id.isEmpty()) return;
    auto *task = m_manager->taskForVM(id);
    if (!task) return;

    if (task->isPaused()) {
        m_manager->resumeVM(id);
        statusBar()->showMessage("VM resumed", 2000);
    } else {
        m_manager->pauseVM(id);
        statusBar()->showMessage("VM paused", 2000);
    }
}

void MainWindow::onEditVM() {
    QString id = selectedVMId();
    if (id.isEmpty()) return;

    auto cfg = m_manager->getConfig(id);
    if (cfg.id.isEmpty()) return;

    VMCreatorDialog dialog(cfg, this);
    if (dialog.exec() != QDialog::Accepted) return;

    VMConfig newCfg = dialog.config();
    newCfg.disk.sizeMB = cfg.disk.sizeMB;

    m_manager->saveConfig(newCfg);
    updateDetailsPanel();
    statusBar()->showMessage("VM settings saved", 2000);
}

void MainWindow::onRenameVM() {
    QString id = selectedVMId();
    if (id.isEmpty()) return;

    VMConfig cfg = m_manager->getConfig(id);
    if (cfg.id.isEmpty()) return;

    bool ok = false;
    QString newName = QInputDialog::getText(this, "Rename VM", "New name:",
                                             QLineEdit::Normal, cfg.name, &ok);
    if (ok && !newName.trimmed().isEmpty() && newName.trimmed() != cfg.name) {
        cfg.name = newName.trimmed();
        m_manager->saveConfig(cfg);
        refreshVMList();
        updateDetailsPanel();
        statusBar()->showMessage("VM renamed to: " + cfg.name, 2000);
    }
}

void MainWindow::onPreferences() {
    AppSettingsDialog dlg(this);
    dlg.exec();
    // Refresh AI panel visibility based on setting
    bool aiOn = AppSettingsDialog::loadAIEnabled();
    m_aiWidget->setVisible(aiOn);
    if (m_aiAction) m_aiAction->setChecked(aiOn);
}

void MainWindow::onAbout() {
    QMessageBox::about(this, "About ForgeVM",
        "<h3>ForgeVM</h3>"
        "<p>Version 1.0</p>"
        "<p>A graphical QEMU virtual machine manager.</p>"
        "<p>Built with Qt " QT_VERSION_STR " and C++17.</p>"
        "<p style='font-size: 11px; color: #888;'>"
        "QEMU is a trademark of the QEMU Project<br>"
        "Not affiliated with or endorsed by the QEMU team</p>"
        "<hr>"
        "<p style='font-size: 11px; color: #888;'>"
        "<b>Icons:</b> OS icons are from the "
        "<a href='https://github.com/utmapp/utm'>UTM project</a> "
        "(GPL-3.0).<br>"
        "<b>Windows Product Keys:</b> From "
        "<a href='https://github.com/iMoeAriaCG/Windows-Key-Collection'>"
        "Windows-Key-Collection</a> (CC-BY-4.0).<br>"
        "<b>Pre-built VM images:</b> Curated from public OS mirrors "
        "(see each distro's license).</p>");
}

void MainWindow::onDeleteVM() {
    QString id = selectedVMId();
    if (id.isEmpty()) return;

    auto cfg = m_manager->getConfig(id);

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Delete VM");
    msgBox.setText(QString("Delete \"%1\"?").arg(cfg.name));
    msgBox.setInformativeText("The VM configuration will be removed.");
    QCheckBox *deleteDisk = new QCheckBox("Also delete the disk image file");
    bool hasDisk = !cfg.disk.path.isEmpty() && QFileInfo::exists(cfg.disk.path);
    if (hasDisk) {
        deleteDisk->setText(QString("Also delete disk image (%1)")
            .arg(QFileInfo(cfg.disk.path).fileName()));
        msgBox.setCheckBox(deleteDisk);
    }
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    if (msgBox.exec() == QMessageBox::Yes) {
        if (hasDisk && deleteDisk->isChecked()) {
            QFile::remove(cfg.disk.path);
        }
        m_manager->deleteConfig(id);
        statusBar()->showMessage("VM deleted", 2000);
        if (m_vmList->topLevelItemCount() == 0) {
            m_contentStack->setCurrentWidget(m_emptyPage);
            clearDetails();
        } else {
            m_vmList->setCurrentItem(m_vmList->topLevelItem(0));
        }
    }
}

void MainWindow::openVNCViewer() {
    QString id = selectedVMId();
    if (id.isEmpty()) return;

    auto *task = m_manager->taskForVM(id);
    if (!task || !task->isRunning()) return;

    auto cfg = m_manager->getConfig(id);

    if (cfg.displayMode == "spice") {
        int port = task->spicePort();
        if (port <= 0) port = 5900;
        QStringList viewers = {"remote-viewer", "vinagre", "spicec"};
        QString viewer;
        for (const auto &v : viewers) {
            viewer = QStandardPaths::findExecutable(v);
            if (!viewer.isEmpty()) break;
        }
        if (viewer.isEmpty()) {
            QMessageBox::information(this, "SPICE Viewer",
                "SPICE display enabled.\n"
                "Install remote-viewer to connect.");
            return;
        }
        QProcess::startDetached(viewer, QStringList() << QString("spice://127.0.0.1:%1").arg(port - 5900));
        return;
    }

    int port = task->vncPort();
    if (port <= 0) return;

    QStringList viewers = {"remote-viewer", "vinagre", "vncviewer",
                           "krdc", "gvncviewer", "vnc-client"};
    QString viewer;
    for (const auto &v : viewers) {
        viewer = QStandardPaths::findExecutable(v);
        if (!viewer.isEmpty()) break;
    }

    if (viewer.isEmpty()) {
        QMessageBox::information(this, "VNC Viewer",
            QString("VM is running on VNC port %1.\n"
                    "Connect with any VNC client to localhost:%1")
                .arg(port));
        return;
    }

    QString addr = QString("localhost:%1").arg(port);
    QString uri = QString("vnc://localhost:%1").arg(port);
    QStringList args;

    if (viewer.endsWith("krdc") || viewer.endsWith("remote-viewer")
        || viewer.endsWith("vinagre") || viewer.endsWith("vnc-client"))
        args << uri;
    else
        args << addr;

    QProcess::startDetached(viewer, args);
}

void MainWindow::openGTKDisplay() {
    QString id = selectedVMId();
    if (id.isEmpty()) return;

    VMConfig cfg = m_manager->getConfig(id);
    auto *task = m_manager->taskForVM(id);
    if (!task || !task->isRunning()) return;

    int port = task->vncPort();
    if (port <= 0) return;

    // Try to find a VNC viewer, in order of preference
    QStringList viewers = {"remote-viewer", "vinagre", "vncviewer", "krdc"};
    QString viewer;
    for (const auto &v : viewers) {
        QString path = QStandardPaths::findExecutable(v);
        if (!path.isEmpty()) {
            viewer = v;
            break;
        }
    }

    if (viewer.isEmpty()) {
        QMessageBox::warning(this, "No VNC Viewer",
            "No VNC viewer found. Install one:\n"
            "  sudo apt install virt-viewer   (GTK)\n"
            "  sudo apt install vinagre        (GNOME)\n"
            "  sudo apt install tigervnc-viewer\n"
            "  sudo apt install krdc           (KDE)");
        return;
    }

    QString uri = QString("vnc://localhost:%1").arg(port);
    QString addr = QString("localhost:%1").arg(port);
    QStringList args;
    if (viewer == "krdc" || viewer == "remote-viewer" || viewer == "vinagre")
        args << uri;
    else
        args << addr;

    QProcess::startDetached(viewer, args);
}

void MainWindow::onConvertDisk() {
    onTools();
}

void MainWindow::onTools() {
    QDialog dlg(this);
    dlg.setWindowTitle("Convert Disk Image");
    dlg.setMinimumSize(500, 300);

    auto *layout = new QVBoxLayout(&dlg);
    auto *header = new QLabel("Convert between VM disk formats");
    header->setStyleSheet("font-size: 14px; font-weight: bold; padding: 8px 0;");
    layout->addWidget(header);

    auto *fromRow = new QHBoxLayout;
    fromRow->addWidget(new QLabel("Source:"));
    auto *srcPath = new QLineEdit;
    srcPath->setPlaceholderText("Path to source image (.vdi, .vmdk, .ova)");
    fromRow->addWidget(srcPath, 1);
    auto *browseSrc = new QPushButton("Browse...");
    fromRow->addWidget(browseSrc);
    layout->addLayout(fromRow);

    auto *formatRow = new QHBoxLayout;
    formatRow->addWidget(new QLabel("Format:"));
    auto *srcFormat = new QComboBox;
    srcFormat->addItems({"vdi", "vmdk", "ova", "raw", "qcow2"});
    formatRow->addWidget(srcFormat);
    formatRow->addStretch();
    layout->addLayout(formatRow);

    auto *toRow = new QHBoxLayout;
    toRow->addWidget(new QLabel("Output:"));
    auto *dstPath = new QLineEdit;
    dstPath->setPlaceholderText("Path for converted image (.qcow2)");
    toRow->addWidget(dstPath, 1);
    auto *browseDst = new QPushButton("Browse...");
    toRow->addWidget(browseDst);
    layout->addLayout(toRow);

    auto *convertBtn = new QPushButton("Convert");
    convertBtn->setObjectName("actionBtn");
    auto *cancelBtn = new QPushButton("Cancel");
    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(convertBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    QString detectedFormat;

    connect(browseSrc, &QPushButton::clicked, [&]() {
        QString path = QFileDialog::getOpenFileName(&dlg,
            "Select Disk Image", QDir::homePath(),
            "Disk Images (*.vdi *.vmdk *.ova *.qcow2 *.raw *.img);;All Files (*)");
        if (!path.isEmpty()) {
            srcPath->setText(path);
            if (path.endsWith(".vdi", Qt::CaseInsensitive)) detectedFormat = "vdi";
            else if (path.endsWith(".vmdk", Qt::CaseInsensitive)) detectedFormat = "vmdk";
            else if (path.endsWith(".ova", Qt::CaseInsensitive)) detectedFormat = "ova";
            else if (path.endsWith(".qcow2", Qt::CaseInsensitive)) detectedFormat = "qcow2";
            else detectedFormat = "raw";
            srcFormat->setCurrentText(detectedFormat);

            QString out = path;
            if (out.endsWith(".vdi", Qt::CaseInsensitive) || out.endsWith(".vmdk", Qt::CaseInsensitive))
                out = out.left(out.lastIndexOf('.')) + ".qcow2";
            else if (out.endsWith(".ova", Qt::CaseInsensitive))
                out = out.left(out.lastIndexOf('.')) + ".qcow2";
            dstPath->setText(out);
        }
    });

    connect(browseDst, &QPushButton::clicked, [&]() {
        QString path = QFileDialog::getSaveFileName(&dlg,
            "Save Converted Image", QDir::homePath(),
            "QEMU Images (*.qcow2);;All Files (*)");
        if (!path.isEmpty()) dstPath->setText(path);
    });

    connect(convertBtn, &QPushButton::clicked, [&]() {
        QString src = srcPath->text().trimmed();
        QString dst = dstPath->text().trimmed();
        if (src.isEmpty() || dst.isEmpty()) {
            QMessageBox::warning(&dlg, "Error", "Source and output paths required");
            return;
        }
        if (!QFileInfo::exists(src)) {
            QMessageBox::warning(&dlg, "Error", "Source file not found");
            return;
        }

        statusBar()->showMessage("Converting...", 0);
        QString format = srcFormat->currentText();

        QProgressDialog pd("Converting disk image...", "Cancel", 0, 0, &dlg);
        pd.setWindowTitle("Converting");
        pd.setWindowModality(Qt::WindowModal);
        pd.setMinimumDuration(0);

        if (format == "ova") {
            // OVA is a tar archive containing VMDK/vmdk files
            QString extractDir = QDir::temp().absoluteFilePath("ova-extract");
            QDir().mkpath(extractDir);

            QProcess tar;
            tar.start("tar", {"-xf", src, "-C", extractDir});
            tar.waitForFinished(30000);
            if (tar.exitCode() != 0) {
                QMessageBox::warning(&dlg, "Error", "Failed to extract OVA:\n" + tar.readAllStandardError());
                return;
            }

            // Find VMDK file
            QStringList vmdks = QDir(extractDir).entryList({"*.vmdk", "*.VMDK"}, QDir::Files);
            if (vmdks.isEmpty()) {
                QMessageBox::warning(&dlg, "Error", "No VMDK found inside OVA");
                return;
            }

            QProcess proc;
            proc.start("qemu-img", {"convert", "-f", "vmdk", "-O", "qcow2",
                      extractDir + "/" + vmdks[0], dst});
            connect(&proc, &QProcess::finished, &pd, &QProgressDialog::reset);
            connect(&pd, &QProgressDialog::canceled, &proc, &QProcess::kill);
            pd.show();
            proc.waitForFinished(120000);
            if (proc.exitCode() != 0) {
                QMessageBox::warning(&dlg, "Error", "Conversion failed:\n" + proc.readAllStandardError());
            } else {
                QMessageBox::information(&dlg, "Done", "Converted to: " + dst);
            }
            QDir(extractDir).removeRecursively();
        } else {
            QProcess proc;
            proc.start("qemu-img", {"convert", "-f", format, "-O", "qcow2", src, dst});
            connect(&proc, &QProcess::finished, &pd, &QProgressDialog::reset);
            connect(&pd, &QProgressDialog::canceled, &proc, &QProcess::kill);
            pd.show();
            proc.waitForFinished(120000);
            if (proc.exitCode() != 0) {
                QMessageBox::warning(&dlg, "Error", "Conversion failed:\n" + proc.readAllStandardError());
            } else {
                QMessageBox::information(&dlg, "Done", "Converted to: " + dst);
            }
        }
        statusBar()->clearMessage();
    });

    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        // handled by connect
    }
}

void MainWindow::onNewVM() {
    VMCreatorDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;

    VMConfig cfg = dialog.config();

    if (dialog.createDisk() && !dialog.diskPath().isEmpty()) {
        QStringList args;
        args << "create" << "-f" << cfg.disk.format << dialog.diskPath()
             << QString::number(cfg.disk.sizeMB) + "M";

        QProcess proc;
        proc.start("qemu-img", args);
        if (!proc.waitForFinished(30000)) {
            QMessageBox::warning(this, "Error",
                "Failed to create disk: " + proc.errorString());
            return;
        }
        if (proc.exitCode() != 0) {
            QMessageBox::warning(this, "Error",
                "Failed to create disk:\n"
                + QString::fromUtf8(proc.readAllStandardError()));
            return;
        }
    }

    m_manager->saveConfig(cfg);
    refreshVMList();

    for (int i = 0; i < m_vmList->topLevelItemCount(); i++) {
        auto *item = m_vmList->topLevelItem(i);
        if (item && item->data(0, Qt::UserRole).toString() == cfg.id) {
            m_vmList->setCurrentItem(item);
            break;
        }
    }
    statusBar()->showMessage("VM created: " + cfg.name, 3000);
}

void MainWindow::onNewVMFromConfig(const VMConfig &cfg) {
    VMCreatorDialog dialog(this);

    // Pre-fill dialog with AI-generated config
    // Since we can't easily set all field programmatically without adding
    // a setter method, we'll just pass the config and the user can adjust
    // Instead, let's create the VM directly with sensible defaults

    VMConfig finalCfg = cfg;
    if (finalCfg.id.isEmpty()) finalCfg.id = VMConfig::generateId();
    if (finalCfg.name.isEmpty()) finalCfg.name = "AI-Generated VM";
    if (finalCfg.disk.path.isEmpty()) {
        finalCfg.disk.path = QDir::homePath() + "/" + finalCfg.name + ".qcow2";
    }
    if (finalCfg.disk.sizeMB == 0) finalCfg.disk.sizeMB = 10240;

    auto reply = QMessageBox::question(this, "Create AI-Generated VM",
        QString("Create VM \"%1\"?\n"
                "CPU: %2 cores\nRAM: %3\nDisk: %4 (%5)\n"
                "OS: %6\n\nOpen the settings dialog to customize?")
            .arg(finalCfg.name)
            .arg(finalCfg.cpuCores)
            .arg(formatSizeMB(finalCfg.memoryMB))
            .arg(formatSizeMB(finalCfg.disk.sizeMB))
            .arg(finalCfg.disk.format)
            .arg(finalCfg.osType),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Save);

    if (reply == QMessageBox::No) return;

    if (reply == QMessageBox::Save) {
        VMCreatorDialog dlg(finalCfg, this);
        if (dlg.exec() == QDialog::Accepted) {
            VMConfig adjusted = dlg.config();
            adjusted.id = finalCfg.id;
            adjusted.disk.sizeMB = finalCfg.disk.sizeMB;
            m_manager->saveConfig(adjusted);
        } else {
            return;
        }
    } else {
        m_manager->saveConfig(finalCfg);
    }

    refreshVMList();
    statusBar()->showMessage("AI-generated VM created: " + finalCfg.name, 3000);
}

void MainWindow::onPresetDownload() {
    QDialog dlg(this);
    dlg.setWindowTitle("Download Preset VM");
    dlg.setMinimumSize(450, 400);
    dlg.resize(500, 450);

    auto *layout = new QVBoxLayout(&dlg);

    auto *header = new QLabel("Select a preset VM configuration");
    header->setStyleSheet("font-size: 16px; font-weight: bold; padding: 8px 0;");
    layout->addWidget(header);

    auto *desc = new QLabel(
        "These are pre-configured templates. You'll still need to provide\n"
        "your own installation media (ISO).");
    desc->setStyleSheet("font-size: 12px; opacity: 0.7; padding-bottom: 8px;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    struct Preset {
        QString name;
        QString osType;
        int cpu;
        int ramMB;
        int diskMB;
        bool efi;
        QString desc;
    };

    QList<Preset> presets = {
        {"Ubuntu 24.04 Desktop", "linux", 4, 4096, 25600, true,
         "Ubuntu Desktop with GNOME, 4 cores, 4 GB RAM"},
        {"Ubuntu 24.04 Server", "linux", 2, 2048, 10240, true,
         "Ubuntu Server LTS, minimal footprint"},
        {"Windows 11", "windows", 4, 8192, 65536, true,
         "Windows 11 with TPM-like UEFI, 8 GB RAM"},
        {"Windows 10", "windows", 4, 4096, 32768, true,
         "Windows 10 with UEFI, 4 GB RAM"},
        {"FreeBSD 14", "freebsd", 2, 2048, 10240, false,
         "FreeBSD with BIOS boot, 2 GB RAM"},
        {"OpenBSD 7.6", "openbsd", 1, 1024, 5120, false,
         "OpenBSD minimal, 1 GB RAM"},
        {"Alpine Linux", "linux", 1, 512, 2048, false,
         "Lightweight Alpine Linux, 512 MB RAM"},
        {"Debian 12", "linux", 2, 2048, 10240, true,
         "Debian Bookworm with UEFI"},
        {"Arch Linux", "linux", 2, 2048, 10240, true,
         "Arch Linux (no installer - manual setup)"},
        {"Kali Linux", "linux", 4, 4096, 32768, true,
         "Kali Linux for security testing"},
    };

    auto *listWidget = new QListWidget;
    for (const auto &p : presets) {
        auto *item = new QListWidgetItem(p.name + "\n" + p.desc);
        item->setData(Qt::UserRole, p.osType);
        item->setData(Qt::UserRole + 1, p.cpu);
        item->setData(Qt::UserRole + 2, p.ramMB);
        item->setData(Qt::UserRole + 3, p.diskMB);
        item->setData(Qt::UserRole + 4, p.efi);
        item->setSizeHint(QSize(0, 50));
        listWidget->addItem(item);
    }
    layout->addWidget(listWidget, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    auto *item = listWidget->currentItem();
    if (!item) return;

    VMConfig cfg;
    cfg.id = VMConfig::generateId();
    cfg.name = item->text().split('\n')[0];
    cfg.osType = item->data(Qt::UserRole).toString();
    cfg.cpuCores = item->data(Qt::UserRole + 1).toInt();
    cfg.memoryMB = item->data(Qt::UserRole + 2).toInt();
    cfg.disk.sizeMB = item->data(Qt::UserRole + 3).toLongLong();
    cfg.efiBoot = item->data(Qt::UserRole + 4).toBool();
    cfg.kvmEnabled = true;
    cfg.machine = "q35";
    cfg.cpuModel = "host";
    cfg.displayMode = "vnc";
    cfg.vncPort = 5900;
    cfg.network.mode = "user";
    cfg.network.model = "virtio-net";
    cfg.disk.format = "qcow2";
    cfg.disk.controller = "virtio";
    cfg.disk.cache = "writeback";
    cfg.disk.path = QDir::homePath() + "/" + cfg.name.toLower()
                        .replace(' ', '-') + ".qcow2";

    VMCreatorDialog dlg2(cfg, this);
    if (dlg2.exec() == QDialog::Accepted) {
        VMConfig finalCfg = dlg2.config();
        finalCfg.id = cfg.id;
        finalCfg.disk.sizeMB = cfg.disk.sizeMB;
        m_manager->saveConfig(finalCfg);
        refreshVMList();
        statusBar()->showMessage("Preset VM created: " + finalCfg.name, 3000);
    }
}


void MainWindow::onPrebuiltVM() {
    QDialog dlg(this);
    dlg.setWindowTitle("Pre-built VM Images");
    dlg.setMinimumSize(550, 500);
    dlg.resize(750, 600);

    auto *layout = new QVBoxLayout(&dlg);

    auto *header = new QLabel("Ready-to-Run VM Images");
    header->setStyleSheet("font-size: 16px; font-weight: bold; padding: 8px 0;");
    layout->addWidget(header);

    auto *desc = new QLabel(
        "These are disk images with an OS already installed. "
        "Click a tab, choose a distro, then pick a version.");
    desc->setWordWrap(true);
    desc->setStyleSheet("font-size: 12px; opacity: 0.7; padding-bottom: 8px;");
    layout->addWidget(desc);

    // Search bar
    auto *searchEdit = new QLineEdit(&dlg);
    searchEdit->setPlaceholderText("Search VM images...");
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumWidth(300);
    layout->addWidget(searchEdit);

    struct PrebuiltImage {
        QString name, icon, url, desc;
        qint64 sizeMB;
        bool efi;
    };

    QList<PrebuiltImage> allImages;
    allImages.append({"Ubuntu 24.04 Desktop", "ubuntu", "https://releases.ubuntu.com/noble/ubuntu-24.04-desktop-amd64.iso", "Ubuntu 24.04 LTS Desktop", 5700, true});
    allImages.append({"Ubuntu 24.04 Server", "ubuntu", "https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img", "Ubuntu 24.04 Server cloud", 700, true});
    allImages.append({"Ubuntu 22.04 LTS", "ubuntu", "https://releases.ubuntu.com/jammy/ubuntu-22.04.5-desktop-amd64.iso", "Ubuntu 22.04 LTS Desktop", 4700, true});
    allImages.append({"Kubuntu 24.04", "kubuntu", "https://releases.ubuntu.com/noble/kubuntu-24.04-desktop-amd64.iso", "Kubuntu 24.04 KDE", 5700, true});
    allImages.append({"Xubuntu 24.04", "xubuntu", "https://releases.ubuntu.com/noble/xubuntu-24.04-desktop-amd64.iso", "Xubuntu 24.04 XFCE", 3500, true});
    allImages.append({"Lubuntu 24.04", "lubuntu", "https://releases.ubuntu.com/noble/lubuntu-24.04-desktop-amd64.iso", "Lubuntu 24.04 LXQt", 2200, true});
    allImages.append({"Mint 22 Cinnamon", "mint", "https://mirror.ventraip.com.au/linuxmint/stable/22.3/linuxmint-22.3-cinnamon-64bit.iso", "Linux Mint Cinnamon", 2100, true});
    allImages.append({"Mint 22 XFCE", "mint", "https://mirror.ventraip.com.au/linuxmint/stable/22.3/linuxmint-22.3-xfce-64bit.iso", "Linux Mint XFCE", 1900, true});
    allImages.append({"Debian 12 GNOME", "debian", "https://cdimage.debian.org/debian-cd/current/amd64/iso-dvd/debian-12.5.0-amd64-DVD-1.iso", "Debian 12 DVD installer", 4100, true});
    allImages.append({"Debian 12 Cloud", "debian", "https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2", "Debian 12 cloud image", 500, true});
    allImages.append({"Pop!_OS 22.04", "pop-os", "https://pop-os.systems/img/pop-os-22.04.iso", "Pop!_OS based on Ubuntu", 4000, true});
    allImages.append({"Fedora 40 Workstation", "fedora", "https://download.fedoraproject.org/pub/fedora/linux/releases/40/Workstation/x86_64/iso/Fedora-Workstation-40-1.14-x86_64.iso", "Fedora 40 GNOME", 2600, true});
    allImages.append({"Fedora 40 KDE", "fedora", "https://download.fedoraproject.org/pub/fedora/linux/releases/40/KDE/x86_64/iso/Fedora-KDE-40-1.14-x86_64.iso", "Fedora 40 KDE Plasma", 2600, true});
    allImages.append({"Manjaro KDE 24.1", "linux", "https://sourceforge.net/projects/manjaro/files/ManjaroKDE/24.1/ManjaroKDE-24.1-250317-Gordon-iso/ManjaroKDE-24.1-250317-Gordon.iso/download", "Manjaro KDE", 2400, true});
    allImages.append({"Manjaro GNOME 24.1", "linux", "https://sourceforge.net/projects/manjaro/files/ManjaroGNOME/24.1/ManjaroGNOME-24.1-250317-Gordon-iso/ManjaroGNOME-24.1-250317-Gordon.iso/download", "Manjaro GNOME", 2400, true});
    allImages.append({"MX Linux 23 XFCE", "linux", "https://mirror.ventraip.com.au/mxlinux/point_releases/23_x64/MX-23.4-xfce-Kyria-20250712.iso", "MX Linux XFCE", 1900, false});
    allImages.append({"MX Linux 23 KDE", "linux", "https://mirror.ventraip.com.au/mxlinux/point_releases/23_x64/MX-23.4-kde-Kyria-20250712.iso", "MX Linux KDE", 2100, false});
    allImages.append({"Zorin OS 17", "ubuntu", "https://download.zorin.com/17.2/zorin-os-17.2-core-amd64.iso", "Zorin OS 17", 3000, true});
    allImages.append({"Peppermint 11", "linux", "https://peppermintos.info/download/Peppermint-11-20230802-x86_64.iso", "Peppermint OS (cloud)", 1400, false});
    allImages.append({"Kali 2025.1", "backtrack", "https://ftp.udx.icscoe.jp/Linux/kali-images/kali-2025.1/kali-linux-2025.1-installer-amd64.iso", "Kali 2025.1 security", 4100, true});
    allImages.append({"Kali 2024.4", "backtrack", "https://mirror.vcu.edu/pub/gnu_linux/kali-images/kali-2024.4/kali-linux-2024.4-installer-amd64.iso", "Kali Linux security", 4100, true});
    allImages.append({"Parrot OS 4.12", "backtrack", "https://mirror.rackcorp.com/parrotsec/parrot-core/4.12/amd64/iso/parrot-core-4.12.0-amd64.iso", "Parrot OS security", 4000, true});
    allImages.append({"Alpine Linux 3.20", "alpine", "https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-virt-3.20.3-x86_64.iso", "Alpine Linux, very lightweight", 60, false});
    allImages.append({"Arch Linux 2025.01", "arch-linux", "https://archive.archlinux.org/iso/2025.01.01/archlinux-2025.01.01-x86_64.iso", "Arch Linux rolling release", 900, true});
    allImages.append({"NixOS 24.11 GNOME", "nixos", "https://releases.nixos.org/nixos/24.11/nixos-24.11.719113.50ab793786d9/nixos-gnome-24.11.719113.50ab793786d9-x86_64-linux.iso", "NixOS with GNOME", 2200, true});
    allImages.append({"OpenSUSE Leap 15.6", "opensuse", "https://download.opensuse.org/distribution/leap/15.6/iso/openSUSE-Leap-15.6-DVD-x86_64-Current.iso", "OpenSUSE Leap", 4400, true});
    allImages.append({"OpenSUSE Tumbleweed", "opensuse", "https://download.opensuse.org/tumbleweed/iso/openSUSE-Tumbleweed-DVD-x86_64-Current.iso", "OpenSUSE rolling", 4500, true});
    allImages.append({"Gentoo Minimal", "gentoo", "https://mirror.ventraip.com.au/gentoo/releases/amd64/current-install-x86_64-minimal-20240808T170106Z.iso", "Gentoo minimal install", 180, false});
    allImages.append({"Void Linux", "linux", "https://void-cache.llnl.gov/voidlinux/live/voidlinux-live-x86_64-20240817.iso", "Void Linux live", 700, false});
    allImages.append({"Rocky Linux 9", "linux", "https://download.rockylinux.org/pub/rocky/9/isos/x86_64/Rocky-9-latest-x86_64-dvd.iso", "Rocky Linux 9 RHEL-compatible", 6500, true});
    allImages.append({"AlmaLinux 9", "linux", "https://repo.almalinux.org/almalinux/9/isos/x86_64/AlmaLinux-9-latest-x86_64-dvd.iso", "AlmaLinux 9 RHEL-compatible", 6500, true});
    allImages.append({"Slackware 15.1", "slackware", "https://mirrors.ocf.berkeley.edu/slackware/slackware-15.1/slackware64-15.1-install-dvd.iso", "Slackware 15.1", 3800, true});
    allImages.append({"Windows 11 24H2", "windows-11", "https://github.com/massgravel/Microsoft-Windows-11-Download/releases/download/Windows-11-24H2-EN-US-x64.iso/Windows-11-24H2-EN-US-x64.iso", "Windows 11 24H2 unified ISO", 6700, true});
    allImages.append({"Windows 11 23H2", "windows-11", "https://github.com/massgravel/Microsoft-Windows-11-Download/releases/download/Windows-11-23H2-EN-US-x64.iso/Windows-11-23H2-EN-US-x64.iso", "Windows 11 unified ISO", 6700, true});
    allImages.append({"Windows 11 SE", "windows-11", "https://github.com/massgravel/Microsoft-Windows-11-Download/releases/download/Windows-11-SE-24H2-EN-US-x64.iso/Windows-11-SE-24H2-EN-US-x64.iso", "Windows 11 SE (education)", 6700, true});
    allImages.append({"Windows 10 22H2", "windows-11", "https://github.com/massgravel/Microsoft-Windows-10-Download/releases/download/Windows-10-22H2-EN-US-x64.iso/Windows-10-22H2-EN-US-x64.iso", "Windows 10 unified ISO", 6700, true});
    allImages.append({"Windows 8.1", "windows", "https://github.com/massgravel/Microsoft-Windows-8.1-Download/releases/download/Windows-8.1-EN-US-x64.iso/Windows-8.1-EN-US-x64.iso", "Windows 8.1 unified ISO", 4000, true});
    allImages.append({"Windows 7 SP1", "windows", "https://github.com/massgravel/Microsoft-Windows-7-Download/releases/download/Windows-7-SP1-EN-US-x64.iso/Windows-7-SP1-EN-US-x64.iso", "Windows 7 SP1 unified ISO", 4700, true});
    allImages.append({"Windows XP SP3", "windows-xp", "https://github.com/massgravel/Microsoft-Windows-XP-Download/releases/download/Windows-XP-SP3-EN-US-x86.iso/Windows-XP-SP3-EN-US-x86.iso", "Windows XP SP3 (x86)", 2500, true});
    allImages.append({"Server 2022", "windows", "https://massgrave.dev/windows-server-links", "Windows Server 2022 x64", 6800, true});
    allImages.append({"Server 2019", "windows", "https://massgrave.dev/windows-server-links", "Windows Server 2019 x64", 6800, true});
    allImages.append({"ReactOS 0.4.14", "windows", "https://download.reactos.org/reactos/ReactOS/0.4.14/ReactOS-0.4.14-iso.zip", "ReactOS 0.4.14 (Windows compat)", 1200, false});
    allImages.append({"macOS Sequoia 15", "macos", "https://github.com/cocoonstack/cocoon-macos/releases/download/sequoia-15/sequoia-15.qcow2.xz", "macOS 15 Sequoia (qcow2)", 12000, true});
    allImages.append({"macOS Sonoma 14", "macos", "https://github.com/cocoonstack/cocoon-macos/releases/download/sonoma-14/sonoma-14.qcow2.xz", "macOS 14 Sonoma (qcow2)", 11000, true});
    allImages.append({"macOS Venturer", "macos", "https://github.com/the-dagger/macos-vm/releases/download/sequoia/sequoia.qcow2", "macOS Sequoia qcow2", 12000, true});
    allImages.append({"Mac OS X Tiger 10.4", "macos", "https://archive.org/download/mac-osx-tiger-10-4-ppc-installed-qcow2-image/osx-tiger_10.4.11_installed.qcow2", "Mac OS X Tiger 10.4 PPC (installed)", 5300, false});
    allImages.append({"Mac OS X Leopard 10.5", "macos", "https://archive.org/download/OsxLeopardInstall/Mac%20OS%20X%20Leopard%20Install%20DVD.iso", "Mac OS X Leopard 10.5 installer", 7800, false});
    allImages.append({"Snow Leopard 10.6", "macos", "https://ia801802.us.archive.org/18/items/10.6.7-10j3250-disk-images/10.6.7-10J4139-ACDT-OSX.dmg", "Mac OS X Snow Leopard 10.6 DMG", 2000, false});
    allImages.append({"Mac OS X Lion 10.7", "macos", "https://archive.org/download/macOS-X-images/Lion%2010.7.iso", "Mac OS X Lion 10.7 installer", 5500, false});
    allImages.append({"OS X Mountain Lion 10.8", "macos", "https://archive.org/download/macOS-X-images/Mountain%20Lion%2010.8.iso", "OS X Mountain Lion 10.8 installer", 5100, false});
    allImages.append({"OS X Mavericks 10.9", "macos", "https://archive.org/download/macOS-X-images/Mavericks%2010.9.iso", "OS X Mavericks 10.9 installer", 6500, false});
    allImages.append({"OS X Yosemite 10.10", "macos", "https://archive.org/download/macOS-X-images/Yosemite%2010.10.iso", "OS X Yosemite 10.10 installer", 6600, false});
    allImages.append({"OS X El Capitan 10.11", "macos", "https://archive.org/download/macOS-X-images/El%20Capitan%2010.11.iso", "OS X El Capitan 10.11 installer", 7100, false});
    allImages.append({"macOS Sierra 10.12", "macos", "https://archive.org/download/macOS-X-images/Sierra%2010.12.iso", "macOS Sierra 10.12 installer", 6200, false});
    allImages.append({"macOS High Sierra 10.13", "macos", "https://archive.org/download/macOS-X-images/High%20Sierra%2010.13.iso", "macOS High Sierra 10.13 installer", 5100, false});
    allImages.append({"macOS Mojave 10.14", "macos", "https://archive.org/download/macOS-X-images/Mojave%2010.14.iso", "macOS 10.14 Mojave", 5500, false});
    allImages.append({"macOS Catalina 10.15", "macos", "https://archive.org/download/macOS-X-images/Catalina%2010.15.iso", "macOS 10.15 Catalina", 7000, false});
    allImages.append({"macOS Big Sur 11", "macos", "https://archive.org/download/macOS-X-images/Big%20Sur%2011.iso", "macOS 11 Big Sur", 11500, false});
    allImages.append({"macOS Monterey 12", "macos", "https://archive.org/download/macOS-X-images/Monterey%2012.iso", "macOS 12 Monterey", 12000, false});
    allImages.append({"FreeBSD 14", "freebsd", "https://download.freebsd.org/ftp/releases/VM-IMAGES/14.1-RELEASE/amd64/Latest/FreeBSD-14.1-RELEASE-amd64.qcow2.xz", "FreeBSD 14 qcow2 VM", 1200, false});
    allImages.append({"FreeBSD 14 Installer", "freebsd", "https://download.freebsd.org/ftp/releases/ISO-IMAGES/14.1/FreeBSD-14.1-RELEASE-amd64-dvd1.iso", "FreeBSD 14 DVD installer", 4100, false});
    allImages.append({"OpenBSD 7.6", "openbsd", "https://cdn.openbsd.org/pub/OpenBSD/7.6/amd64/install76.iso", "OpenBSD 7.6 installer", 400, false});
    allImages.append({"NetBSD 10", "netbsd", "https://cdn.netbsd.org/pub/NetBSD/NetBSD-10.1/amd64/binary/iso/NetBSD-10.1-amd64.iso", "NetBSD 10 installer", 450, false});
    allImages.append({"DragonFly BSD 6.4", "dragonfly", "https://mirror.dragonflybsd.org/releases/6.4/DragonFlyBSD-6.4-RC3-x86_64.iso", "DragonFly BSD 6.4", 900, false});
    allImages.append({"AIX 7.3 TL1", "aix", "https://public.dhe.ibm.com/aix/fixes/7.3.0/aix733-0001.iso", "IBM AIX 7.3 (ppc64)", 800, false});
    allImages.append({"Oracle Solaris 11.4", "solaris", "https://www.oracle.com/solaris/solaris11/downloads/solaris11-install-downloads.html", "Oracle Solaris 11.4 x86", 1800, true});
    allImages.append({"OpenIndiana Hipster", "indiana", "http://mirror.transip.net/openindiana/dlc/isos/hipster/20240426/OI-hipster-gui-20240426.iso", "OpenIndiana Hipster 2024.04", 2000, true});
    allImages.append({"Qubes OS 4.3.1", "qubes", "https://ftp.qubes-os.org/iso/Qubes-R4.3.1-x86_64.iso", "Qubes OS 4.3 security", 8000, true});
    allImages.append({"FreeDOS 1.3 LiveCD", "dos", "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.3/official/FD13-LiveCD.zip", "FreeDOS 1.3 LiveCD", 380, false});
    // Windows rare/unreleased
    allImages.append({"Windows ME", "windows", "https://github.com/massgravel/Microsoft-Windows-ME-Download/releases/download/Windows-ME-EN-US-x86.iso/Windows-ME-EN-US-x86.iso", "Windows ME (x86)", 350, true});
    allImages.append({"Windows 98 SE", "windows", "https://github.com/massgravel/Microsoft-Windows-98-Download/releases/download/Windows-98-SE-EN-US-x86.iso/Windows-98-SE-EN-US-x86.iso", "Windows 98 SE (x86)", 400, true});
    allImages.append({"Windows 95", "windows", "https://github.com/massgravel/Microsoft-Windows-95-Download/releases/download/Windows-95-EN-US-x86.iso/Windows-95-EN-US-x86.iso", "Windows 95 (x86)", 300, true});
    allImages.append({"Windows NT 4.0", "windows", "https://github.com/massgravel/Microsoft-Windows-NT-Download/releases/download/Windows-NT-4.0-EN-US-x86.iso/Windows-NT-4.0-EN-US-x86.iso", "Windows NT 4.0 Workstation", 300, true});
    allImages.append({"Windows 3.11", "windows", "https://github.com/massgravel/Microsoft-Windows-3.1/releases/download/Windows-3.11-EN-US-x86.iso/Windows-3.11-EN-US-x86.iso", "Windows 3.11 for Workgroups", 200, true});
    allImages.append({"Windows 10 22H3", "windows-11", "https://github.com/massgravel/Microsoft-Windows-10-Download/releases/download/Windows-10-22H3-EN-US-x64.iso/Windows-10-22H3-EN-US-x64.iso", "Windows 10 22H3 final", 6700, true});
    allImages.append({"Windows Server 2025", "windows", "https://massgrave.dev/windows-server-links", "Windows Server 2025 x64", 6800, true});
    // OS/2
    allImages.append({"OS/2 Warp 4.0", "os2", "https://ftp.os2.org/os2/warp4/warp4.iso", "OS/2 Warp 4.0 (x86)", 800, false});
    allImages.append({"OS/2 eComStation 1.2", "os2", "https://ftp.os2.org/os2/ecom/ecs12.iso", "eComStation 1.2 (x86)", 900, false});
    allImages.append({"OS/2 ArcaOS 5.1", "os2", "https://www.arcanos.com/downloads/arcaos-51-x86.iso", "ArcaOS 5.1 (x86)", 1200, false});
    // Niche/rare - under Retro
    allImages.append({"Retro - Haiku R1", "haiku", "https://downloads.haiku-os.org/R1/haiku-r1-haiku-x86_64.iso", "Haiku R1 (x86_64)", 700, false});
    allImages.append({"Retro - KolibriOS", "linux", "https://kolibrios.org/en/kolibri-latest.iso", "KolibriOS (tiny GUI OS)", 10, false});
    allImages.append({"Retro - Syllable Desktop", "linux", "https://www.syllable.org/downloads/latest.iso", "Syllable Desktop", 300, false});
    allImages.append({"Retro - RISC OS", "other", "https://www.riscos.com/downloads/riscos-5.28.iso", "RISC OS 5.28 (ARM)", 500, false});
    allImages.append({"Retro - MINIX 3", "other", "https://sourceforge.net/projects/minix3/files/ISO%20images/minix3-3.4.0.iso/download", "MINIX 3 (x86)", 200, false});
    allImages.append({"Retro - Plan 9", "other", "https://9front.org/fd053312.iso", "Plan 9 from User Space", 400, false});

    auto *tabs = new QTabWidget(&dlg);

    { // Tab: Linux
        auto *tab = new QWidget;
        auto *tabLayout = new QVBoxLayout(tab);
        auto *combo = new QComboBox(tab);
        combo->addItem("Ubuntu");
        combo->addItem("Kubuntu");
        combo->addItem("Xubuntu");
        combo->addItem("Lubuntu");
        combo->addItem("Linux Mint");
        combo->addItem("Debian");
        combo->addItem("Pop!_OS");
        combo->addItem("Fedora");
        combo->addItem("Manjaro");
        combo->addItem("MX Linux");
        combo->addItem("Zorin");
        combo->addItem("Peppermint");
        combo->addItem("Kali");
        combo->addItem("Parrot");
        combo->addItem("Alpine");
        combo->addItem("Arch");
        combo->addItem("NixOS");
        combo->addItem("openSUSE");
        combo->addItem("Gentoo");
        combo->addItem("Void");
        combo->addItem("Rocky");
        combo->addItem("AlmaLinux");
        combo->addItem("Slackware");
        auto *listWidget = new QListWidget(tab);
        tabLayout->addWidget(combo);
        tabLayout->addWidget(listWidget);
        tabs->addTab(tab, "Linux");
        auto updateListLinux = [=](const QString &distroName) {
            listWidget->clear();
            for (const auto &img : allImages) {
                if (img.name.contains(distroName, Qt::CaseInsensitive) && !distroName.isEmpty()) {
                    auto *item = new QListWidgetItem(img.name);
                    item->setData(Qt::UserRole, img.url);
                    item->setData(Qt::UserRole + 1, img.name);
                    item->setData(Qt::UserRole + 2, img.icon);
                    item->setData(Qt::UserRole + 3, img.efi);
                    item->setData(Qt::UserRole + 4, img.desc);
                    item->setSizeHint(QSize(0, 50));
                    QString _iconFile = img.icon;
                    if (_iconFile == "dragonfly" || _iconFile == "void" || _iconFile == "qubes" || _iconFile == "dos" || _iconFile == "indiana") _iconFile = "linux";
                    else if (_iconFile == "aix") _iconFile = "SUSE";
                    else if (_iconFile == "haiku") _iconFile = "haiku-os";
                    else if (_iconFile == "os2") _iconFile = "os2";
                    else if (_iconFile == "aix") _iconFile = "SUSE";
                    item->setIcon(QIcon(QString(":/icons/%1.png").arg(_iconFile)));
                    listWidget->addItem(item);
                }
            }
        };
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int idx) {
            updateListLinux(combo->itemText(idx));
        });
        updateListLinux(combo->currentText());
    }
    { // Tab: Windows
        auto *tab = new QWidget;
        auto *tabLayout = new QVBoxLayout(tab);
        auto *combo = new QComboBox(tab);
        combo->addItem("Windows 11");
        combo->addItem("Windows 10");
        combo->addItem("Windows 8.1");
        combo->addItem("Windows 7");
        combo->addItem("Windows XP");
        combo->addItem("Server");
        combo->addItem("ReactOS");
        auto *listWidget = new QListWidget(tab);
        tabLayout->addWidget(combo);
        tabLayout->addWidget(listWidget);
        tabs->addTab(tab, "Windows");
        auto updateListWindows = [=](const QString &distroName) {
            listWidget->clear();
            for (const auto &img : allImages) {
                if (img.name.contains(distroName, Qt::CaseInsensitive) && !distroName.isEmpty()) {
                    auto *item = new QListWidgetItem(img.name);
                    item->setData(Qt::UserRole, img.url);
                    item->setData(Qt::UserRole + 1, img.name);
                    item->setData(Qt::UserRole + 2, img.icon);
                    item->setData(Qt::UserRole + 3, img.efi);
                    item->setData(Qt::UserRole + 4, img.desc);
                    item->setSizeHint(QSize(0, 50));
                    QString _iconFile = img.icon;
                    if (_iconFile == "dragonfly" || _iconFile == "void" || _iconFile == "qubes" || _iconFile == "dos" || _iconFile == "indiana") _iconFile = "linux";
                    else if (_iconFile == "aix") _iconFile = "SUSE";
                    else if (_iconFile == "haiku") _iconFile = "haiku-os";
                    else if (_iconFile == "os2") _iconFile = "os2";
                    else if (_iconFile == "aix") _iconFile = "SUSE";
                    item->setIcon(QIcon(QString(":/icons/%1.png").arg(_iconFile)));
                    listWidget->addItem(item);
                }
            }
        };
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int idx) {
            updateListWindows(combo->itemText(idx));
        });
        updateListWindows(combo->currentText());
    }
    { // Tab: macOS
        auto *tab = new QWidget;
        auto *tabLayout = new QVBoxLayout(tab);
        auto *combo = new QComboBox(tab);
        combo->addItem("Sequoia");
        combo->addItem("Sonoma");
        combo->addItem("Ventura");
        combo->addItem("10.4-10.7");
        combo->addItem("10.8-10.10");
        combo->addItem("10.11-10.13");
        combo->addItem("10.14-10.15");
        combo->addItem("11-12");
        auto *listWidget = new QListWidget(tab);
        tabLayout->addWidget(combo);
        tabLayout->addWidget(listWidget);
        tabs->addTab(tab, "macOS");
        auto updateListMac = [=](const QString &distroName) {
            listWidget->clear();
            for (const auto &img : allImages) {
                if (img.name.contains(distroName, Qt::CaseInsensitive) && !distroName.isEmpty()) {
                    auto *item = new QListWidgetItem(img.name);
                    item->setData(Qt::UserRole, img.url);
                    item->setData(Qt::UserRole + 1, img.name);
                    item->setData(Qt::UserRole + 2, img.icon);
                    item->setData(Qt::UserRole + 3, img.efi);
                    item->setData(Qt::UserRole + 4, img.desc);
                    item->setSizeHint(QSize(0, 50));
                    QString _iconFile = img.icon;
                    if (_iconFile == "dragonfly" || _iconFile == "void" || _iconFile == "qubes" || _iconFile == "dos" || _iconFile == "indiana") _iconFile = "linux";
                    else if (_iconFile == "aix") _iconFile = "SUSE";
                    else if (_iconFile == "haiku") _iconFile = "haiku-os";
                    else if (_iconFile == "os2") _iconFile = "os2";
                    else if (_iconFile == "aix") _iconFile = "SUSE";
                    item->setIcon(QIcon(QString(":/icons/%1.png").arg(_iconFile)));
                    listWidget->addItem(item);
                }
            }
        };
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int idx) {
            updateListMac(combo->itemText(idx));
        });
        updateListMac(combo->currentText());
    }
    { // Tab: BSD
        auto *tab = new QWidget;
        auto *tabLayout = new QVBoxLayout(tab);
        auto *combo = new QComboBox(tab);
        combo->addItem("FreeBSD");
        combo->addItem("OpenBSD");
        combo->addItem("NetBSD");
        combo->addItem("DragonFly");
        auto *listWidget = new QListWidget(tab);
        tabLayout->addWidget(combo);
        tabLayout->addWidget(listWidget);
        tabs->addTab(tab, "BSD");
        auto updateListBSD = [=](const QString &distroName) {
            listWidget->clear();
            for (const auto &img : allImages) {
                if (img.name.contains(distroName, Qt::CaseInsensitive) && !distroName.isEmpty()) {
                    auto *item = new QListWidgetItem(img.name);
                    item->setData(Qt::UserRole, img.url);
                    item->setData(Qt::UserRole + 1, img.name);
                    item->setData(Qt::UserRole + 2, img.icon);
                    item->setData(Qt::UserRole + 3, img.efi);
                    item->setData(Qt::UserRole + 4, img.desc);
                    item->setSizeHint(QSize(0, 50));
                    QString _iconFile = img.icon;
                    if (_iconFile == "dragonfly" || _iconFile == "void" || _iconFile == "qubes" || _iconFile == "dos" || _iconFile == "indiana") _iconFile = "linux";
                    else if (_iconFile == "aix") _iconFile = "SUSE";
                    else if (_iconFile == "haiku") _iconFile = "haiku-os";
                    else if (_iconFile == "os2") _iconFile = "os2";
                    else if (_iconFile == "aix") _iconFile = "SUSE";
                    item->setIcon(QIcon(QString(":/icons/%1.png").arg(_iconFile)));
                    listWidget->addItem(item);
                }
            }
        };
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int idx) {
            updateListBSD(combo->itemText(idx));
        });
        updateListBSD(combo->currentText());
    }
    { // Tab: Other
        auto *tab = new QWidget;
        auto *tabLayout = new QVBoxLayout(tab);
        auto *combo = new QComboBox(tab);
        combo->addItem("AIX");
        combo->addItem("Solaris");
        combo->addItem("Qubes");
        combo->addItem("Retro");
        combo->addItem("OS/2");
        combo->addItem("Haiku");
        auto *listWidget = new QListWidget(tab);
        tabLayout->addWidget(combo);
        tabLayout->addWidget(listWidget);
        tabs->addTab(tab, "Other");
        auto updateListOther = [=](const QString &distroName) {
            listWidget->clear();
            for (const auto &img : allImages) {
                if (img.name.contains(distroName, Qt::CaseInsensitive) && !distroName.isEmpty()) {
                    auto *item = new QListWidgetItem(img.name);
                    item->setData(Qt::UserRole, img.url);
                    item->setData(Qt::UserRole + 1, img.name);
                    item->setData(Qt::UserRole + 2, img.icon);
                    item->setData(Qt::UserRole + 3, img.efi);
                    item->setData(Qt::UserRole + 4, img.desc);
                    item->setSizeHint(QSize(0, 50));
                    QString _iconFile = img.icon;
                    if (_iconFile == "dragonfly" || _iconFile == "void" || _iconFile == "qubes" || _iconFile == "dos" || _iconFile == "indiana") _iconFile = "linux";
                    else if (_iconFile == "aix") _iconFile = "SUSE";
                    else if (_iconFile == "haiku") _iconFile = "haiku-os";
                    else if (_iconFile == "os2") _iconFile = "os2";
                    else if (_iconFile == "aix") _iconFile = "SUSE";
                    item->setIcon(QIcon(QString(":/icons/%1.png").arg(_iconFile)));
                    listWidget->addItem(item);
                }
            }
        };
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int idx) {
            updateListOther(combo->itemText(idx));
        });
        updateListOther(combo->currentText());
    }

    // Filter
    auto filterSearch = [=](const QString &filter) {
        for (int i = 0; i < tabs->count(); i++) {
            auto *tab = tabs->widget(i);
            auto *lw = tab->findChild<QListWidget *>();
            if (!lw) continue;
            for (int j = 0; j < lw->count(); j++) {
                auto *item = lw->item(j);
                item->setHidden(!filter.isEmpty() && !item->text().contains(filter, Qt::CaseInsensitive));
            }
        }
    };

    connect(searchEdit, &QLineEdit::textChanged, this, filterSearch);

    auto *downloadBtn = new QPushButton("Download & Create VM");
    downloadBtn->setObjectName("actionBtn");
    auto *urlBtn = new QPushButton("Open URL in Browser");
    urlBtn->setObjectName("actionBtnSecondary");
    auto *closeBtn = new QPushButton("Close");
    auto *btnLayout = new QHBoxLayout;
    btnLayout->addWidget(downloadBtn);
    btnLayout->addWidget(urlBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    auto doDownload = [this, tabs]() {
        auto *currentTab = tabs->currentWidget();
        if (!currentTab) return;
        auto *listWidget = currentTab->findChild<QListWidget *>();
        auto *item = listWidget ? listWidget->currentItem() : nullptr;
        if (!item) {
            QMessageBox::information(tabs, "Select", "Select a VM image first");
            return;
        }
        QString url = item->data(Qt::UserRole).toString();
        QString name = item->data(Qt::UserRole + 1).toString();
        QString icon = item->data(Qt::UserRole + 2).toString();
        bool efi = item->data(Qt::UserRole + 3).toBool();
        if (url.isEmpty()) return;

        QString fileName = QUrl(url).fileName();
        if (fileName.isEmpty()) fileName = name.toLower().replace(' ', '-') + ".img";

        QString savePath = QFileDialog::getSaveFileName(tabs,
            "Save Image", QDir::homePath() + "/" + fileName,
            "Disk Images (*.img *.qcow2 *.iso *.xz);;All Files (*)");
        if (savePath.isEmpty()) return;

        statusBar()->showMessage("Downloading " + name + "...", 0);
        QString downloader;
        if (QFileInfo::exists("/usr/bin/curl")) downloader = "curl";
        else if (QFileInfo::exists("/usr/bin/wget")) downloader = "wget";
        if (downloader.isEmpty()) {
            QDesktopServices::openUrl(QUrl(url));
            QMessageBox::information(tabs, "Download", "No curl/wget found.");
            return;
        }

        QProcess *proc = new QProcess(this);
        if (downloader == "curl")
            proc->start("curl", {"-L", "-o", savePath, url});
        else
            proc->start("wget", {"-O", savePath, url});

        // Progress dialog (parent = this, NOT &dlg, so it works)
        auto *progressDlg = new QProgressDialog("Downloading " + name + "...",
            "Cancel", 0, 100, this);
        progressDlg->setWindowTitle("Downloading");
        progressDlg->setModal(false);
        progressDlg->setMinimumDuration(0);
        progressDlg->setValue(0);
        progressDlg->setAttribute(Qt::WA_DeleteOnClose);
        progressDlg->show();

        auto totalSize = std::make_shared<qint64>(0);
        QProcess *sizeProc = new QProcess(this);
        sizeProc->start("curl", {"-s", "-I", "-L", url});
        QObject::connect(sizeProc, &QProcess::finished, this, [=](int) {
            QString output = sizeProc->readAllStandardOutput();
            sizeProc->deleteLater();
            int pos = output.indexOf("Content-Length:");
            if (pos >= 0) {
                QStringList lines = output.mid(pos).split('\n');
                for (auto &line : lines) {
                    line = line.trimmed();
                    if (line.startsWith("Content-Length:")) {
                        bool ok = false;
                        *totalSize = line.section(':', 1).trimmed().toLongLong(&ok);
                        if (ok)
                            progressDlg->setMaximum(*totalSize);
                        break;
                    }
                }
            }
        });

        auto onFinished = [this, proc, progressDlg, savePath, name]() {
            proc->deleteLater();
            progressDlg->close();
            statusBar()->clearMessage();
            if (proc->exitCode() != 0) {
                QMessageBox::warning(this, "Download Failed", "Failed to download the image.");
                return;
            }
            QMessageBox::information(this, "Done", "Saved:\n" + savePath);
        };

        QObject::connect(proc, &QProcess::finished, this, onFinished);

        QTimer *pollTimer = new QTimer(this);
        pollTimer->setInterval(500);
        QObject::connect(pollTimer, &QTimer::timeout, this, [=]() {
            if (progressDlg->wasCanceled()) {
                proc->kill();
                pollTimer->stop();
                progressDlg->close();
                return;
            }
            QFileInfo fi(savePath);
            if (fi.exists()) {
                qint64 currentSize = fi.size();
                int pct = *totalSize > 0 ? (int)((double)currentSize / *totalSize * 100) : 0;
                progressDlg->setValue(qMin(pct, 100));
                progressDlg->setLabelText(
                    QString("Downloading %1... (%2 / %3)")
                        .arg(name)
                        .arg(formatSizeMB(currentSize / 1024 / 1024))
                        .arg(*totalSize > 0 ? formatSizeMB(*totalSize / 1024 / 1024) : "?"));
            }
            if (proc->state() == QProcess::NotRunning && proc->exitCode() != 0) {
                pollTimer->stop();
                onFinished();
            }
        });
        pollTimer->start();
    };

    connect(downloadBtn, &QPushButton::clicked, this, doDownload);
    connect(urlBtn, &QPushButton::clicked, this, [this, tabs]() {
        auto *lw = tabs->currentWidget()->findChild<QListWidget *>();
        auto *item = lw ? lw->currentItem() : nullptr;
        if (!item) return;
        QDesktopServices::openUrl(QUrl(item->data(Qt::UserRole).toString()));
    });
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();
}

void MainWindow::onUserGuide() {
    QDialog dlg(this);
    dlg.setWindowTitle("User Guide");
    dlg.setMinimumSize(500, 450);
    dlg.resize(600, 500);

    auto *layout = new QVBoxLayout(&dlg);

    auto *tabs = new QTabWidget;

    // Getting Started tab
    auto *startTab = new QWidget;
    auto *sl = new QVBoxLayout(startTab);
    auto *startText = new QTextEdit;
    startText->setReadOnly(true);
    startText->setHtml(R"(
<h3>Getting Started</h3>
<p><b>VM Manager</b> is a graphical interface for QEMU virtual machines.</p>

<h4>Creating a VM</h4>
<ol>
<li>Click <b>New VM</b> in the toolbar or on the welcome screen</li>
<li>Enter a name for your VM</li>
<li>Configure CPU cores, memory, disk size</li>
<li>Set up networking (User mode works out of the box)</li>
<li>Click OK to create the VM and disk image</li>
</ol>

<h4>Starting a VM</h4>
<ol>
<li>Select the VM from the sidebar</li>
<li>Click <b>Start</b> in the toolbar</li>
<li>The serial console will show boot output</li>
<li>For graphical display, use the <b>Open Display</b> button (VNC)</li>
</ol>

<h4>Installation Media</h4>
<p>You'll need an ISO file to install an OS. In the VM Settings, configure
the VM to boot from your ISO using the <b>Extra Args</b> field:
<code>-cdrom /path/to/your.iso -boot d</code></p>

<p>Or use the <b>Settings</b> tab after creating the VM to add extra arguments.</p>
    )");
    sl->addWidget(startText);
    tabs->addTab(startTab, "Getting Started");

    // AI tab
    auto *aiTab = new QWidget;
    auto *ail = new QVBoxLayout(aiTab);
    auto *aiText = new QTextEdit;
    aiText->setReadOnly(true);
    aiText->setHtml(R"(
<h3>AI Assistant</h3>
<p>The AI Assistant helps you configure VMs and troubleshoot problems.</p>

<h4>Setup</h4>
<p>The AI connects to a local <a href='https://ollama.ai'>Ollama</a> instance
or any OpenAI-compatible API.</p>
<ol>
<li>Install Ollama: <code>curl -fsSL https://ollama.ai/install.sh | sh</code></li>
<li>Pull a model: <code>ollama pull llama3.2</code></li>
<li>Click <b>AI</b> in the toolbar, then <b>Settings</b> to configure</li>
<li>Default: <code>http://localhost:11434</code> with model <code>llama3.2</code></li>
</ol>

<h4>What the AI can do</h4>
<ul>
<li>Generate VM configurations from descriptions</li>
<li>Troubleshoot QEMU error messages</li>
<li>Recommend settings for specific operating systems</li>
<li>Explain virtualization concepts</li>
</ul>

<h4>Examples</h4>
<p><i>"Create a Windows 11 VM with 8GB RAM and 4 cores"</i></p>
<p><i>"My VM won't start, it says 'KVM not supported'"</i></p>
<p><i>"What's the best config for Ubuntu Server?"</i></p>
    )");
    ail->addWidget(aiText);
    tabs->addTab(aiTab, "AI Assistant");

    // FAQ tab
    auto *faqTab = new QWidget;
    auto *fl = new QVBoxLayout(faqTab);
    auto *faqText = new QTextEdit;
    faqText->setReadOnly(true);
    faqText->setHtml(R"(
<h3>FAQ & Troubleshooting</h3>

<h4>My VM won't start</h4>
<p>Common causes:</p>
<ul>
<li><b>KVM not available:</b> Enable virtualization in BIOS/EFI, or disable KVM in VM settings</li>
<li><b>OVMF not found:</b> Install OVMF package: <code>sudo apt install ovmf</code></li>
<li><b>Port in use:</b> Change the VNC port if 5900 is already in use</li>
<li><b>Disk path invalid:</b> Check the disk image path in VM settings</li>
</ul>

<h4>How do I install an OS?</h4>
<p>Add the ISO as a cdrom in Extra Args: <code>-cdrom /path/to/os.iso -boot d</code></p>

<h4>No display when using VNC</h4>
<p>Install a VNC viewer: <code>sudo apt install gvncviewer tigervnc-viewer</code></p>
<p>Then click <b>Open Display (VNC)</b> when the VM is running.</p>

<h4>Network doesn't work in the VM</h4>
<p>User-mode networking (default) provides NAT. The VM gets IP via DHCP.
For bridged networking, set up a bridge interface and run as root.</p>

<h4>Can I use USB passthrough?</h4>
<p>Add to Extra Args: <code>-usb -device usb-host,vendorid=0xVVVV,productid=0xPPPP</code></p>
<p>Find your device IDs with <code>lsusb</code>.</p>
    )");
    fl->addWidget(faqText);
    tabs->addTab(faqTab, "FAQ");

    layout->addWidget(tabs);

    auto *closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    layout->addWidget(closeBtn);

    dlg.exec();
}

void MainWindow::onShowAI() {
    m_aiDock->setVisible(!m_aiDock->isVisible());
    m_aiAction->setChecked(m_aiDock->isVisible());
}

void MainWindow::onExportVM() {
    QString id = selectedVMId();
    if (id.isEmpty()) {
        statusBar()->showMessage("Select a VM to export", 3000);
        return;
    }

    auto cfg = m_manager->getConfig(id);
    if (cfg.id.isEmpty()) return;

    QString defaultName = cfg.name.toLower().replace(' ', '-') + ".json";
    QString path = QFileDialog::getSaveFileName(this, "Export VM Config",
        QDir::homePath() + "/" + defaultName,
        "VM Config (*.json);;All Files (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(cfg.toJson());
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        statusBar()->showMessage("Exported: " + path, 3000);
    } else {
        QMessageBox::warning(this, "Export Error",
            "Failed to write: " + file.errorString());
    }
}

void MainWindow::onImportVM() {
    QString path = QFileDialog::getOpenFileName(this, "Import VM Config",
        QDir::homePath(), "VM Config (*.json);;All Files (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Import Error",
            "Failed to read: " + file.errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        QMessageBox::warning(this, "Import Error", "Invalid config file");
        return;
    }

    VMConfig cfg = VMConfig::fromJson(doc.object());
    if (cfg.name.isEmpty()) {
        QMessageBox::warning(this, "Import Error", "Invalid VM config (no name)");
        return;
    }

    cfg.id = VMConfig::generateId(); // New ID to avoid conflict

    // Check for name conflict
    bool conflict = false;
    for (const auto &existing : m_manager->allConfigs()) {
        if (existing.name == cfg.name) {
            conflict = true;
            break;
        }
    }
    if (conflict) {
        cfg.name += " (imported)";
    }

    // Validate disk path
    if (!cfg.disk.path.isEmpty() && !QFileInfo::exists(cfg.disk.path)) {
        auto reply = QMessageBox::question(this, "Disk Not Found",
            QString("Disk image not found:\n%1\n\nImport config anyway?")
                .arg(cfg.disk.path),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) return;
    }

    m_manager->saveConfig(cfg);
    refreshVMList();
    statusBar()->showMessage("Imported: " + cfg.name, 3000);
}

void MainWindow::onImportUTM() {
    QString dir = QFileDialog::getExistingDirectory(this,
        "Select UTM Bundle", QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;
    if (!dir.endsWith(".utm", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, "Not a UTM Bundle",
            "Please select a .utm bundle directory");
        return;
    }

    // Check for config.plist
    QString plistPath = dir + "/config.plist";
    if (!QFileInfo::exists(plistPath)) {
        QMessageBox::warning(this, "Import Error",
            "config.plist not found in UTM bundle");
        return;
    }

    // Use Python to parse config.plist and output JSON
    QProcess py;
    py.start("python3", {"-c", R"(
import plistlib, json, sys
with open(sys.argv[1], 'rb') as f:
    d = plistlib.load(f)
print(json.dumps(d, default=str))
)" , plistPath});
    if (!py.waitForFinished(5000) || py.exitCode() != 0) {
        QMessageBox::warning(this, "Import Error",
            "Failed to parse config.plist:\n" + py.readAllStandardError());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(py.readAllStandardOutput());
    if (!doc.isObject()) {
        QMessageBox::warning(this, "Import Error", "Invalid plist format");
        return;
    }

    QJsonObject root = doc.object();
    QJsonObject sys = root["System"].toObject();
    QJsonObject info = root["Info"].toObject();
    QJsonObject net = root["Networking"].toObject();
    QJsonObject snd = root["Sound"].toObject();
    QJsonObject disp = root["Display"].toObject();
    QJsonArray drives = root["Drives"].toArray();

    // Map architecture
    QString utmArch = sys["Architecture"].toString();
    QString arch;
    if (utmArch == "x86_64") arch = "x86_64";
    else if (utmArch == "aarch64") arch = "aarch64";
    else if (utmArch == "arm") arch = "arm";
    else if (utmArch == "sparc") arch = "sparc";
    else if (utmArch == "sparc64") arch = "sparc64";
    else if (utmArch == "ppc" || utmArch == "ppc64") arch = "ppc64";
    else if (utmArch == "m68k") arch = "m68k";
    else if (utmArch == "mips" || utmArch == "mips64") arch = "mips64el";
    else if (utmArch == "riscv64") arch = "riscv64";
    else if (utmArch == "loongarch64") arch = "loongarch64";
    else {
        QMessageBox::warning(this, "Import Error",
            "Unsupported architecture: " + utmArch);
        return;
    }

    QString machine = sys["Target"].toString();
    if (machine.isEmpty()) {
        ArchInfo ai = VMConfig::archInfo(arch);
        machine = ai.defaultMachine;
    }

    VMConfig cfg;
    cfg.name = QFileInfo(dir).baseName();
    cfg.id = VMConfig::generateId();
    cfg.arch = arch;
    cfg.machine = machine;
    cfg.cpuCores = qMax(1, sys["CPUCount"].toInt());
    cfg.memoryMB = qMax(64, sys["Memory"].toInt());
    cfg.efiBoot = sys["BootUefi"].toBool();

    // Map boot device
    QString bootDev = sys["BootDevice"].toString();
    if (bootDev == "hdd") cfg.bootOrder = "disk,cdrom";
    else if (bootDev == "cdrom") cfg.bootOrder = "cdrom,disk";
    else cfg.bootOrder = "disk,cdrom";

    // Machine properties → extraArgs
    QString machProps = sys["MachineProperties"].toString();
    if (!machProps.isEmpty())
        cfg.extraArgs = "-machine " + machProps;

    // Optional UUID
    QString sysUuid = sys["SystemUUID"].toString();
    if (!sysUuid.isEmpty()) cfg.id = sysUuid;

    // Display card
    cfg.vgaModel = disp["DisplayCard"].toString();
    if (cfg.vgaModel.isEmpty()) cfg.vgaModel = "virtio";
    cfg.displayMode = "vnc";

    // Networking
    cfg.network.enabled = net["NetworkEnabled"].toBool(true);
    cfg.network.model = net["NetworkCard"].toString();
    if (cfg.network.model.isEmpty()) cfg.network.model = "virtio-net";
    QString netMode = net["NetworkMode"].toString();
    cfg.network.mode = (netMode == "emulated" || netMode == "user") ? "user" : "user";
    cfg.network.mac = net["NetworkCardMAC"].toString();

    // Sound
    cfg.audioEnabled = snd["SoundEnabled"].toBool(false);
    cfg.soundModel = snd["SoundCard"].toString();
    if (cfg.soundModel.isEmpty()) cfg.soundModel = "hda";

    // Input
    cfg.usbTablet = !root["Input"].toObject()["InputLegacy"].toBool();

    // Clipboard sharing
    cfg.clipboardShare = root["Sharing"].toObject()["ClipboardSharing"].toBool();

    // Icon & notes
    cfg.iconName = info["Icon"].toString();
    cfg.notes = info["Notes"].toString();

    // Drives
    QString imagesDir = dir + "/Images/";
    bool firstDisk = true;
    for (const auto &dv : drives) {
        QJsonObject d = dv.toObject();
        QString imgType = d["ImageType"].toString();
        QString path = d["ImagePath"].toString();
        QString absPath = QDir(imagesDir).absoluteFilePath(path);
        if (!QFileInfo::exists(absPath)) continue;

        if (imgType == "bios") {
            // BIOS file: add as -bios or extra args
            if (!cfg.extraArgs.isEmpty()) cfg.extraArgs += " ";
            cfg.extraArgs += "-bios \"" + absPath + "\"";
            continue;
        }

        DiskDrive dd;
        dd.path = absPath;
        dd.controller = d["InterfaceType"].toString("ide");

        // Detect format from file extension
        if (absPath.endsWith(".qcow2", Qt::CaseInsensitive))
            dd.format = "qcow2";
        else if (absPath.endsWith(".raw", Qt::CaseInsensitive))
            dd.format = "raw";
        else if (absPath.endsWith(".img", Qt::CaseInsensitive))
            dd.format = "raw";
        else
            dd.format = "qcow2";

        if (imgType == "cd") {
            dd.driveType = 1;
            dd.format = "raw";
            cfg.extraDrives.append(dd);
        } else if (imgType == "disk") {
            if (firstDisk) {
                cfg.disk = dd;
                firstDisk = false;
            } else {
                cfg.extraDrives.append(dd);
            }
        }
    }

    // KVM
    ArchInfo ai = VMConfig::archInfo(arch);
    cfg.kvmEnabled = ai.supportsKvm;

    // Save
    m_manager->saveConfig(cfg);
    refreshVMList();
    statusBar()->showMessage("Imported UTM VM: " + cfg.name, 3000);
}

void MainWindow::toggleSidebar() {
    m_sidebarWidget->setVisible(m_toggleSidebarAction->isChecked());
}

void MainWindow::toggleToolbar() {
    m_toolbar->setVisible(m_toggleToolbarAction->isChecked());
}

void MainWindow::toggleAI() {
    m_aiDock->setVisible(!m_aiDock->isVisible());
    m_aiAction->setChecked(m_aiDock->isVisible());
}

void MainWindow::filterVMList(const QString &text) {
    int visible = 0;
    for (int i = 0; i < m_vmList->topLevelItemCount(); i++) {
        auto *item = m_vmList->topLevelItem(i);
        bool match = text.isEmpty() ||
            item->text(0).contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
        if (match) visible++;
    }

    if (visible == 0 && m_vmList->topLevelItemCount() > 0) {
        m_contentStack->setCurrentWidget(m_emptyPage);
        clearDetails();
    } else if (visible > 0 && m_contentStack->currentWidget() == m_emptyPage) {
        m_contentStack->setCurrentWidget(m_detailsPage);
    }

    if (visible > 0) {
        m_vmList->blockSignals(true);
        for (int i = 0; i < m_vmList->topLevelItemCount(); i++) {
            auto *item = m_vmList->topLevelItem(i);
            if (!item->isHidden()) {
                m_vmList->setCurrentItem(item);
                break;
            }
        }
        m_vmList->blockSignals(false);
        onVMSelected();
    }
}

void MainWindow::onCloneVM() {
    QString id = selectedVMId();
    if (id.isEmpty()) {
        statusBar()->showMessage("Select a VM to clone", 3000);
        return;
    }

    auto cfg = m_manager->getConfig(id);
    if (cfg.id.isEmpty()) return;

    bool ok;
    QString newName = QInputDialog::getText(this, "Clone VM",
        "New VM name:", QLineEdit::Normal,
        cfg.name + " (clone)", &ok);
    if (!ok || newName.trimmed().isEmpty()) return;

    cfg.id = VMConfig::generateId();
    cfg.name = newName.trimmed();

    bool copyDisk = false;
    if (!cfg.disk.path.isEmpty() && QFileInfo::exists(cfg.disk.path)) {
        auto reply = QMessageBox::question(this, "Clone Disk",
            "Clone shares the disk image by default.\n"
            "Copy the disk image for full isolation?",
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel) return;
        copyDisk = (reply == QMessageBox::Yes);
    }

    if (copyDisk) {
        QString oldPath = cfg.disk.path;
        QFileInfo fi(oldPath);
        QString newPath = fi.dir().absoluteFilePath(
            fi.completeBaseName() + "-" + cfg.id.left(8) + "." + fi.suffix());
        QProcess proc;
        proc.start("qemu-img", {"create", "-f", cfg.disk.format,
                  "-b", oldPath, "-F", cfg.disk.format, newPath});
        if (proc.waitForFinished(30000) && proc.exitCode() == 0) {
            cfg.disk.path = newPath;
            statusBar()->showMessage("Disk copied to: " + newPath, 5000);
        } else {
            QMessageBox::warning(this, "Clone Warning",
                "Disk copy failed. Clone will share the original disk.");
        }
    }

    m_manager->saveConfig(cfg);
    refreshVMList();
    statusBar()->showMessage("Cloned VM: " + cfg.name, 3000);
}

void MainWindow::onForceStopVM() {
    QString id = selectedVMId();
    if (id.isEmpty()) return;

    auto *task = m_manager->taskForVM(id);
    if (!task || !task->isRunning()) return;

    auto reply = QMessageBox::question(this, "Force Stop",
        "Forcefully kill the QEMU process?\n"
        "This may cause data loss.",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        task->forceStop();
        statusBar()->showMessage("VM forcefully stopped", 3000);
    }
}

void MainWindow::onSnapshots() {
    QString id = selectedVMId();
    if (id.isEmpty()) return;

    auto *task = m_manager->taskForVM(id);
    if (!task || !task->isRunning()) {
        QMessageBox::information(this, "Snapshots",
            "VM must be running to manage snapshots.\n"
            "Snapshots are saved in the qcow2 disk image.");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Snapshots - " + m_manager->getConfig(id).name);
    dlg.resize(600, 400);

    auto *layout = new QVBoxLayout(&dlg);

    auto *desc = new QLabel("Manage snapshots for " + m_manager->getConfig(id).name);
    layout->addWidget(desc);

    auto *snapTable = new QTableWidget;
    snapTable->setColumnCount(3);
    snapTable->setHorizontalHeaderLabels({"Name", "Date/Time", "Type"});
    snapTable->horizontalHeader()->setStretchLastSection(true);
    snapTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    snapTable->setAlternatingRowColors(true);
    snapTable->setMinimumHeight(150);
    layout->addWidget(snapTable, 1);

    auto *btnLayout = new QHBoxLayout;
    auto *refreshBtn = new QPushButton("Refresh");
    auto *createBtn = new QPushButton("Create Snapshot");
    auto *restoreBtn = new QPushButton("Restore");
    auto *deleteBtn = new QPushButton("Delete");
    auto *closeBtn = new QPushButton("Close");
    refreshBtn->setObjectName("actionButtonTertiary");
    createBtn->setObjectName("actionBtn");
    restoreBtn->setObjectName("actionBtn");
    deleteBtn->setObjectName("actionBtnDanger");
    closeBtn->setObjectName("actionButtonTertiary");
    btnLayout->addWidget(refreshBtn);
    btnLayout->addWidget(createBtn);
    btnLayout->addWidget(restoreBtn);
    btnLayout->addWidget(deleteBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    auto refreshSnapList = [this, task, snapTable]() {
        snapTable->setRowCount(0);
        task->querySnapshots();
    };

    connect(refreshBtn, &QPushButton::clicked, refreshSnapList);

    connect(task, &VMTask::snapshotListReceived, this, [snapTable](const QStringList &names) {
        snapTable->setRowCount(names.size());
        for (int i = 0; i < names.size(); i++) {
            snapTable->setItem(i, 0, new QTableWidgetItem(names[i]));
            snapTable->setItem(i, 1, new QTableWidgetItem("—"));
            snapTable->setItem(i, 2, new QTableWidgetItem("internal"));
        }
    });

    connect(createBtn, &QPushButton::clicked, this, [this, task, &refreshSnapList]() {
        bool ok;
        QString name = QInputDialog::getText(this, "Snapshot Name",
            "Enter snapshot name:", QLineEdit::Normal, "", &ok);
        if (!ok || name.isEmpty()) return;
        task->sendMonitorCommand(QString("savevm %1\n").arg(name));
        statusBar()->showMessage("Snapshot created: " + name, 3000);
        QTimer::singleShot(1000, this, [&refreshSnapList]() {
            refreshSnapList();
        });
    });

    connect(restoreBtn, &QPushButton::clicked, this, [this, task, snapTable]() {
        auto *item = snapTable->currentItem();
        if (!item) return;
        QString name = item->text();
        auto reply = QMessageBox::question(this, "Restore Snapshot",
            "Restore snapshot \"" + name + "\"?\n"
            "The VM will be reset to the snapshot state.",
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            task->sendMonitorCommand(QString("loadvm %1\n").arg(name));
            statusBar()->showMessage("Restoring snapshot: " + name, 3000);
        }
    });

    connect(deleteBtn, &QPushButton::clicked, this, [this, task, &refreshSnapList, snapTable]() {
        auto *item = snapTable->currentItem();
        if (!item) return;
        QString name = item->text();
        auto reply = QMessageBox::question(this, "Delete Snapshot",
            "Delete snapshot \"" + name + "\"?",
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            task->sendMonitorCommand(QString("delvm %1\n").arg(name));
            statusBar()->showMessage("Deleted snapshot: " + name, 3000);
            QTimer::singleShot(1000, this, [&refreshSnapList]() {
                refreshSnapList();
            });
        }
    });

    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    refreshSnapList();
    dlg.exec();
}

void MainWindow::clearDetails() {
    m_vmNameLabel->clear();
    m_vmStatusLabel->clear();

    m_vmCpuLabel->setText("-");
    m_vmMemoryLabel->setText("-");
    m_vmDiskLabel->setText("-");
    m_vmNetworkLabel->setText("-");
    m_vmDisplayLabel->setText("-");
    m_vmUptimeLabel->setText("-");
    m_vmNotesLabel->clear();
    m_vmNotesLabel->setVisible(false);
    m_vncViewerBtn->setEnabled(false);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (QSystemTrayIcon::isSystemTrayAvailable() && m_trayIcon && m_trayIcon->isVisible()) {
        hide();
        m_trayIcon->showMessage("VM Manager", "Still running in the system tray",
                                QSystemTrayIcon::Information, 1500);
        event->ignore();
    } else {
        m_updateTimer->stop();
        m_manager->stopAll();
        event->accept();
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (obj == m_vmList->viewport() && event->type() == QEvent::Drop) {
        auto *drop = static_cast<QDropEvent *>(event);
        const auto *mime = drop->mimeData();
        if (!mime->hasUrls()) return false;

        // Find which item we dropped on
        auto *item = m_vmList->itemAt(drop->position().toPoint());
        if (!item) return false;
        QString id = item->data(0, Qt::UserRole).toString();
        if (id.isEmpty()) return false;

        QSet<QString> disks;
        for (const auto &url : mime->urls()) {
            QString path = url.toLocalFile();
            QString ext = QFileInfo(path).suffix().toLower();
            if (ext == "iso" || ext == "img" || ext == "vdi" || ext == "vmdk")
                disks.insert(path);
        }
        if (disks.isEmpty()) return false;

        // Add dropped disks as extra drives (convert VDI/VMDK if needed)
        auto cfg = m_manager->getConfig(id);
        for (const auto &diskPath : disks) {
            QString finalPath = VMCreatorDialog::convertDiskImage(this, diskPath);
            bool isCDROM = diskPath.endsWith(".iso", Qt::CaseInsensitive);
            DiskDrive dd;
            dd.path = finalPath;
            dd.format = isCDROM ? "raw" : "qcow2";
            dd.controller = isCDROM ? "ide" : "scsi";
            dd.driveType = isCDROM ? 1 : 0;
            bool dup = false;
            if (dd.path == cfg.disk.path) dup = true;
            for (const auto &ed : cfg.extraDrives)
                if (ed.path == dd.path) { dup = true; break; }
            if (!dup) cfg.extraDrives.append(dd);
        }
        m_manager->saveConfig(cfg);
        refreshVMList();
        statusBar()->showMessage(QString("Added %1 disk(s) to %2").arg(disks.size()).arg(cfg.name), 3000);
        drop->accept();
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::setupTrayIcon() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    m_trayMenu = new QMenu(this);

    auto *showAction = m_trayMenu->addAction("Show VM Manager");
    connect(showAction, &QAction::triggered, this, [this]() {
        show();
        raise();
        activateWindow();
    });

    m_trayMenu->addSeparator();

    connect(m_trayMenu, &QMenu::aboutToShow, this, &MainWindow::updateTrayMenu);

    m_trayMenu->addSeparator();

    auto *quitAction = m_trayMenu->addAction("Quit");
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->setToolTip("VM Manager");

    // Create a simple icon programmatically
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(m_darkTheme ? "#7c7cf0" : "#4a4a8a"));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(4, 4, 56, 56, 10, 10);
    p.setPen(QPen(Qt::white, 3));
    p.setFont(QFont("monospace", 22, QFont::Bold));
    p.drawText(QRect(0, 0, 64, 64), Qt::AlignCenter, "VM");
    p.end();
    m_trayIcon->setIcon(QIcon(pm));

    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
        [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick) {
                show();
                raise();
                activateWindow();
            }
        });
}

void MainWindow::updateTrayMenu() {
    if (!m_trayMenu) return;

    // Remove old VM entries
    while (m_trayMenu->actions().size() > 2) {
        auto *a = m_trayMenu->actions().at(1);
        if (a->isSeparator()) break;
        m_trayMenu->removeAction(a);
        delete a;
    }

    // Insert VM entries after the "Show" action
    auto configs = m_manager->allConfigs();
    int insertPos = 1;
    for (const auto &cfg : configs) {
        bool running = m_manager->isVMRunning(cfg.id);
        auto *task = m_manager->taskForVM(cfg.id);
        QString text = cfg.name;
        if (task) text += " [" + task->stateString() + "]";

        auto *vmAction = new QAction(text);
        vmAction->setEnabled(running);
        QString iconFile = cfg.iconName.isEmpty() ? "linux" : cfg.iconName;
        vmAction->setIcon(QIcon(QString(":/icons/%1.png").arg(iconFile)));
        if (running) {
            connect(vmAction, &QAction::triggered, this, [this, id = cfg.id]() {
                selectVMById(id);
            });
        }
        m_trayMenu->insertAction(m_trayMenu->actions().at(insertPos), vmAction);
        insertPos++;
    }
}

void MainWindow::selectVMById(const QString &id) {
    show();
    raise();
    activateWindow();
    for (int i = 0; i < m_vmList->topLevelItemCount(); i++) {
        auto *item = m_vmList->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == id) {
            m_vmList->setCurrentItem(item);
            break;
        }
    }
}
