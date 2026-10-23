#pragma once

#include <QWidget>
#include "profile_editor.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

// F-Dpi: the DPI Bypass section of the profile dialog. Strategy tab: preset, capture ports and winws options. Site selection
// tab (Split Bypass): all sites, listed sites only or a list winws learns, plus sites that are never touched.
class EditDpi : public QWidget, public ProfileEditor {
    Q_OBJECT

public:
    explicit EditDpi(QWidget *parent = nullptr);

    void onStart(std::shared_ptr<Configs::Profile> _ent) override;

    bool onEnd() override;

    void bindValidation(Validation::Form &form) override;

private:
    QWidget *buildStrategyTab();
    QWidget *buildSitesTab();
    // A "Add list" menu button and an "Import" button that append to edit.
    QWidget *buildListButtons(QPlainTextEdit *edit, bool blockedLists);

    void applyPreset(int index);
    // Any hand edit turns the preset choice into "Custom".
    void markCustom();
    void updateModeRows();
    void updateLearned();
    void showCommandLine();
    void runBlockcheck();

    // Saves the form into config (also used by the preview).
    void storeInto(Configs::dpi *config) const;

    static void appendHosts(QPlainTextEdit *edit, const QStringList &hosts);
    static QStringList lines(const QPlainTextEdit *edit);

    std::shared_ptr<Configs::Profile> ent;
    Validation::Form *m_form = nullptr;
    bool m_loading = false;

    QComboBox *m_preset = nullptr;
    QLineEdit *m_wfTcp = nullptr;
    QLineEdit *m_wfUdp = nullptr;
    QPlainTextEdit *m_strategy = nullptr;

    QComboBox *m_mode = nullptr;
    QLabel *m_modeHint = nullptr;
    QLabel *m_hostlistLabel = nullptr;
    QPlainTextEdit *m_hostlist = nullptr;
    QWidget *m_hostlistButtons = nullptr;
    QWidget *m_learnedRow = nullptr;
    QLabel *m_learned = nullptr;
    QPushButton *m_learnedShow = nullptr;
    QPushButton *m_learnedClear = nullptr;
    QPlainTextEdit *m_exclude = nullptr;
};
