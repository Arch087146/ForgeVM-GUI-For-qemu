#include "appsettingsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QTabWidget>
#include <QMessageBox>

AppSettingsDialog::AppSettingsDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Preferences");
    setMinimumSize(480, 360);
    resize(520, 400);
    setupUI();
    loadSettings();
}

void AppSettingsDialog::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);

    // General tab
    auto *genTab = new QWidget;
    auto *genLayout = new QVBoxLayout(genTab);
    genLayout->setContentsMargins(16, 12, 16, 12);
    genLayout->setSpacing(12);

    auto *aiGroup = new QGroupBox("AI Assistant");
    auto *aiLayout = new QVBoxLayout(aiGroup);
    aiLayout->setContentsMargins(12, 16, 12, 12);
    aiLayout->setSpacing(8);

    m_aiEnabledCheck = new QCheckBox("Enable AI Assistant");
    m_aiEnabledCheck->setToolTip("Show or hide the AI Assistant panel entirely.");
    aiLayout->addWidget(m_aiEnabledCheck);

    m_autoStartAICheck = new QCheckBox("Auto-start AI server when unreachable");
    m_autoStartAICheck->setToolTip("When the AI panel opens and the server is not running,\n"
                                   "automatically try to start LM Studio / Ollama.");
    aiLayout->addWidget(m_autoStartAICheck);

    auto *note = new QLabel("Requires LM Studio (lms) or Ollama to be installed.");
    note->setWordWrap(true);
    note->setStyleSheet("font-size: 11px; color: gray;");
    aiLayout->addWidget(note);

    genLayout->addWidget(aiGroup);
    genLayout->addStretch();

    tabs->addTab(genTab, "General");

    mainLayout->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &AppSettingsDialog::onSave);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void AppSettingsDialog::loadSettings() {
    m_aiEnabledCheck->setChecked(loadAIEnabled());
    m_autoStartAICheck->setChecked(loadAutoStartAI());
}

void AppSettingsDialog::onSave() {
    saveAIEnabled(m_aiEnabledCheck->isChecked());
    saveAutoStartAI(m_autoStartAICheck->isChecked());
    accept();
}

bool AppSettingsDialog::loadAutoStartAI() {
    QSettings s("forgevm", "forgevm");
    return s.value("general/autoStartAI", true).toBool();
}

void AppSettingsDialog::saveAutoStartAI(bool on) {
    QSettings s("forgevm", "forgevm");
    s.setValue("general/autoStartAI", on);
}

bool AppSettingsDialog::loadAIEnabled() {
    QSettings s("forgevm", "forgevm");
    return s.value("general/aiEnabled", false).toBool();
}

void AppSettingsDialog::saveAIEnabled(bool on) {
    QSettings s("forgevm", "forgevm");
    s.setValue("general/aiEnabled", on);
}
