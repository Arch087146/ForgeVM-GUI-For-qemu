#include "aichatwidget.h"
#include "appsettingsdialog.h"
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSplitter>
#include <QScrollBar>
#include <QLabel>
#include <QFont>
#include <QDateTime>
#include <QMessageBox>
#include <QFrame>
#include <QStandardPaths>
#include <QFileInfo>

static const char *SYSTEM_PROMPT = R"(You are a VM configuration assistant integrated into a QEMU ForgeVM desktop application.

You can help users:
1. Generate VM configurations based on natural language descriptions
2. Troubleshoot QEMU errors
3. Recommend settings for different operating systems
4. Explain virtualization concepts

When the user wants to create a VM, respond with a VM config in this exact JSON format inside a ```json code block:
{
  "name": "vm-name",
  "osType": "linux|windows|freebsd|openbsd|other",
  "machine": "q35|pc",
  "cpuModel": "host|qemu64|qemu32|kvm64",
  "cpuCores": 2,
  "memoryMB": 2048,
  "efiBoot": true,
  "kvmEnabled": true,
  "audioEnabled": false,
  "diskSizeMB": 20480,
  "diskFormat": "qcow2",
  "networkMode": "user|bridge",
  "networkModel": "virtio-net|e1000",
  "displayMode": "vnc|gtk|sdl|none"
}

When helping with errors, analyze the QEMU error message and suggest specific fixes.
Keep responses concise and practical. The user is comfortable with Linux/QEMU terminology.)";

AIChatWidget::AIChatWidget(QWidget *parent) : QWidget(parent) {
    m_net = new QNetworkAccessManager(this);
    setupUI();
    loadSettings();
    addSystemMessage(
        "AI Assistant ready. I can help you create VMs, troubleshoot QEMU errors, "
        "and recommend configurations. Just describe what you need.");
    QTimer::singleShot(500, this, &AIChatWidget::checkConnection);
}

void AIChatWidget::setupUI() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header
    auto *header = new QWidget;
    header->setObjectName("aiHeader");
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(12, 8, 12, 8);
    auto *titleLabel = new QLabel("AI Assistant");
    titleLabel->setStyleSheet("font-weight: bold; font-size: 13px;");
    hl->addWidget(titleLabel);
    m_statusDot = new QLabel;
    m_statusDot->setFixedSize(8, 8);
    m_statusDot->setToolTip("Checking connection...");
    m_statusDot->setStyleSheet("background: #f0ad4e; border-radius: 4px;");
    hl->addWidget(m_statusDot);
    hl->addStretch();
    m_settingsBtn = new QPushButton("Settings");
    m_settingsBtn->setFixedHeight(24);
    m_settingsBtn->setStyleSheet("font-size: 11px; padding: 2px 10px;");
    connect(m_settingsBtn, &QPushButton::clicked, this, [this]() {
        m_settingsPanel->setVisible(!m_settingsPanel->isVisible());
    });
    hl->addWidget(m_settingsBtn);
    layout->addWidget(header);

    // Settings panel
    m_settingsPanel = new QWidget;
    m_settingsPanel->setObjectName("aiSettings");
    auto *sl = new QVBoxLayout(m_settingsPanel);
    sl->setContentsMargins(12, 8, 12, 8);
    sl->setSpacing(4);

    auto *urlRow = new QHBoxLayout;
    urlRow->addWidget(new QLabel("API URL:"));
    m_apiUrlEdit = new QLineEdit(m_apiUrl);
    m_apiUrlEdit->setPlaceholderText("https://api.example.com/v1/chat/completions");
    urlRow->addWidget(m_apiUrlEdit, 1);
    sl->addLayout(urlRow);

    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel("API Key:"));
    m_apiKeyEdit = new QLineEdit(m_apiKey);
    m_apiKeyEdit->setPlaceholderText("Optional");
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    keyRow->addWidget(m_apiKeyEdit, 1);
    sl->addLayout(keyRow);

    auto *modelRow = new QHBoxLayout;
    modelRow->addWidget(new QLabel("Model:"));
    m_modelCombo = new QComboBox;
    m_modelCombo->setEditable(true);
    m_modelCombo->addItems({"", "llama3.2", "llama3.1", "mistral", "qwen2.5", "codellama", "gemma2", "phi3", "custom"});
    m_modelCombo->setCurrentText(m_model);
    modelRow->addWidget(m_modelCombo, 1);
    sl->addLayout(modelRow);

    auto *saveBtn = new QPushButton("Save Settings");
    connect(saveBtn, &QPushButton::clicked, this, &AIChatWidget::onSettingsChanged);
    sl->addWidget(saveBtn);

    m_settingsPanel->setVisible(false);
    layout->addWidget(m_settingsPanel);

    // Messages area
    m_messages = new QTextEdit;
    m_messages->setReadOnly(true);
    m_messages->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_messages->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_messages->document()->setMaximumBlockCount(500);
    layout->addWidget(m_messages, 1);

    // Input area
    auto *inputRow = new QWidget;
    inputRow->setObjectName("aiInput");
    auto *il = new QHBoxLayout(inputRow);
    il->setContentsMargins(8, 8, 8, 8);

    m_input = new QLineEdit;
    m_input->setPlaceholderText("Describe the VM you want, or ask for help...");
    connect(m_input, &QLineEdit::returnPressed, this, &AIChatWidget::onSend);
    il->addWidget(m_input, 1);

    m_sendBtn = new QPushButton("Send");
    m_sendBtn->setFixedWidth(70);
    connect(m_sendBtn, &QPushButton::clicked, this, &AIChatWidget::onSend);
    il->addWidget(m_sendBtn);

    layout->addWidget(inputRow);
}

void AIChatWidget::loadSettings() {
    QSettings s("forgevm", "forgevm");
    s.beginGroup("ai");
    m_apiUrl = s.value("apiUrl", "").toString();
    m_apiKey = s.value("apiKey", "").toString();
    m_model = s.value("model", "").toString();
    s.endGroup();
}

void AIChatWidget::saveSettings() {
    QSettings s("forgevm", "forgevm");
    s.beginGroup("ai");
    s.setValue("apiUrl", m_apiUrlEdit->text().trimmed());
    s.setValue("apiKey", m_apiKeyEdit->text().trimmed());
    s.setValue("model", m_modelCombo->currentText().trimmed());
    s.endGroup();
}

void AIChatWidget::onSettingsChanged() {
    saveSettings();
    m_apiUrl = m_apiUrlEdit->text().trimmed();
    m_apiKey = m_apiKeyEdit->text().trimmed();
    m_model = m_modelCombo->currentText().trimmed();
    m_settingsPanel->setVisible(false);
    emit statusMessage("AI settings saved");
    checkConnection();
}

void AIChatWidget::addSystemMessage(const QString &content) {
    appendMessage("system", content);
}

void AIChatWidget::appendMessage(const QString &role, const QString &content) {
    if (role == "user") {
        m_conversation.append({"user", content});
    } else if (role == "assistant") {
        m_conversation.append({"assistant", content});
    } else if (role == "system") {
        m_conversation.append({"system", content});
    }

    QString prefix = (role == "user") ? "You" : "AI";
    QString color = m_dark ? "#6fcf97" : "#27ae60";
    if (role == "user") color = m_dark ? "#5bc0de" : "#2980b9";

    m_messages->append(
        QString("<div style='margin: 8px 0;'>"
                "<span style='color: %1; font-weight: bold;'>%2:</span>"
                "</div>").arg(color, prefix));
    QString displayContent = content;
    m_messages->append(
        QString("<div style='margin: 0 0 12px 0; line-height: 1.5;'>%1</div>")
            .arg(displayContent.replace('\n', "<br>")));

    auto *sb = m_messages->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void AIChatWidget::onSend() {
    if (!AppSettingsDialog::loadAIEnabled()) {
        appendMessage("system", "AI Assistant is disabled. Enable it in Settings (Ctrl+,).");
        return;
    }
    QString text = m_input->text().trimmed();
    if (text.isEmpty() || m_waiting) return;

    emit requestSent();
    m_input->clear();
    appendMessage("user", text);

    m_waiting = true;
    m_sendBtn->setEnabled(false);
    m_sendBtn->setText("WAIT");

    emit statusMessage("AI is thinking...");

    QJsonObject body;
    if (!m_model.isEmpty())
        body["model"] = m_model;
    else
        body["model"] = "local-model";
    body["messages"] = buildConversation();
    body["stream"] = false;

    if (!m_apiKey.isEmpty()) {
        body["api_key"] = m_apiKey;
    }

    QString url = m_apiUrl;
    // If the URL doesn't already point to an API path, assume OpenAI-compatible (LM Studio)
    if (!url.contains("/chat/completions") && !url.contains("/api/")) {
        if (!url.endsWith("/")) url += "/";
        url += "v1/chat/completions";
    }

    QUrl qurl(url);
    QNetworkRequest req(qurl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_apiKey.isEmpty()) {
        req.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    }

    auto *reply = m_net->post(req, QJsonDocument(body).toJson());
    QPointer<QNetworkReply> guard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, guard]() {
        if (guard) onReplyFinished(guard);
    });
}

QString AIChatWidget::buildSystemPrompt() {
    return SYSTEM_PROMPT;
}

QJsonArray AIChatWidget::buildConversation() {
    QJsonArray arr;
    for (const auto &msg : m_conversation) {
        QJsonObject obj;
        obj["role"] = msg.role;
        obj["content"] = msg.content;
        arr.append(obj);
    }
    // Add last user message (already in m_conversation from appendMessage)
    return arr;
}

void AIChatWidget::onReplyFinished(QNetworkReply *reply) {
    m_waiting = false;
    m_sendBtn->setEnabled(true);
    m_sendBtn->setText("Send");

    if (reply->error() != QNetworkReply::NoError) {
        showError("Connection failed: " + reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        showError("Failed to parse AI response");
        return;
    }

    QString content;
    QJsonObject obj = doc.object();

    // Try OpenAI format
    if (obj.contains("choices")) {
        auto choices = obj["choices"].toArray();
        if (!choices.isEmpty()) {
            auto msg = choices[0].toObject()["message"].toObject();
            content = msg["content"].toString();
        }
    }
    // Try Ollama format
    else if (obj.contains("message")) {
        content = obj["message"].toObject()["content"].toString();
    } else if (obj.contains("response")) {
        content = obj["response"].toString();
    }

    if (content.isEmpty()) {
        showError("Empty response from AI. Check your model and API settings.");
        return;
    }

    appendMessage("assistant", content);

    // Check for embedded JSON config
    int jsonStart = content.indexOf("```json");
    if (jsonStart >= 0) {
        jsonStart += 7; // skip ```json
        int jsonEnd = content.indexOf("```", jsonStart);
        if (jsonEnd > jsonStart) {
            QString jsonStr = content.mid(jsonStart, jsonEnd - jsonStart).trimmed();
            QJsonDocument cfgDoc = QJsonDocument::fromJson(jsonStr.toUtf8());
            if (cfgDoc.isObject()) {
                VMConfig cfg = VMConfig::fromJson(cfgDoc.object());
                if (!cfg.name.isEmpty()) {
                    cfg.id = VMConfig::generateId();
                    if (cfgDoc.object().contains("diskSizeMB")) {
                        cfg.disk.sizeMB = static_cast<qint64>(
                            cfgDoc.object()["diskSizeMB"].toDouble());
                    }
                    if (cfgDoc.object().contains("diskFormat")) {
                        cfg.disk.format = cfgDoc.object()["diskFormat"].toString();
                    }
                    emit configGenerated(cfg);
                    appendMessage("system",
                        "VM configuration detected! You can now use it to create the VM.");
                }
            }
        }
    }

    emit statusMessage("Ready");
}

