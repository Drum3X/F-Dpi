#include "include/ui/dns/dialog_auto_dns.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>

#include "include/configs/outbounds/dpi.h"
#include "include/database/DatabaseManager.h"
#include "include/database/SettingsRepo.h"
#include "include/dns/AutoDns.h"
#include "include/engine/EngineHost.h"
#include "include/engine/dns/AutoDnsService.h"
#include "include/global/Utils.hpp"
#include "include/sys/SystemDns.hpp"

namespace {
    QTableWidgetItem *autoDnsItem(const QString &text, const QColor &color = {}) {
        auto *item = new QTableWidgetItem(text);
        if (color.isValid()) item->setForeground(color);
        return item;
    }

    const QColor kAutoDnsGood(0x2e, 0x9e, 0x44);
    const QColor kAutoDnsBad(0xd0, 0x3a, 0x2f);
    const QColor kAutoDnsWarn(0xc0, 0x80, 0x10);

    QColor autoDnsVerdictColor(Engine::DnsVerdict verdict) {
        switch (verdict) {
            case Engine::DnsVerdict::Clean: return kAutoDnsGood;
            case Engine::DnsVerdict::Poisoned:
            case Engine::DnsVerdict::NoAnswer: return kAutoDnsBad;
            case Engine::DnsVerdict::Different: return kAutoDnsWarn;
            case Engine::DnsVerdict::Unknown: break;
        }
        return {};
    }

    QString autoDnsVerdictCell(const QStringList &addresses, Engine::DnsVerdict verdict) {
        const auto verdictText = Engine::DnsBenchmark::VerdictText(verdict);
        if (addresses.isEmpty()) return verdictText;
        return QStringLiteral("%1 — %2").arg(verdictText, addresses.mid(0, 2).join(QStringLiteral(", ")));
    }
} // namespace

