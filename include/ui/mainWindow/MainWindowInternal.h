#pragma once

// Shared internals of src/ui/mainWindow/: everything must be inline or a macro (unity builds).

#include <QDialog>

#include "include/global/Configs.hpp"
#include "include/database/GroupsRepo.h"
#include "include/database/NetworkScopesRepo.h"

inline bool dialog_is_using = false;

inline bool mw_sub_updating = false;

// Expands inside a MainWindow member function: uses `this` and `connect`.
#define USE_DIALOG(a)                                    \
    if (dialog_is_using) return;                         \
    dialog_is_using = true;                              \
    auto dialog = new a(this);                           \
    connect(dialog, &QDialog::finished, this, [=, this] { \
        dialog->deleteLater();                           \
        dialog_is_using = false;                         \
    });                                                  \
    dialog->show();

// F-Dpi: the tab bar lists one section at a time, General or Custom (per network); refresh_groups() picks it.
inline bool mw_network_section = false;

inline bool groupInNetworkSection(int gid) {
    return Configs::dataManager->networkScopesRepo->Get(gid).network;
}

// The shown section's groups in tab order; all groups when that section is empty, so the tab bar never is.
inline QList<int> visibleGroupsTabOrder() {
    const auto all = Configs::dataManager->groupsRepo->GetGroupsTabOrder();
    QList<int> shown;
    for (const int gid : all) {
        if (groupInNetworkSection(gid) == mw_network_section) shown << gid;
    }
    return shown.isEmpty() ? all : shown;
}

inline int tabIndex2GroupId(int index) {
    auto tabOrder = visibleGroupsTabOrder();
    if (tabOrder.length() <= index) return -1;
    return tabOrder[index];
}

inline int groupId2TabIndex(int gid) {
    auto tabOrder = visibleGroupsTabOrder();
    for (int key = 0; key < tabOrder.count(); key++) {
        if (tabOrder[key] == gid) return key;
    }
    return 0;
}
