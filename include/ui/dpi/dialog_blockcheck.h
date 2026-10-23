#pragma once

#include <QDialog>

#include "include/engine/dpi/Blockcheck.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;

// F-Dpi: runs Blockcheck and shows what it finds. applyToProfile: opened from a DPI profile's editor, so the result is
// handed back (accept + result()); otherwise the result is saved as a new DPI profile.
class DialogBlockcheck : public QDialog {
    Q_OBJECT

public:
    DialogBlockcheck(const QStringList &domains, bool applyToProfile, QWidget *parent = nullptr);

    [[nodiscard]] const Engine::BlockcheckResult &result() const { return m_result; }

protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

private:
    void startScan();
    void onFinished(const Engine::BlockcheckResult &result);
    void saveAsProfile();
    void setRunning(bool running);

    Engine::Blockcheck *m_blockcheck = nullptr;
    Engine::BlockcheckResult m_result;
    bool m_applyToProfile = false;
    bool m_closeWhenDone = false;

    QPlainTextEdit *m_domains = nullptr;
    QComboBox *m_mode = nullptr;
    QCheckBox *m_http = nullptr;
    QCheckBox *m_tls12 = nullptr;
    QCheckBox *m_tls13 = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_stop = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_current = nullptr;
    QTableWidget *m_table = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_apply = nullptr;
};
