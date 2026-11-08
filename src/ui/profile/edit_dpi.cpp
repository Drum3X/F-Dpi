#include "include/ui/profile/edit_dpi.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "include/dpi/DpiLaunch.h"
#include "include/dpi/DpiPresets.h"
#include "include/global/Utils.hpp"
#include "include/ui/dpi/dialog_blockcheck.h"
#include "include/ui/utils/NativeFileDialog.h"
#include "include/ui/utils/ValidationForm.h"

EditDpi::EditDpi(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildStrategyTab(), tr("Strategy"));
    tabs->addTab(buildSitesTab(), tr("Site selection"));
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tabs);
    // The pages differ in height (and the site rows show per mode), so the dialog refits on every switch.
    connect(tabs, &QTabWidget::currentChanged, this, [this] {
        if (editor_state_changed) editor_state_changed();
    });
}

QWidget *EditDpi::buildStrategyTab() {
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);

    m_preset = new QComboBox(page);
    for (const auto &preset : Dpi::Presets()) m_preset->addItem(Dpi::PresetName(preset), preset.id);
    m_preset->addItem(tr("Custom"), QString());
    layout->addRow(tr("Preset"), m_preset);

    m_wfTcp = new QLineEdit(page);
    m_wfTcp->setPlaceholderText(QStringLiteral("80,443"));
#ifdef Q_OS_WIN
    m_wfTcp->setToolTip(tr("TCP ports winws captures (--wf-tcp). Separate ports and ranges with commas."));
#else
    m_wfTcp->setToolTip(tr("TCP ports whose connections go through nfqws (an nftables queue rule). Separate ports and ranges with commas."));
#endif
    layout->addRow(tr("TCP ports"), m_wfTcp);

    m_wfUdp = new QLineEdit(page);
    m_wfUdp->setPlaceholderText(QStringLiteral("443"));
#ifdef Q_OS_WIN
    m_wfUdp->setToolTip(tr("UDP ports winws captures (--wf-udp), e.g. 443 for QUIC. Leave empty to leave UDP alone."));
#else
    m_wfUdp->setToolTip(tr("UDP ports whose traffic goes through nfqws, e.g. 443 for QUIC. Leave empty to leave UDP alone."));
#endif
    layout->addRow(tr("UDP ports"), m_wfUdp);

    m_strategy = new QPlainTextEdit(page);
    m_strategy->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_strategy->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_strategy->setMinimumHeight(150);
#ifdef Q_OS_WIN
    m_strategy->setToolTip(tr("winws options. Separate profiles with --new; # starts a comment; %ZAPRET% is the winws folder."));
#else
    m_strategy->setToolTip(tr("nfqws options. Separate profiles with --new; # starts a comment; %ZAPRET% is the zapret folder. WinDivert options (--wf-*) do not apply."));
#endif
    layout->addRow(tr("Strategy"), m_strategy);

    auto *preview = new QPushButton(tr("Show command line"), page);
    connect(preview, &QPushButton::clicked, this, [this] { showCommandLine(); });
    auto *blockcheck = new QPushButton(tr("Blockcheck..."), page);
    blockcheck->setToolTip(tr("Find a strategy that opens this profile's sites on this network."));
    connect(blockcheck, &QPushButton::clicked, this, [this] { runBlockcheck(); });
    auto *actions = new QWidget(page);
    auto *actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->addWidget(blockcheck);
    actionsLayout->addWidget(preview);
    layout->addRow(QString(), actions);

    auto *hint = new QLabel(tr("Proxy and DPI Bypass never run together: starting this profile stops the running proxy."), page);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    layout->addRow(hint);

    connect(m_preset, &QComboBox::currentIndexChanged, this, [this](int index) { applyPreset(index); });
    connect(m_wfTcp, &QLineEdit::textEdited, this, [this] { markCustom(); });
    connect(m_wfUdp, &QLineEdit::textEdited, this, [this] { markCustom(); });
    connect(m_strategy, &QPlainTextEdit::textChanged, this, [this] { markCustom(); });
    return page;
}

