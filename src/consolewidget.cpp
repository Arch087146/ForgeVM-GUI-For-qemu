#include "consolewidget.h"
#include <QScrollBar>
#include <QTextCursor>
#include <QTextBlock>
#include <QFont>
#include <QKeyEvent>

ConsoleWidget::ConsoleWidget(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    m_statusLabel = new QLabel("Console");
    headerRow->addWidget(m_statusLabel);
    headerRow->addStretch();
    m_clearBtn = new QPushButton("Clear");
    m_clearBtn->setFixedSize(50, 20);
    m_clearBtn->setStyleSheet("font-size: 10px; padding: 0;");
    connect(m_clearBtn, &QPushButton::clicked, this, &ConsoleWidget::clear);
    headerRow->addWidget(m_clearBtn);

    m_output = new QPlainTextEdit;
    m_output->setReadOnly(true);
    m_output->setFont(QFont("JetBrains Mono", 10));
    m_output->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_output->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setMaximumBlockCount(10000);

    m_input = new QLineEdit;
    m_input->setPlaceholderText("Send command to serial console...");
    m_input->setFont(QFont("JetBrains Mono", 10));

    connect(m_input, &QLineEdit::returnPressed, this, [this]() {
        if (!m_input->text().isEmpty()) {
            emit commandSent(m_input->text() + "\n");
            m_output->appendPlainText(">>> " + m_input->text());
            m_input->clear();
        }
    });

    layout->addLayout(headerRow);
    layout->addWidget(m_output, 1);
    layout->addWidget(m_input);

    setDarkMode(true);
}

void ConsoleWidget::setDarkMode(bool dark) {
    m_dark = dark;
    QString bg, fg, border, sel;
    if (dark) {
        bg = "#1e1e2e"; fg = "#cdd6f4";
        border = "#313244"; sel = "#45475a";
    } else {
        bg = "#ffffff"; fg = "#1e1e2e";
        border = "#dce0e8"; sel = "#ccd0da";
    }

    m_statusLabel->setStyleSheet(
        QString("QLabel { background: %1; color: %2; padding: 4px 12px; "
                "font-size: 11px; border-bottom: 1px solid %3; }")
            .arg(dark ? "#181825" : "#f2f3f5", dark ? "#6c7086" : "#6c7086", border));

    m_output->setStyleSheet(
        QString("QPlainTextEdit { background: %1; color: %2; "
                "border: none; padding: 8px; }"
                "QPlainTextEdit::selection { background: %3; }")
            .arg(bg, fg, sel));

    m_input->setStyleSheet(
        QString("QLineEdit { background: %1; color: %2; border: none; "
                "border-top: 1px solid %3; padding: 8px 12px; }")
            .arg(dark ? "#181825" : "#f2f3f5", fg, border));
}

void ConsoleWidget::appendOutput(const QString &text) {
    m_output->moveCursor(QTextCursor::End);
    m_output->insertPlainText(text);
    if (m_output->document()->blockCount() > 10000) {
        QTextCursor c(m_output->document()->begin());
        c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        c.removeSelectedText();
    }
    auto *sb = m_output->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void ConsoleWidget::appendSerial(const QString &text) {
    appendOutput(text);
}

void ConsoleWidget::clear() {
    m_output->clear();
}
