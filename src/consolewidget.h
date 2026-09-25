#ifndef CONSOLEWIDGET_H
#define CONSOLEWIDGET_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

class ConsoleWidget : public QWidget {
    Q_OBJECT
public:
    explicit ConsoleWidget(QWidget *parent = nullptr);
    void setDarkMode(bool dark);

    void appendOutput(const QString &text);
    void appendSerial(const QString &text);
    void clear();

signals:
    void commandSent(const QString &cmd);

private:
    bool m_dark = true;
    QPlainTextEdit *m_output;
    QLineEdit *m_input;
    QLabel *m_statusLabel;
    QPushButton *m_clearBtn;
};

#endif