void AIChatWidget::checkConnection() {
    m_statusDot->setStyleSheet("background: #f0ad4e; border-radius: 4px;");
    m_statusDot->setToolTip("Checking connection...");

    QString url = m_apiUrl;
    if (url.contains("/chat/completions")) {
        url = url.left(url.indexOf("/chat/completions"));
    }
    if (url.contains("/api/")) {
        url = url.left(url.indexOf("/api/"));
    }
    QUrl qurl(url);
    QNetworkRequest req(qurl);
    auto *reply = m_net->get(req);
    QPointer<QNetworkReply> guard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, guard]() {
        if (guard) onConnectionCheckFinished(guard);
    });
}

void AIChatWidget::onConnectionCheckFinished(QNetworkReply *reply) {
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();

    if (ok) {
        m_statusDot->setStyleSheet("background: #2ecc71; border-radius: 4px;");
        m_statusDot->setToolTip("Connected to " + m_apiUrl);
        m_autoStartAttempted = false;
    } else {
        m_statusDot->setStyleSheet("background: #e74c3c; border-radius: 4px;");
        m_statusDot->setToolTip("Disconnected");
        if (!m_autoStartAttempted && AppSettingsDialog::loadAutoStartAI()) {
            m_autoStartAttempted = true;
            tryAutoStartServer();
        } else if (m_autoStartAttempted) {
            appendMessage("system",
                "AI server not reachable at <b>" + m_apiUrl.toHtmlEscaped() + "</b>.<br>"
                "Check your internet connection and API settings, or open Settings to change the API URL.");
        }
    }
}