QWidget *EditDpi::buildSitesTab() {
    auto *page = new QWidget(this);
    auto *layout = new QFormLayout(page);

    m_mode = new QComboBox(page);
    m_mode->addItem(tr("All sites"), QString::fromLatin1(Configs::dpi::ModeAll));
    m_mode->addItem(tr("Only listed sites"), QString::fromLatin1(Configs::dpi::ModeHostlist));
    m_mode->addItem(tr("Automatic: learn blocked sites"), QString::fromLatin1(Configs::dpi::ModeAutoHostlist));
    layout->addRow(tr("Apply bypass to"), m_mode);

    m_modeHint = new QLabel(page);
    m_modeHint->setWordWrap(true);
    m_modeHint->setEnabled(false);
    layout->addRow(QString(), m_modeHint);

    const auto makeList = [page] {
        auto *edit = new QPlainTextEdit(page);
        edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        // Capped: a QPlainTextEdit asks for ~200 px, and two of them would push the dialog off a 1080p screen.
        edit->setMinimumHeight(70);
        edit->setMaximumHeight(110);
        edit->setPlaceholderText(QStringLiteral("discord.com\nyoutube.com"));
        edit->setToolTip(tr("One domain per line; its subdomains match too. # starts a comment."));
        return edit;
    };

    m_hostlistLabel = new QLabel(tr("Sites"), page);
    m_hostlist = makeList();
    auto *hostlistBox = new QWidget(page);
    auto *hostlistLayout = new QVBoxLayout(hostlistBox);
    hostlistLayout->setContentsMargins(0, 0, 0, 0);
    hostlistLayout->addWidget(m_hostlist);
    m_hostlistButtons = buildListButtons(m_hostlist, true);
    hostlistLayout->addWidget(m_hostlistButtons);
    layout->addRow(m_hostlistLabel, hostlistBox);

    m_learnedRow = new QWidget(page);
    auto *learnedLayout = new QHBoxLayout(m_learnedRow);
    learnedLayout->setContentsMargins(0, 0, 0, 0);
    m_learned = new QLabel(m_learnedRow);
    m_learnedShow = new QPushButton(tr("Show"), m_learnedRow);
    m_learnedClear = new QPushButton(tr("Clear"), m_learnedRow);
    learnedLayout->addWidget(m_learned, 1);
    learnedLayout->addWidget(m_learnedShow);
    learnedLayout->addWidget(m_learnedClear);
    layout->addRow(tr("Learned"), m_learnedRow);

    m_exclude = makeList();
    m_exclude->setPlaceholderText(QStringLiteral("steampowered.com"));
    m_exclude->setToolTip(tr("Games mostly use UDP on their own ports. With only 443 in the UDP ports, their traffic is never "
                             "captured; add game launchers to \"Never touch\" to leave their web traffic alone too."));
    auto *excludeBox = new QWidget(page);
    auto *excludeLayout = new QVBoxLayout(excludeBox);
    excludeLayout->setContentsMargins(0, 0, 0, 0);
    excludeLayout->addWidget(m_exclude);
    excludeLayout->addWidget(buildListButtons(m_exclude, false));
    layout->addRow(tr("Never touch"), excludeBox);

    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] { updateModeRows(); });
    connect(m_learnedShow, &QPushButton::clicked, this, [this] {
        if (ent == nullptr) return;
        QFile file(Dpi::ListPath(QStringLiteral("autohostlist"), ent->id));
        const QString text = file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
        MessageBoxScrollable(tr("Learned sites"), text.isEmpty() ? tr("No site has been learned yet.") : text);
    });
    connect(m_learnedClear, &QPushButton::clicked, this, [this] {
        if (ent == nullptr) return;
        QFile::remove(Dpi::ListPath(QStringLiteral("autohostlist"), ent->id));
        updateLearned();
    });
    return page;
}

