#include "include/ui/group/GroupSectionBar.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

#include "include/global/Configs.hpp"
#include "include/sys/NetworkMonitor.hpp"

GroupSectionBar::GroupSectionBar(QWidget *parent) : QWidget(parent) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 6, 0);
    layout->setSpacing(2);

    const auto makeSection = [this](const QString &text) {
        auto *button = new QToolButton(this);
        button->setText(text);
        button->setCheckable(true);
        button->setAutoRaise(true);
        return button;
    };
    m_general = makeSection(tr("General"));
    m_general->setToolTip(tr("Configs used on any network without a config of its own."));
    m_network = makeSection(tr("Custom (per network)"));
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(m_general);
    group->addButton(m_network);
    m_general->setChecked(true);
    connect(m_general, &QToolButton::clicked, this, [this] { emit sectionRequested(false); });
    connect(m_network, &QToolButton::clicked, this, [this] { emit sectionRequested(true); });

    m_auto = new QToolButton(this);
    m_auto->setText(tr("Auto switch"));
    m_auto->setCheckable(true);
    m_auto->setAutoRaise(true);
    m_auto->setToolTip(tr("When the network changes, run its own config (Custom groups bound to it), else the last general config."));
    m_auto->setChecked(Configs::dataManager->settingsRepo->network_auto_switch);
    connect(m_auto, &QToolButton::toggled, this, [](bool on) {
        Configs::dataManager->settingsRepo->network_auto_switch = on;
        Configs::dataManager->settingsRepo->Save();
    });

    m_current = new QLabel(this);
    m_current->setEnabled(false);

    layout->addWidget(m_general);
    layout->addWidget(m_network);
    layout->addSpacing(6);
    layout->addWidget(m_auto);
    layout->addWidget(m_current);

    connect(Sys::NetworkMonitor::instance(), &Sys::NetworkMonitor::networkChanged, this, [this] { updateNetwork(); });
    updateNetwork();
}

void GroupSectionBar::setNetworkSection(bool network) {
    (network ? m_network : m_general)->setChecked(true);
}

void GroupSectionBar::updateNetwork() {
    const auto network = Sys::NetworkMonitor::instance()->current();
    if (!network.connected()) {
        m_current->setText(tr("No network"));
        m_network->setToolTip(tr("Configs bound to a Wi-Fi or Ethernet network. No network is connected now."));
        return;
    }
    const QString kind = network.kind == Sys::NetworkIdentity::Kind::WiFi ? tr("Wi-Fi") : tr("Ethernet");
    m_current->setText(QStringLiteral("%1: %2").arg(kind, network.name));
    m_network->setToolTip(tr("Configs bound to a Wi-Fi or Ethernet network. Now connected to %1 %2.").arg(kind, network.name));
}
