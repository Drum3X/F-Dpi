#pragma once

#include <QDialog>

#include "include/engine/dns/DnsBenchmark.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;

// F-Dpi: Otomatik DNS settings, plus a benchmark of the encrypted DNS servers and a DNS poisoning test for this network.
class DialogAutoDns : public QDialog {
    Q_OBJECT

public:
    explicit DialogAutoDns(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;
    void accept() override;

private:
    void startTest();
    void onFinished(const Engine::DnsBenchmarkResult &result);
    void refreshStatus();
    void setRunning(bool running);

    Engine::DnsBenchmark *m_benchmark = nullptr;
    bool m_closeWhenDone = false;

    QComboBox *m_mode = nullptr;
    QCheckBox *m_proxy = nullptr;
    QCheckBox *m_system = nullptr;
    QLabel *m_status = nullptr;
    QLineEdit *m_domains = nullptr;
    QPushButton *m_test = nullptr;
    QProgressBar *m_progress = nullptr;
    QTableWidget *m_servers = nullptr;
    QTableWidget *m_poison = nullptr;
    QLabel *m_summary = nullptr;
};