DialogAutoDns::DialogAutoDns(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Automatic DNS"));
    setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(this);
    const auto &settings = Configs::dataManager->settingsRepo;

    auto *intro = new QLabel(tr("Many networks answer DNS queries for blocked sites with a wrong address (DNS poisoning). Automatic DNS "
                                "uses a fast encrypted (DNS-over-HTTPS) server instead, which the network cannot read or change."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *settingsBox = new QGroupBox(tr("Settings"), this);
    auto *form = new QFormLayout(settingsBox);
    m_mode = new QComboBox(settingsBox);
    m_mode->addItem(tr("Automatic: the fastest on this network"), Dns::kModeAuto);
    for (const auto &provider : Dns::Providers()) m_mode->addItem(provider.name, provider.id);
    m_mode->addItem(tr("Off: use the DNS settings as they are"), Dns::kModeOff);
    m_mode->setCurrentIndex(qMax(0, m_mode->findData(settings->auto_dns_mode)));
    form->addRow(tr("Server"), m_mode);
    m_proxy = new QCheckBox(tr("Proxy profiles: use it as the direct DNS"), settingsBox);
    m_proxy->setChecked(settings->auto_dns_proxy);
    m_proxy->setToolTip(tr("Replaces the direct DNS of the DNS settings, unless the routing profile has its own DNS. "
                           "The remote DNS already goes through the proxy."));
    form->addRow(m_proxy);
#ifdef Q_OS_WIN
    m_system = new QCheckBox(tr("DPI Bypass: point Windows' DNS at it while DPI Bypass runs (DNS-over-HTTPS required)"), settingsBox);
    m_system->setChecked(settings->auto_dns_system);
    m_system->setToolTip(tr("winws does not touch DNS. The network adapters' DNS settings are saved first and put back when "
                            "DPI Bypass stops, and at the next start if F-Dpi was closed abruptly. A network whose DNS servers you set in "
                            "Windows is left as it is."));
    if (!Sys::SystemDns::Supported()) {
        m_system->setEnabled(false);
        m_system->setToolTip(tr("Needs Windows 11."));
    }
#else
    m_system = new QCheckBox(tr("DPI Bypass: point the system DNS (systemd-resolved) at it while DPI Bypass runs (DNS-over-TLS required)"), settingsBox);
    m_system->setChecked(settings->auto_dns_system);
    m_system->setToolTip(tr("nfqws does not touch DNS. The network connection's DNS settings are saved first and put back when "
                            "DPI Bypass stops, and at the next start if F-Dpi was closed abruptly. A connection whose DNS servers you set in "
                            "NetworkManager is left as it is. OpenDNS has no DNS-over-TLS, so Cloudflare is used instead."));
    if (!Sys::SystemDns::Supported()) {
        m_system->setEnabled(false);
        m_system->setToolTip(tr("Needs systemd-resolved, which this system does not resolve through."));
    }
#endif
    form->addRow(m_system);
    m_status = new QLabel(settingsBox);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Now"), m_status);
    layout->addWidget(settingsBox);

    auto *testBox = new QGroupBox(tr("Test"), this);
    auto *testLayout = new QVBoxLayout(testBox);
    auto *testForm = new QFormLayout;
    m_domains = new QLineEdit(Dns::DefaultTestDomains().join(QStringLiteral(", ")), testBox);
    m_domains->setToolTip(tr("Sites that may be blocked here; the poisoning test compares their answers."));
    testForm->addRow(tr("Sites"), m_domains);
    testLayout->addLayout(testForm);
    auto *controls = new QHBoxLayout;
    m_test = new QPushButton(tr("Test now"), testBox);
    m_progress = new QProgressBar(testBox);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    controls->addWidget(m_test);
    controls->addWidget(m_progress, 1);
    testLayout->addLayout(controls);

    m_servers = new QTableWidget(0, 4, testBox);
    m_servers->setHorizontalHeaderLabels({tr("Server"), tr("Connect"), tr("Query"), tr("Status")});
    m_servers->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_servers->verticalHeader()->setVisible(false);
    m_servers->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_servers->setSelectionMode(QAbstractItemView::NoSelection);
    m_servers->setMinimumHeight(150);
    testLayout->addWidget(m_servers, 1);

    m_poison = new QTableWidget(0, 4, testBox);
    m_poison->setHorizontalHeaderLabels({tr("Site"), tr("System DNS"), tr("Plain DNS (1.1.1.1)"), tr("Encrypted DNS")});
    for (int column = 1; column < 4; ++column) m_poison->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Stretch);
    m_poison->verticalHeader()->setVisible(false);
    m_poison->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_poison->setSelectionMode(QAbstractItemView::NoSelection);
    m_poison->setMinimumHeight(150);
    testLayout->addWidget(m_poison, 1);

    m_summary = new QLabel(testBox);
    m_summary->setWordWrap(true);
    testLayout->addWidget(m_summary);
    layout->addWidget(testBox, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    // Qt's own button texts are not translated here.
    buttons->button(QDialogButtonBox::Save)->setText(tr("Save"));
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &DialogAutoDns::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &DialogAutoDns::reject);

    m_benchmark = new Engine::DnsBenchmark(this);
    connect(m_benchmark, &Engine::DnsBenchmark::message, this, [this](const QString &line) { m_summary->setText(line); });
    connect(m_benchmark, &Engine::DnsBenchmark::serverMeasured, this, [this](const Engine::DnsServerScore &score) {
        const int row = m_servers->rowCount();
        m_servers->insertRow(row);
        m_servers->setItem(row, 0, autoDnsItem(score.name));
        m_servers->setItem(row, 1, autoDnsItem(score.connectMs >= 0 ? tr("%1 ms").arg(score.connectMs) : QStringLiteral("—")));
        m_servers->setItem(row, 2, autoDnsItem(score.medianMs >= 0 ? tr("%1 ms").arg(score.medianMs) : QStringLiteral("—")));
        QString status;
        QColor color;
        if (!score.reachable) {
            status = tr("unreachable (blocked?): %1").arg(score.note);
            color = kAutoDnsBad;
        } else if (score.filtered) {
            status = tr("filters: %1").arg(score.note);
            color = kAutoDnsBad;
        } else if (!score.usable()) {
            status = score.note;
            color = kAutoDnsWarn;
        } else {
            status = tr("works");
            color = kAutoDnsGood;
        }
        m_servers->setItem(row, 3, autoDnsItem(status, color));
    });
    connect(m_benchmark, &Engine::DnsBenchmark::finished, this, [this](const Engine::DnsBenchmarkResult &result) { onFinished(result); });
    connect(m_test, &QPushButton::clicked, this, [this] { startTest(); });
    if (auto *service = Engine::host()->autoDns()) connect(service, &Engine::AutoDnsService::changed, this, [this] { refreshStatus(); });

    refreshStatus();
    setRunning(false);
    resize(780, 680);
}

void DialogAutoDns::refreshStatus() {
    const auto &settings = Configs::dataManager->settingsRepo;
    QStringList lines;
    const auto *active = Dns::ActiveProvider();
    if (active == nullptr) {
        lines << tr("Off.");
    } else if (settings->auto_dns_mode == Dns::kModeAuto && settings->auto_dns_server == active->id && settings->auto_dns_latency >= 0) {
        const auto checked = QDateTime::fromSecsSinceEpoch(settings->auto_dns_checked.toLongLong());
        lines << tr("%1, picked automatically (%2 ms, measured %3).")
                     .arg(active->name)
                     .arg(settings->auto_dns_latency)
                     .arg(QLocale().toString(checked, QLocale::ShortFormat));
    } else if (settings->auto_dns_mode == Dns::kModeAuto) {
        lines << tr("%1 until a test picks the fastest server.").arg(active->name);
    } else {
        lines << tr("%1, chosen by hand.").arg(active->name);
    }
    if (auto *service = Engine::host()->autoDns(); service != nullptr && service->systemDnsActive()) {
        const auto original = service->originalServers();
#ifdef Q_OS_WIN
        lines << tr("Windows' DNS is changed by F-Dpi while DPI Bypass runs (it was %1).")
                     .arg(original.isEmpty() ? tr("unknown") : original.join(QStringLiteral(", ")));
#else
        lines << tr("The system DNS is changed by F-Dpi while DPI Bypass runs (it was %1).")
                     .arg(original.isEmpty() ? tr("unknown") : original.join(QStringLiteral(", ")));
#endif
    }
    m_status->setText(lines.join('\n'));
}

void DialogAutoDns::setRunning(bool running) {
    m_test->setEnabled(!running);
    m_domains->setEnabled(!running);
    m_progress->setRange(0, running ? 0 : 1);
}

void DialogAutoDns::startTest() {
    Engine::DnsBenchmarkOptions options;
    for (const auto &part : m_domains->text().split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts)) {
        const auto domain = Configs::dpi::NormalizeHost(part);
        if (!domain.isEmpty() && !options.testDomains.contains(domain)) options.testDomains << domain;
    }
    if (auto *service = Engine::host()->autoDns(); service != nullptr && service->systemDnsActive()) options.systemServers = service->originalServers();
    m_servers->setRowCount(0);
    m_poison->setRowCount(0);
    m_summary->clear();
    if (!m_benchmark->start(options)) return;
    setRunning(true);
}

