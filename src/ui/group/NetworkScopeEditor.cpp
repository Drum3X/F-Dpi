#include "include/ui/group/NetworkScopeEditor.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>
#include <QPushButton>

#include "include/database/GroupsRepo.h"
#include "include/database/NetworkScopesRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/global/Configs.hpp"
#include "include/sys/NetworkMonitor.hpp"

namespace {
    bool networkScopeEditorDefaultNetwork = false;
    constexpr int kNetworkScopeKeyRole = Qt::UserRole;
    constexpr int kNetworkScopeNameRole = Qt::UserRole + 1;
} // namespace

void NetworkScopeEditor::SetDefaultForNew(bool network) {
    networkScopeEditorDefaultNetwork = network;
}

NetworkScopeEditor::NetworkScopeEditor(const std::shared_ptr<Configs::Group> &group, QWidget *parent) : QGroupBox(tr("Scope"), parent) {
    auto *layout = new QFormLayout(this);

    m_scope = new QComboBox(this);
    m_scope->addItem(tr("General: any network"), false);
    m_scope->addItem(tr("Custom (per network)"), true);
    layout->addRow(tr("Use on"), m_scope);

    m_networkBox = new QWidget(this);
    auto *boxLayout = new QFormLayout(m_networkBox);
    boxLayout->setContentsMargins(0, 0, 0, 0);

    m_networks = new QListWidget(m_networkBox);
    m_networks->setMaximumHeight(90);
    auto *buttons = new QWidget(m_networkBox);
    auto *buttonsLayout = new QHBoxLayout(buttons);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    m_addCurrent = new QPushButton(tr("Add current network"), buttons);
    m_remove = new QPushButton(tr("Remove"), buttons);
    buttonsLayout->addWidget(m_addCurrent);
    buttonsLayout->addWidget(m_remove);
    buttonsLayout->addStretch(1);
    auto *networksBox = new QWidget(m_networkBox);
    auto *networksLayout = new QVBoxLayout(networksBox);
    networksLayout->setContentsMargins(0, 0, 0, 0);
    networksLayout->addWidget(m_networks);
    networksLayout->addWidget(buttons);
    boxLayout->addRow(tr("Networks"), networksBox);

    m_profile = new QComboBox(m_networkBox);
    m_profile->addItem(tr("First profile of the group"), -1);
    boxLayout->addRow(tr("Run"), m_profile);

    m_hint = new QLabel(m_networkBox);
    m_hint->setWordWrap(true);
    m_hint->setEnabled(false);
    boxLayout->addRow(m_hint);
    layout->addRow(m_networkBox);

    Configs::GroupScope scope;
    if (group->id >= 0) {
        scope = Configs::dataManager->networkScopesRepo->Get(group->id);
    } else {
        scope.network = networkScopeEditorDefaultNetwork;
    }
    m_scope->setCurrentIndex(scope.network ? 1 : 0);
    for (const auto &network : scope.networks) addNetwork(network.keys, network.name);
    if (group->id >= 0) {
        for (const auto &profile : Configs::dataManager->profilesRepo->GetProfileBatch(group->Profiles())) {
            if (profile == nullptr || profile->outbound == nullptr) continue;
            m_profile->addItem(profile->outbound->DisplayTypeAndName(), profile->id);
        }
        const int index = m_profile->findData(scope.profileId);
        m_profile->setCurrentIndex(index >= 0 ? index : 0);
    }

    connect(m_scope, &QComboBox::currentIndexChanged, this, [this] { updateRows(); });
    connect(m_networks, &QListWidget::currentRowChanged, this, [this] { updateRows(); });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        delete m_networks->takeItem(m_networks->currentRow());
        updateRows();
    });
    connect(m_addCurrent, &QPushButton::clicked, this, [this] {
        const auto network = Sys::NetworkMonitor::instance()->current();
        if (network.connected()) addNetwork(network.keys, network.name);
        updateRows();
    });
    connect(Sys::NetworkMonitor::instance(), &Sys::NetworkMonitor::networkChanged, this, [this] { updateRows(); });
    updateRows();
}

bool NetworkScopeEditor::listed(const QStringList &keys) const {
    for (int i = 0; i < m_networks->count(); ++i) {
        const Configs::BoundNetwork bound{m_networks->item(i)->data(kNetworkScopeKeyRole).toStringList(), {}};
        if (bound.matches(keys)) return true;
    }
    return false;
}

void NetworkScopeEditor::addNetwork(const QStringList &keys, const QString &name) {
    if (keys.isEmpty() || listed(keys)) return;
    auto *item = new QListWidgetItem(name, m_networks);
    item->setData(kNetworkScopeKeyRole, keys);
    item->setData(kNetworkScopeNameRole, name);
    item->setToolTip(keys.join('\n'));
}

void NetworkScopeEditor::updateRows() {
    const bool network = m_scope->currentData().toBool();
    m_networkBox->setVisible(network);
    const auto current = Sys::NetworkMonitor::instance()->current();
    m_addCurrent->setEnabled(current.connected() && !listed(current.keys));
    m_addCurrent->setToolTip(current.connected() ? current.name : tr("No network is connected."));
    m_remove->setEnabled(m_networks->currentRow() >= 0);
    m_profile->setEnabled(m_profile->count() > 1);
    if (m_networks->count() == 0) {
        m_hint->setText(tr("Add the networks this group is for; until then it never runs on its own."));
    } else if (m_profile->count() <= 1) {
        m_hint->setText(tr("Add profiles to the group; its first profile runs on these networks."));
    } else {
        m_hint->setText(tr("On these networks this profile runs automatically; elsewhere the general config runs."));
    }
    emit layoutChanged();
}

void NetworkScopeEditor::store(const std::shared_ptr<Configs::Group> &group) const {
    Configs::GroupScope scope;
    scope.groupId = group->id;
    scope.network = m_scope->currentData().toBool();
    for (int i = 0; i < m_networks->count(); ++i) {
        const auto *item = m_networks->item(i);
        scope.networks << Configs::BoundNetwork{item->data(kNetworkScopeKeyRole).toStringList(), item->data(kNetworkScopeNameRole).toString()};
    }
    scope.profileId = m_profile->currentData().toInt();
    Configs::dataManager->networkScopesRepo->SetPending(group, scope);
}
