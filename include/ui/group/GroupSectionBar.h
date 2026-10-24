#pragma once

#include <QWidget>

class QLabel;
class QTabBar;
class QTabWidget;
class QToolButton;

// F-Dpi: the "Genel" / "Özel (Ağ Bazlı)" tabs above the group tabs. In the Özel section the group tabs' left corner also shows
// the network auto-switch toggle and the current network.
class GroupSectionBar : public QWidget {
    Q_OBJECT

public:
    // Goes above groupTabs (which it moves into a column with itself, in groupTabs' place) and owns its corner tools.
    explicit GroupSectionBar(QTabWidget *groupTabs);

    // Shows which section's tabs are listed; emits nothing.
    void setNetworkSection(bool network);

signals:
    void sectionRequested(bool network);

private:
    void updateNetwork();

    QTabWidget *m_groupTabs = nullptr;
    QTabBar *m_sections = nullptr;
    QWidget *m_networkTools = nullptr;
    QToolButton *m_auto = nullptr;
    QLabel *m_current = nullptr;
};
