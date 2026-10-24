#include "include/ui/group/GroupSectionBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "include/global/Configs.hpp"
#include "include/sys/NetworkMonitor.hpp"

GroupSectionBar::GroupSectionBar(QTabWidget *groupTabs) : QWidget(nullptr), m_groupTabs(groupTabs) {
    // A column in the group tabs' place: these section tabs, then the group tabs. Both start without a parent: a new child
    // of a QSplitter joins it as its last pane, and replaceWidget() would then move it there instead of into the tabs' place.
    auto *column = new QWidget();
    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(4);
    if (auto *splitter = qobject_cast<QSplitter *>(groupTabs->parentWidget())) {
        splitter->replaceWidget(splitter->indexOf(groupTabs), column);
    } else if (auto *layout = groupTabs->parentWidget() != nullptr ? groupTabs->parentWidget()->layout() : nullptr) {
        delete layout->replaceWidget(groupTabs, column);
    }
    columnLayout->addWidget(this);
    columnLayout->addWidget(groupTabs, 1);
    groupTabs->show();

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_sections = new QTabBar(this);
    m_sections->setObjectName(QStringLiteral("groupSections")); // rounded in ThemeManager's tab sheet
    m_sections->setDrawBase(false);
    m_sections->setExpanding(false);
    m_sections->addTab(tr("General"));
    m_sections->setTabToolTip(0, tr("Configs used on any network without a config of its own."));
    m_sections->addTab(tr("Custom (per network)"));
    connect(m_sections, &QTabBar::currentChanged, this, [this](int index) { emit sectionRequested(index == 1); });
    layout->addWidget(m_sections);
    layout->addStretch(1);

    // Left of the group tabs while the Özel section is shown.
    m_networkTools = new QWidget(groupTabs);
    auto *tools = new QHBoxLayout(m_networkTools);
    tools->setContentsMargins(0, 0, 6, 0);
    tools->setSpacing(6);
    m_auto = new QToolButton(m_networkTools);
    m_auto->setText(tr("Auto switch"));
    m_auto->setCheckable(true);
    m_auto->setAutoRaise(true);
    m_auto->setToolTip(tr("When the network changes, run its own config (Custom groups bound to it), else the last general config."));
    m_auto->setChecked(Configs::dataManager->settingsRepo->network_auto_switch);
    connect(m_auto, &QToolButton::toggled, this, [](bool on) {
        Configs::dataManager->settingsRepo->network_auto_switch = on;
        Configs::dataManager->settingsRepo->Save();
    });
    m_current = new QLabel(m_networkTools);
    m_current->setEnabled(false);
    tools->addWidget(m_auto);
    tools->addWidget(m_current);
    m_networkTools->hide();

    connect(Sys::NetworkMonitor::instance(), &Sys::NetworkMonitor::networkChanged, this, [this] { updateNetwork(); });
    updateNetwork();
}

void GroupSectionBar::setNetworkSection(bool network) {
    {
        const QSignalBlocker blocker(m_sections);
        m_sections->setCurrentIndex(network ? 1 : 0);
    }
    // QTabWidget keeps a hidden corner widget's room, so the tools come and go as the corner widget.
    if (network == (m_groupTabs->cornerWidget(Qt::TopLeftCorner) == m_networkTools)) return;
    m_groupTabs->setCornerWidget(network ? m_networkTools : nullptr, Qt::TopLeftCorner);
    m_networkTools->setVisible(network);
}

void GroupSectionBar::updateNetwork() {
    const auto network = Sys::NetworkMonitor::instance()->current();
    if (!network.connected()) {
        m_current->setText(tr("No network"));
        m_sections->setTabToolTip(1, tr("Configs bound to a Wi-Fi or Ethernet network. No network is connected now."));
        return;
    }
    const QString kind = network.kind == Sys::NetworkIdentity::Kind::WiFi ? tr("Wi-Fi") : tr("Ethernet");
    m_current->setText(QStringLiteral("%1: %2").arg(kind, network.name));
    m_sections->setTabToolTip(1, tr("Configs bound to a Wi-Fi or Ethernet network. Now connected to %1 %2.").arg(kind, network.name));
}
