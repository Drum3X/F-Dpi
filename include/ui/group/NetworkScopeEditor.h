#pragma once

#include <memory>

#include <QGroupBox>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace Configs {
    class Group;
}

// F-Dpi: the scope section of the group dialog: general, or network-specific with the networks it is bound to and the profile
// that runs on them.
class NetworkScopeEditor : public QGroupBox {
    Q_OBJECT

public:
    explicit NetworkScopeEditor(const std::shared_ptr<Configs::Group> &group, QWidget *parent = nullptr);

    // Saves the scope; for a group not added yet, as soon as it gets its id.
    void store(const std::shared_ptr<Configs::Group> &group) const;

    // The scope a new group starts with: the section whose tabs are shown.
    static void SetDefaultForNew(bool network);

signals:
    void layoutChanged();

private:
    void updateRows();
    void addNetwork(const QStringList &keys, const QString &name);
    bool listed(const QStringList &keys) const;

    QComboBox *m_scope = nullptr;
    QWidget *m_networkBox = nullptr;
    QListWidget *m_networks = nullptr;
    QPushButton *m_addCurrent = nullptr;
    QPushButton *m_remove = nullptr;
    QComboBox *m_profile = nullptr;
    QLabel *m_hint = nullptr;
};
