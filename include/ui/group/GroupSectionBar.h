#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

// F-Dpi: the switch left of the group tabs that splits them into "Genel" and "Özel (Ağ Bazlı)", plus the network auto-switch
// and connect-on-launch toggles and the current network.
class GroupSectionBar : public QWidget {
    Q_OBJECT

public:
    explicit GroupSectionBar(QWidget *parent = nullptr);

    // Shows which section's tabs are listed; emits nothing.
    void setNetworkSection(bool network);

signals:
    void sectionRequested(bool network);

private:
    void updateNetwork();

    QToolButton *m_general = nullptr;
    QToolButton *m_network = nullptr;
    QToolButton *m_auto = nullptr;
    QToolButton *m_launch = nullptr;
    QLabel *m_current = nullptr;
};
