#include "include/ui/group/GroupSectionBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "include/sys/NetworkMonitor.hpp"

GroupSectionBar::GroupSectionBar(QTabWidget *groupTabs) : QWidget(nullptr) {
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

    connect(Sys::NetworkMonitor::instance(), &Sys::NetworkMonitor::networkChanged, this, [this] { updateNetwork(); });
    updateNetwork();
}

namespace {
    QString groupSectionNetworkKind(const Sys::NetworkIdentity &network) {
        return network.kind == Sys::NetworkIdentity::Kind::WiFi ? GroupSectionBar::tr("Wi-Fi") : GroupSectionBar::tr("Ethernet");
    }
} // namespace

QLabel *GroupSectionBar::NewNetworkLabel(QWidget *parent) {
    auto *label = new QLabel(parent);
    const auto update = [label] {
        const auto network = Sys::NetworkMonitor::instance()->current();
        label->setText(network.connected() ? QStringLiteral("%1: %2").arg(groupSectionNetworkKind(network), network.name) : tr("No network"));
    };
    connect(Sys::NetworkMonitor::instance(), &Sys::NetworkMonitor::networkChanged, label, update);
    update();
    return label;
}

void GroupSectionBar::setNetworkSection(bool network) {
    const QSignalBlocker blocker(m_sections);
    m_sections->setCurrentIndex(network ? 1 : 0);
}

void GroupSectionBar::updateNetwork() {
    const auto network = Sys::NetworkMonitor::instance()->current();
    if (!network.connected()) {
        m_sections->setTabToolTip(1, tr("Configs bound to a Wi-Fi or Ethernet network. No network is connected now."));
        return;
    }
    m_sections->setTabToolTip(1, tr("Configs bound to a Wi-Fi or Ethernet network. Now connected to %1 %2.").arg(groupSectionNetworkKind(network), network.name));
}
