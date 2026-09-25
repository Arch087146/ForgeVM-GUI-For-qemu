#ifndef AICHATWIDGET_H
#define AICHATWIDGET_H

#include <QWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QComboBox>
#include <QLabel>
#include <QProcess>
#include <QTimer>
#include <QPointer>
#include <QSettings>
#include "vmconfig.h"

struct AIMessage {
    QString role; // "user" or "assistant"
    QString content;
};

class AIChatWidget : public QWidget {
    Q_OBJECT
public:
    explicit AIChatWidget(QWidget *parent = nullptr);

    void appendMessage(const QString &role, const QString &content);
    void addSystemMessage(const QString &content);
    void setDarkMode(bool dark);
    void sendRequest(const QString &message);

signals:
    void configGenerated(const VMConfig &config);
    void statusMessage(const QString &msg);
    void requestSent();

private slots:
    void onSend();
    void onReplyFinished(QNetworkReply *reply);
    void onSettingsChanged();
    void onConnectionCheckFinished(QNetworkReply *reply);

private:
    void setupUI();
    void loadSettings();
    void saveSettings();
    QString buildSystemPrompt();
    QJsonArray buildConversation();
    void showError(const QString &error);

    void checkConnection();
    void tryAutoStartServer();
    void onAutoStartFinished(int exitCode, QProcess::ExitStatus status);

    QTextEdit *m_messages;
    QLineEdit *m_input;
    QPushButton *m_sendBtn;
    QPushButton *m_settingsBtn;
    QWidget *m_settingsPanel;
    QLineEdit *m_apiUrlEdit;
    QLineEdit *m_apiKeyEdit;
    QComboBox *m_modelCombo;
    QLabel *m_statusDot;

    QNetworkAccessManager *m_net;
    QList<AIMessage> m_conversation;
    bool m_waiting = false;
    bool m_autoStartAttempted = false;
    QProcess *m_aiProcess = nullptr;
    QString m_apiUrl = "";
    QString m_apiKey = "";
    QString m_model = "";
    bool m_dark = true;
};

#endif
