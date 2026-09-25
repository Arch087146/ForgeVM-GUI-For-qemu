#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTreeWidget>
#include <QStackedWidget>
#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QDockWidget>
#include <QLineEdit>
#include <QTabWidget>
#include <QComboBox>
#include <QSystemTrayIcon>
#include <QCloseEvent>
#include <QProgressBar>
#include <QFrame>
#include "vmconfig.h"
#include "vmmanager.h"
#include "vmtask.h"
#include "consolewidget.h"
#include "aichatwidget.h"
#include "vncviewer.h"
#include "vncdisplaywindow.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onNewVM();
    void onNewVMFromConfig(const VMConfig &cfg);
void onEditVM();
    void onDeleteVM();
    void onPreferences();
    void onStartVM();
    void onStopVM();
    void onPauseResumeVM();
    void onVMSelected();
    void onVMContextMenu(const QPoint &pos);
    void refreshVMList();
    void onVMStateChanged(const QString &id, VMTask::State state);
    void updateDetailsPanel();
    void openVNCViewer();
    void openGTKDisplay();
    void connectVNCViewer();
    void onPresetDownload();
    void onPrebuiltVM();
    void onUserGuide();
    void onShowAI();
    void onThemeChanged();
    void onTools();
void onAbout();
    void onExportVM();
    void onImportVM();
    void onImportUTM();
    void onConvertDisk();
    void onCloneVM();
    void onRenameVM();
    void onForceStopVM();
    void onSnapshots();
    void toggleSidebar();
    void toggleToolbar();
    void toggleAI();
    void filterVMList(const QString &text);

private:
    void setupUI();
    void setupToolbar();
    void setupSidebar();
    void setupDetailsPanel();
    void setupEmptyState();
    void setupAI();
    void applyStyles();
    void applyDarkStyles();
    void applyLightStyles();
    void updateActionStates();
    QString selectedVMId() const;
    void clearDetails();
    void detectSystemTheme();
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void setupTrayIcon();
    void updateTrayMenu();
    void updateVMCount();
    void selectVMById(const QString &id);

    VMManager *m_manager;

    // Sidebar
    QLineEdit *m_filterEdit;
    QTreeWidget *m_vmList;
    QWidget *m_sidebarWidget;

    // Content
    QStackedWidget *m_contentStack;

    // Details panel (page 0)
    QWidget *m_detailsPage;
    QLabel *m_vmNameLabel;
    QPushButton *m_vmStartBtn;
    QPushButton *m_vmStopBtn;
    QPushButton *m_vmPauseBtn;
    QPushButton *m_vmForceStopBtn;
    QPushButton *m_openDisplayBtn;
    QLabel *m_vmIconLabel;
    QLabel *m_vmStatusLabel;
    QLabel *m_vmCpuLabel;
    QLabel *m_vmMemoryLabel;
    QLabel *m_vmDiskLabel;
    QLabel *m_vmNetworkLabel;
    QLabel *m_vmDisplayLabel;
    QLabel *m_vmUptimeLabel;
    QProgressBar *m_vmCpuBar = nullptr;
    QProgressBar *m_vmMemoryBar = nullptr;
    QProgressBar *m_vmDiskBar = nullptr;
    QProgressBar *m_vmNetworkBar = nullptr;
    QProgressBar *m_vmDisplayBar = nullptr;
    QProgressBar *m_vmUptimeBar = nullptr;
    QLabel *m_vmNotesLabel;
    QWidget *m_detailsInfoPanel;
    QTabWidget *m_rightTabs;
    ConsoleWidget *m_consoleWidget;
    VncViewer *m_vncViewer;
    VncDisplayWindow *m_vncDisplayWindow;
    QPushButton *m_vncViewerBtn;
    QComboBox *m_displayModeCombo;
    QComboBox *m_displayModeToggle;
    QWidget *m_detailsButtons;

    // Empty state (page 1)
    QWidget *m_emptyPage;

    // AI
    QDockWidget *m_aiDock;
    AIChatWidget *m_aiWidget;
    QAction *m_aiAction;

    // Toolbar actions
    QAction *m_newAction;
    QAction *m_startAction;
    QAction *m_stopAction;
    QAction *m_pauseResumeAction;
    QAction *m_settingsAction;
    QAction *m_deleteAction;
    QAction *m_exportAction;
    QAction *m_importAction;
    QAction *m_toggleSidebarAction;
    QAction *m_toggleToolbarAction;

    QToolBar *m_toolbar;

    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QLabel *m_statusVMsLabel = nullptr;
    QProgressBar *m_vmProgressBar = nullptr;
    QFrame *m_vmControlBar = nullptr;

    QTimer *m_updateTimer;
    bool m_darkTheme = true;
};

#endif
