#pragma once

#include <QWidget>

class QLabel;
class QTabBar;
class QTabWidget;

// F-Dpi: the "Genel" / "Özel (Ağ Bazlı)" buttons above the group tabs.
class GroupSectionBar : public QWidget {
    Q_OBJECT

public:
    // Goes above groupTabs, which it moves into a column with itself, in groupTabs' place.
    explicit GroupSectionBar(QTabWidget *groupTabs);

    // The current network ("Wi-Fi: <name>"), kept up to date; for the status bar.
    static QLabel *NewNetworkLabel(QWidget *parent);

    // Shows which section's tabs are listed; emits nothing.
    void setNetworkSection(bool network);

signals:
    void sectionRequested(bool network);

private:
    void updateNetwork();

    QTabBar *m_sections = nullptr;
};