QWidget *EditDpi::buildListButtons(QPlainTextEdit *edit, bool blockedLists) {
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *add = new QToolButton(row);
    add->setText(tr("Add list"));
    add->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(add);
    for (const auto &list : blockedLists ? Dpi::BlockedSiteLists() : Dpi::ExcludedSiteLists()) {
        menu->addAction(Dpi::SiteListName(list), this, [edit, list] { appendHosts(edit, Dpi::SiteListHosts(list)); });
    }
    add->setMenu(menu);

    auto *import = new QPushButton(tr("Import from file..."), row);
    connect(import, &QPushButton::clicked, this, [this, edit] {
        const auto path = NativeFileDialog::getOpenFileName(this, tr("Import site list"), {}, tr("Text files (*.txt);;All files (*)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            MessageBoxWarning(tr("Import site list"), file.errorString());
            return;
        }
        appendHosts(edit, QString::fromUtf8(file.readAll()).split('\n'));
    });

    layout->addWidget(add);
    layout->addWidget(import);
    layout->addStretch(1);
    return row;
}

void EditDpi::onStart(std::shared_ptr<Configs::Profile> _ent) {
    ent = _ent;
    const auto *config = ent->Dpi();
    m_loading = true;
    m_wfTcp->setText(config->wfTcp);
    m_wfUdp->setText(config->wfUdp);
    m_strategy->setPlainText(config->strategy);
    const int preset = m_preset->findData(config->preset);
    m_preset->setCurrentIndex(preset >= 0 ? preset : m_preset->count() - 1);
    const int mode = m_mode->findData(config->mode);
    m_mode->setCurrentIndex(mode >= 0 ? mode : 0);
    m_hostlist->setPlainText(config->hostlist.join('\n'));
    m_exclude->setPlainText(config->exclude.join('\n'));
    m_loading = false;
    updateModeRows();
}

bool EditDpi::onEnd() {
    storeInto(ent->Dpi());
    return true;
}

void EditDpi::storeInto(Configs::dpi *config) const {
    config->preset = m_preset->currentData().toString();
    config->wfTcp = m_wfTcp->text().trimmed();
    config->wfUdp = m_wfUdp->text().trimmed();
    config->strategy = m_strategy->toPlainText();
    config->mode = m_mode->currentData().toString();
    config->hostlist = Configs::dpi::NormalizeHosts(lines(m_hostlist));
    config->exclude = Configs::dpi::NormalizeHosts(lines(m_exclude));
}

void EditDpi::bindValidation(Validation::Form &form) {
    m_form = &form;
    form.bind(m_wfTcp, Configs::Checks::DpiPorts, this);
    form.bind(m_wfUdp, Configs::Checks::DpiPorts, this);
    form.bind(m_strategy, Configs::Checks::DpiStrategy, this);
    form.bindLines(m_hostlist, Configs::Checks::DpiHost, this);
    form.bindLines(m_exclude, Configs::Checks::DpiHost, this);
    form.addRule({m_wfTcp, m_wfUdp}, [this] {
        if (!m_wfTcp->text().trimmed().isEmpty() || !m_wfUdp->text().trimmed().isEmpty()) return QString();
        return tr("Enter TCP or UDP ports; with neither, no traffic is captured.");
    }, this);
    form.addRule({m_mode, m_hostlist}, [this] {
        if (m_mode->currentData().toString() != QLatin1String(Configs::dpi::ModeHostlist)) return QString();
        if (!Configs::dpi::NormalizeHosts(lines(m_hostlist)).isEmpty()) return QString();
        return tr("Add at least one site, or apply the bypass to all sites.");
    }, this);
}

void EditDpi::applyPreset(int index) {
    if (m_loading) return;
    const auto *preset = Dpi::FindPreset(m_preset->itemData(index).toString());
    if (preset == nullptr) return;
    m_loading = true;
    m_wfTcp->setText(preset->wfTcp);
    m_wfUdp->setText(preset->wfUdp);
    m_strategy->setPlainText(preset->strategy);
    m_loading = false;
}

void EditDpi::markCustom() {
    if (m_loading) return;
    const QSignalBlocker blocker(m_preset);
    m_preset->setCurrentIndex(m_preset->count() - 1);
}

void EditDpi::updateModeRows() {
    const auto mode = m_mode->currentData().toString();
    const bool all = mode == QLatin1String(Configs::dpi::ModeAll);
    const bool automatic = mode == QLatin1String(Configs::dpi::ModeAutoHostlist);
    if (all) {
        m_modeHint->setText(tr("Every connection on the captured ports is changed. Simplest, but some sites or games may misbehave."));
    } else if (automatic) {
#ifdef Q_OS_WIN
        m_modeHint->setText(tr("winws watches connections and adds a site to the learned list once it looks blocked; only those sites "
                               "and the ones below are changed."));
#else
        m_modeHint->setText(tr("nfqws watches connections and adds a site to the learned list once it looks blocked; only those sites "
                               "and the ones below are changed."));
#endif
    } else {
        m_modeHint->setText(tr("Only the sites below (and their subdomains) are changed; everything else is left alone."));
    }
    m_hostlistLabel->setText(automatic ? tr("Start with") : tr("Sites"));
    m_hostlistLabel->setVisible(!all);
    m_hostlist->parentWidget()->setVisible(!all);
    m_learnedRow->setVisible(automatic);
    if (auto *label = qobject_cast<QFormLayout *>(m_learnedRow->parentWidget()->layout())) {
        if (auto *item = label->labelForField(m_learnedRow)) item->setVisible(automatic);
    }
    updateLearned();
    if (m_form != nullptr) m_form->refresh();
    if (editor_state_changed) editor_state_changed();
}

void EditDpi::updateLearned() {
    const bool saved = ent != nullptr && ent->id >= 0;
    int count = 0;
    if (saved) {
        QFile file(Dpi::ListPath(QStringLiteral("autohostlist"), ent->id));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            count = Configs::dpi::NormalizeHosts(QString::fromUtf8(file.readAll()).split('\n')).size();
        }
    }
    m_learned->setText(saved ? tr("%n site(s)", nullptr, count) : tr("Available once the profile is saved and run."));
    m_learnedShow->setEnabled(saved);
    m_learnedClear->setEnabled(saved && count > 0);
}

