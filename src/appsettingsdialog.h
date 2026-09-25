#ifndef APPSETTINGSDIALOG_H
#define APPSETTINGSDIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QSettings>

class AppSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit AppSettingsDialog(QWidget *parent = nullptr);
    bool autoStartAI() const { return m_autoStartAICheck->isChecked(); }
    bool aiEnabled() const { return m_aiEnabledCheck->isChecked(); }

    static bool loadAutoStartAI();
    static void saveAutoStartAI(bool on);
    static bool loadAIEnabled();
    static void saveAIEnabled(bool on);

private slots:
    void onSave();

private:
    void setupUI();
    void loadSettings();

    QCheckBox *m_autoStartAICheck;
    QCheckBox *m_aiEnabledCheck;
};

#endif
