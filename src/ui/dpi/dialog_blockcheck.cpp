#include "include/ui/dpi/dialog_blockcheck.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "include/database/GroupsRepo.h"
#include "include/database/NetworkScopesRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/engine/EngineHost.h"
#include "include/global/Configs.hpp"
#include "include/global/Utils.hpp"

namespace {
    const QStringList kBlockcheckDefaultDomains = {QStringLiteral("discord.com"), QStringLiteral("youtube.com")};
}

DialogBlockcheck::DialogBlockcheck(const QStringList &domains, bool applyToProfile, QWidget *parent)
    : QDialog(parent), m_applyToProfile(applyToProfile) {
    setWindowTitle(tr("Blockcheck"));
    setAttribute(Qt::WA_DeleteOnClose, !applyToProfile);
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(tr("Finds a DPI bypass strategy that opens the sites below on this network. Profiles are stopped "
                                "during the scan and started again afterwards."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout;
    m_domains = new QPlainTextEdit(this);
    m_domains->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_domains->setMaximumHeight(80);
    m_domains->setPlainText((domains.isEmpty() ? kBlockcheckDefaultDomains : domains.mid(0, 8)).join('\n'));
    m_domains->setToolTip(tr("Blocked sites to test, one per line. A few are enough; each one adds probes to every strategy."));
    form->addRow(tr("Sites"), m_domains);

    m_mode = new QComboBox(this);
    m_mode->addItem(tr("Quick scan: common strategies, stops at the first that works"), false);
    m_mode->addItem(tr("Deep scan: every combination (can take 30 minutes)"), true);
    form->addRow(tr("Scan"), m_mode);

    auto *protocols = new QWidget(this);
    auto *protocolsLayout = new QHBoxLayout(protocols);
    protocolsLayout->setContentsMargins(0, 0, 0, 0);
    m_http = new QCheckBox(QStringLiteral("HTTP"), protocols);
    m_tls12 = new QCheckBox(QStringLiteral("TLS 1.2"), protocols);
    m_tls13 = new QCheckBox(QStringLiteral("TLS 1.3"), protocols);
    for (auto *box : {m_http, m_tls12, m_tls13}) {
        box->setChecked(true);
        protocolsLayout->addWidget(box);
    }
    auto *quic = new QLabel(tr("QUIC: not testable, a standard QUIC bypass is added"), protocols);
    quic->setEnabled(false);
    protocolsLayout->addWidget(quic);
    protocolsLayout->addStretch(1);
    form->addRow(tr("Protocols"), protocols);
    layout->addLayout(form);

    auto *controls = new QHBoxLayout;
    m_start = new QPushButton(tr("Start"), this);
    m_stop = new QPushButton(tr("Stop"), this);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    controls->addWidget(m_start);
    controls->addWidget(m_stop);
    controls->addWidget(m_progress, 1);
    layout->addLayout(controls);

    m_current = new QLabel(this);
    m_current->setEnabled(false);
    m_current->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_current);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({tr("Target"), tr("Strategy"), tr("Opened")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setMinimumHeight(160);
    layout->addWidget(m_table, 1);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(110);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_log);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto *buttons = new QHBoxLayout;
    m_apply = new QPushButton(applyToProfile ? tr("Use in this profile") : tr("Save as new DPI profile"), this);
    m_apply->setEnabled(false);
    auto *close = new QPushButton(tr("Close"), this);
    buttons->addStretch(1);
    buttons->addWidget(m_apply);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    m_blockcheck = new Engine::Blockcheck(Engine::host()->connection(), this);
    connect(m_blockcheck, &Engine::Blockcheck::message, this, [this](const QString &line) { m_log->appendPlainText(line); });
    connect(m_blockcheck, &Engine::Blockcheck::progress, this, [this](int done, int total, const QString &current) {
        m_progress->setRange(0, qMax(1, total));
        m_progress->setValue(done);
        m_current->setText(current);
    });
    connect(m_blockcheck, &Engine::Blockcheck::strategyTested, this,
            [this](const QString &target, const QString &strategy, int passed, int total) {
                const int row = m_table->rowCount();
                m_table->insertRow(row);
                m_table->setItem(row, 0, new QTableWidgetItem(target));
                m_table->setItem(row, 1, new QTableWidgetItem(strategy));
                auto *score = new QTableWidgetItem(passed < 0 ? tr("winws refused") : QStringLiteral("%1/%2").arg(passed).arg(total));
                if (passed > 0 && passed == total) score->setForeground(QColor(0x2e, 0x9e, 0x44));
                m_table->setItem(row, 2, score);
                m_table->scrollToBottom();
            });
    connect(m_blockcheck, &Engine::Blockcheck::finished, this, [this](const Engine::BlockcheckResult &result) { onFinished(result); });

    connect(m_start, &QPushButton::clicked, this, [this] { startScan(); });
    connect(m_stop, &QPushButton::clicked, this, [this] { m_blockcheck->cancel(); });
    connect(close, &QPushButton::clicked, this, [this] { reject(); });
    connect(m_apply, &QPushButton::clicked, this, [this] {
        if (m_applyToProfile) accept();
        else saveAsProfile();
    });
    setRunning(false);
    resize(760, 620);
}

void DialogBlockcheck::setRunning(bool running) {
    m_start->setEnabled(!running);
    m_stop->setEnabled(running);
    for (QWidget *w : std::initializer_list<QWidget *>{m_domains, m_mode, m_http, m_tls12, m_tls13}) w->setEnabled(!running);
    if (running) m_apply->setEnabled(false);
}

void DialogBlockcheck::startScan() {
    Engine::BlockcheckOptions options;
    for (const auto &line : m_domains->toPlainText().split('\n')) {
        const auto domain = Configs::dpi::NormalizeHost(line);
        if (!domain.isEmpty() && !options.domains.contains(domain)) options.domains << domain;
    }
    if (options.domains.isEmpty()) {
        MessageBoxWarning(windowTitle(), tr("Enter at least one blocked site."));
        return;
    }
    options.deep = m_mode->currentData().toBool();
    options.http = m_http->isChecked();
    options.tls12 = m_tls12->isChecked();
    options.tls13 = m_tls13->isChecked();
    if (!options.http && !options.tls12 && !options.tls13) {
        MessageBoxWarning(windowTitle(), tr("Choose at least one protocol."));
        return;
    }
    m_table->setRowCount(0);
    m_log->clear();
    m_summary->clear();
    m_current->clear();
    m_result = {};
    if (!m_blockcheck->start(options)) return;
    setRunning(true);
}

void DialogBlockcheck::onFinished(const Engine::BlockcheckResult &result) {
    m_result = result;
    setRunning(false);
    m_current->clear();
    if (!result.error.isEmpty()) {
        m_summary->setText(tr("Blockcheck could not run: %1").arg(result.error));
    } else if (result.httpBlocked == 0 && result.tlsBlocked == 0 && !result.cancelled) {
        m_summary->setText(tr("These sites are not blocked on this network; no bypass is needed."));
    } else if (!result.found()) {
        m_summary->setText(result.cancelled ? tr("Stopped before a working strategy was found.")
                                            : tr("No strategy opened these sites. Try the deep scan, or a proxy profile instead."));
    } else {
        QStringList lines;
        if (!result.bestHttp.isEmpty()) lines << tr("HTTP: %1 (%2/%3)").arg(result.bestHttp).arg(result.bestHttpPassed).arg(result.httpBlocked);
        if (!result.bestTls.isEmpty()) lines << tr("TLS: %1 (%2/%3)").arg(result.bestTls).arg(result.bestTlsPassed).arg(result.tlsBlocked);
        m_summary->setText((result.cancelled ? tr("Best found before stopping:") : tr("Best strategy:")) + QStringLiteral("\n") + lines.join('\n'));
        m_apply->setEnabled(true);
    }
    if (m_closeWhenDone) QDialog::reject();
}

void DialogBlockcheck::saveAsProfile() {
    // Into the current group when it is a general one, else the first general group: a Blockcheck result is no network's own.
    int gid = Configs::dataManager->settingsRepo->current_group;
    if (Configs::dataManager->networkScopesRepo->Get(gid).network || Configs::dataManager->groupsRepo->GetGroup(gid) == nullptr) {
        for (const int id : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
            if (!Configs::dataManager->networkScopesRepo->Get(id).network) {
                gid = id;
                break;
            }
        }
    }
    auto ent = Configs::dataManager->profilesRepo->NewProfile(QStringLiteral("dpi"));
    auto *config = ent->Dpi();
    config->name = tr("Blockcheck %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("dd.MM HH:mm")));
    config->preset.clear();
    config->strategy = m_result.strategy;
    config->wfTcp = m_result.wfTcp;
    config->wfUdp = m_result.wfUdp;
    if (!Configs::dataManager->profilesRepo->AddProfile(ent, gid)) return;
    MW_dialog_message(MwMessage::ProfileChanged, {});
    MessageBoxInfo(windowTitle(), tr("Saved as the DPI profile \"%1\". Start it from the profile list.").arg(config->name));
    m_apply->setEnabled(false);
}

void DialogBlockcheck::closeEvent(QCloseEvent *event) {
    if (m_blockcheck->running()) {
        // The scan stops winws and restarts the stopped profile on its way out; the dialog closes after that.
        m_closeWhenDone = true;
        m_blockcheck->cancel();
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void DialogBlockcheck::reject() {
    if (m_blockcheck->running()) {
        m_closeWhenDone = true;
        m_blockcheck->cancel();
        return;
    }
    QDialog::reject();
}