void EditDpi::showCommandLine() {
    Configs::dpi preview;
    storeInto(&preview);
    Dpi::Launch launch;
    QString error;
    if (!Dpi::BuildLaunch(preview, ent != nullptr ? ent->id : -1, &launch, &error, true)) {
        MessageBoxWarning(tr("Command line"), error);
        return;
    }
    MessageBoxScrollable(tr("Command line"), Dpi::DisplayCommandLine(launch));
}

void EditDpi::runBlockcheck() {
    DialogBlockcheck dialog(Configs::dpi::NormalizeHosts(lines(m_hostlist)), true, this);
    if (dialog.exec() != QDialog::Accepted || !dialog.result().found()) return;
    const auto &result = dialog.result();
    m_loading = true;
    m_wfTcp->setText(result.wfTcp);
    m_wfUdp->setText(result.wfUdp);
    m_strategy->setPlainText(result.strategy);
    m_loading = false;
    markCustom();
}

void EditDpi::appendHosts(QPlainTextEdit *edit, const QStringList &hosts) {
    auto current = Configs::dpi::NormalizeHosts(lines(edit));
    const int before = current.size();
    for (const auto &host : Configs::dpi::NormalizeHosts(hosts)) {
        if (!current.contains(host)) current << host;
    }
    if (current.size() != before) edit->setPlainText(current.join('\n'));
}

QStringList EditDpi::lines(const QPlainTextEdit *edit) {
    return edit->toPlainText().split('\n');
}