void AIChatWidget::tryAutoStartServer() {
    // Try known AI server binaries
    QStringList candidates;
    QString url = m_apiUrl;
    if (url.contains("1234") || url.contains("localhost")) {
        candidates << "lms" << "ollama" << "lmstudio" << "llama-server";
    } else {
        candidates << "ollama";
    }

    QString serverCmd;
    QStringList args;
    for (const auto &c : candidates) {
        QString path = QStandardPaths::findExecutable(c);
        if (!path.isEmpty()) {
            serverCmd = path;
            if (c == "lms") args << "server" << "start";
            else if (c == "ollama") args << "serve";
            break;
        }
    }

    if (serverCmd.isEmpty()) {
        m_autoStartAttempted = false;
        appendMessage("system",
            "AI server not reachable at <b>" + m_apiUrl.toHtmlEscaped() + "</b>.<br>"
            "Install <b>LM Studio</b> or <b>Ollama</b>, then open this panel and try again.");
        return;
    }

    auto name = QFileInfo(serverCmd).fileName();
    appendMessage("system", "Starting <b>" + name + "</b> server...");

    m_statusDot->setStyleSheet("background: #f0ad4e; border-radius: 4px;");
    m_statusDot->setToolTip("Starting " + name + "...");

    if (m_aiProcess) { m_aiProcess->kill(); m_aiProcess->deleteLater(); }
    m_aiProcess = new QProcess(this);
    m_aiProcess->setProcessChannelMode(QProcess::ForwardedChannels);
    connect(m_aiProcess, &QProcess::finished, this, &AIChatWidget::onAutoStartFinished);
    m_aiProcess->start(serverCmd, args);
}

void AIChatWidget::onAutoStartFinished(int exitCode, QProcess::ExitStatus status) {
    Q_UNUSED(exitCode)
    Q_UNUSED(status)
    // Wait a moment then re-check connection
    QTimer::singleShot(3000, this, &AIChatWidget::checkConnection);
}

void AIChatWidget::showError(const QString &error) {
    m_messages->append(
        QString("<div style='color: #e74c3c; margin: 8px 0;'>"
                "<b>Error:</b> %1</div>").arg(error));
    emit statusMessage("Error");
}

void AIChatWidget::sendRequest(const QString &message) {
    m_input->setText(message);
    emit requestSent();
    onSend();
}

void AIChatWidget::setDarkMode(bool dark) {
    m_dark = dark;
}