void DialogAutoDns::onFinished(const Engine::DnsBenchmarkResult &result) {
    setRunning(false);
    if (m_closeWhenDone) {
        QDialog::reject();
        return;
    }
    for (const auto &check : result.domains) {
        const int row = m_poison->rowCount();
        m_poison->insertRow(row);
        m_poison->setItem(row, 0, autoDnsItem(check.domain));
        m_poison->setItem(row, 1, autoDnsItem(autoDnsVerdictCell(check.system, check.systemVerdict), autoDnsVerdictColor(check.systemVerdict)));
        m_poison->setItem(row, 2, autoDnsItem(autoDnsVerdictCell(check.plain, check.plainVerdict), autoDnsVerdictColor(check.plainVerdict)));
        m_poison->setItem(row, 3, autoDnsItem(check.secure.isEmpty() ? tr("no answer") : check.secure.mid(0, 2).join(QStringLiteral(", "))));
    }
    if (!result.systemServer.isEmpty()) {
        m_poison->horizontalHeaderItem(1)->setText(tr("System DNS (%1)").arg(result.systemServer));
    }

    QStringList lines;
    if (!result.error.isEmpty()) lines << result.error;
    if (const auto *best = Dns::FindProvider(result.bestId)) {
        lines << tr("Fastest: %1 (%2 ms).").arg(best->name).arg(result.bestMs);
        if (auto *service = Engine::host()->autoDns()) service->adopt(result);
    }
    if (result.systemPoisoned) {
        lines << tr("This network's DNS poisons or blocks some of these sites: use Automatic DNS.");
    } else if (!result.domains.isEmpty() && !result.systemServer.isEmpty()) {
        lines << tr("The system DNS answered these sites correctly.");
    }
    if (result.plainIntercepted) lines << tr("Plain DNS to public servers is tampered with on the way too: only encrypted DNS is safe here.");
    if (result.cancelled) lines << tr("Stopped.");
    m_summary->setText(lines.join('\n'));
    refreshStatus();
}

void DialogAutoDns::accept() {
    auto &settings = Configs::dataManager->settingsRepo;
    const auto mode = m_mode->currentData().toString();
    const bool changed = settings->auto_dns_mode != mode || settings->auto_dns_proxy != m_proxy->isChecked() ||
                         settings->auto_dns_system != m_system->isChecked();
    settings->auto_dns_mode = mode;
    settings->auto_dns_proxy = m_proxy->isChecked();
    settings->auto_dns_system = m_system->isChecked();
    settings->Save();
    if (changed) {
        if (auto *service = Engine::host()->autoDns()) service->settingsChanged();
    }
    if (m_benchmark->running()) {
        m_closeWhenDone = true;
        m_benchmark->cancel();
        return;
    }
    QDialog::accept();
}

void DialogAutoDns::closeEvent(QCloseEvent *event) {
    if (m_benchmark->running()) {
        m_closeWhenDone = true;
        m_benchmark->cancel();
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void DialogAutoDns::reject() {
    if (m_benchmark->running()) {
        m_closeWhenDone = true;
        m_benchmark->cancel();
        return;
    }
    QDialog::reject();
}
