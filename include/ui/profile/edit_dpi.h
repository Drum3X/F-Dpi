#pragma once

#include <QWidget>
#include "profile_editor.h"

class QComboBox;
class QLineEdit;
class QPlainTextEdit;

// F-Dpi: the DPI Bypass section of the profile dialog. F2 scope: preset, capture ports and the winws strategy; the split-bypass
// list editors come in F3.
class EditDpi : public QWidget, public ProfileEditor {
    Q_OBJECT

public:
    explicit EditDpi(QWidget *parent = nullptr);

    void onStart(std::shared_ptr<Configs::Profile> _ent) override;

    bool onEnd() override;

    void bindValidation(Validation::Form &form) override;

private:
    void applyPreset(int index);
    // Any hand edit turns the preset choice into "Custom".
    void markCustom();

    std::shared_ptr<Configs::Profile> ent;
    QComboBox *m_preset = nullptr;
    QLineEdit *m_wfTcp = nullptr;
    QLineEdit *m_wfUdp = nullptr;
    QPlainTextEdit *m_strategy = nullptr;
    bool m_loading = false;
};
