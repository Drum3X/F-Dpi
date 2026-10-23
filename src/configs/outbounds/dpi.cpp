#include "include/configs/outbounds/dpi.h"

#include <algorithm>

#include <QCoreApplication>
#include <QJsonArray>
#include <QHostAddress>
#include <QRegularExpression>
#include <QUrl>

#include "include/dpi/DpiPresets.h"

namespace Configs {
    namespace {
        QStringList dpiStringList(const QJsonValue &value) {
            QStringList out;
            for (const auto &item : value.toArray()) {
                const auto text = item.toString().trimmed();
                if (!text.isEmpty()) out << text;
            }
            return out;
        }

        // Options F-Dpi sets itself or that make winws write files: a strategy carrying them could overwrite anything as admin.
        const QStringList &dpiManagedOptions() {
            static const QStringList options = {
                QStringLiteral("--hostlist-auto"), QStringLiteral("--wf-save"), QStringLiteral("--pidfile"),
                QStringLiteral("--daemon"), QStringLiteral("--debug=@"), QStringLiteral("--debug=syslog"),
            };
            return options;
        }
    } // namespace

    dpi::dpi() {
        ApplyPreset(Dpi::kDefaultPreset);
    }

    bool dpi::ApplyPreset(const QString &id) {
        const auto *p = Dpi::FindPreset(id);
        if (p == nullptr) return false;
        preset = p->id;
        strategy = p->strategy;
        wfTcp = p->wfTcp;
        wfUdp = p->wfUdp;
        return true;
    }

    bool dpi::ParseFromJson(const QJsonObject &object) {
        if (object.isEmpty()) return false;
        if (object.contains("name")) name = object["name"].toString();
        if (object.contains("preset")) preset = object["preset"].toString();
        if (object.contains("strategy")) strategy = object["strategy"].toString();
        if (object.contains("wf_tcp")) wfTcp = object["wf_tcp"].toString();
        if (object.contains("wf_udp")) wfUdp = object["wf_udp"].toString();
        if (object.contains("mode")) mode = object["mode"].toString();
        if (object.contains("hostlist")) hostlist = dpiStringList(object["hostlist"]);
        if (object.contains("exclude")) exclude = dpiStringList(object["exclude"]);
        return true;
    }

    QJsonObject dpi::ExportToJson() {
        QJsonObject object;
        object["name"] = name;
        object["type"] = "dpi";
        object["preset"] = preset;
        object["strategy"] = strategy;
        object["wf_tcp"] = wfTcp;
        object["wf_udp"] = wfUdp;
        object["mode"] = mode;
        object["hostlist"] = QJsonArray::fromStringList(hostlist);
        object["exclude"] = QJsonArray::fromStringList(exclude);
        return object;
    }

    QString dpi::DisplayAddress() {
        QString what = QCoreApplication::translate("Dpi", "Custom strategy");
        if (const auto *p = Dpi::FindPreset(preset)) what = Dpi::PresetName(*p);
        if (mode == ModeHostlist) return QCoreApplication::translate("Dpi", "%1, listed sites only").arg(what);
        if (mode == ModeAutoHostlist) return QCoreApplication::translate("Dpi", "%1, automatic list").arg(what);
        return what;
    }

    void dpi::Validate(Validation::Issues &issues) {
        outbound::Validate(issues);
        const auto report = [&issues](const QString &field, const QString &message, const QString &value = {}) {
            if (!message.isEmpty()) issues << Validation::Issue{Validation::Severity::Error, field, message, value};
        };
        report("strategy", Checks::DpiStrategy(strategy));
        report("wf_tcp", Checks::DpiPorts(wfTcp), wfTcp);
        report("wf_udp", Checks::DpiPorts(wfUdp), wfUdp);
        if (wfTcp.trimmed().isEmpty() && wfUdp.trimmed().isEmpty()) {
            report("wf_tcp", QCoreApplication::translate("Dpi", "Enter TCP or UDP ports; with neither, no traffic is captured."));
        }
        if (mode != ModeAll && mode != ModeHostlist && mode != ModeAutoHostlist) {
            report("mode", QCoreApplication::translate("Dpi", "Unknown site selection mode."), mode);
        }
        if (mode == ModeHostlist && hostlist.isEmpty()) {
            report("hostlist", QCoreApplication::translate("Dpi", "Add at least one site, or apply the bypass to all sites."));
        }
    }

    BuildResult dpi::Build() {
        return {{}, QCoreApplication::translate("Dpi", "A DPI Bypass profile is not a proxy, so it cannot be used in a chain, a route or a test.")};
    }

    QString dpi::NormalizeHost(const QString &line) {
        QString host = line.section('#', 0, 0).trimmed();
        if (host.contains(QStringLiteral("://"))) host = QUrl(host).host();
        host = host.section('/', 0, 0);
        if (host.startsWith(QStringLiteral("*."))) host = host.mid(2);
        if (host.startsWith('.')) host = host.mid(1);
        if (host.isEmpty()) return {};
        const auto ace = QString::fromLatin1(QUrl::toAce(host));
        return (ace.isEmpty() ? host : ace).toLower();
    }

    QStringList dpi::NormalizeHosts(const QStringList &lines) {
        QStringList out;
        for (const auto &line : lines) {
            const auto host = NormalizeHost(line);
            if (!host.isEmpty() && !out.contains(host)) out << host;
        }
        return out;
    }

    QStringList dpi::StrategyTokens(QString *error) const {
        QStringList tokens;
        QString current;
        bool inToken = false;
        QChar quote;
        bool comment = false;
        for (const QChar c : strategy) {
            if (comment) {
                if (c == '\n') comment = false;
                continue;
            }
            if (!quote.isNull()) {
                if (c == quote) quote = QChar();
                else current += c;
                continue;
            }
            if (c.isSpace()) {
                if (inToken) tokens << std::exchange(current, {});
                inToken = false;
            } else if (c == '#' && !inToken) {
                comment = true;
            } else if (c == '"' || c == '\'') {
                quote = c;
                inToken = true;
            } else {
                current += c;
                inToken = true;
            }
        }
        if (!quote.isNull() && error) {
            *error = QCoreApplication::translate("Validation", "A quote is not closed; end it with the same quote mark.");
        }
        if (inToken) tokens << current;
        return tokens;
    }

    namespace Checks {
        QString DpiPorts(const QString &text) {
            static const QRegularExpression item(QStringLiteral("^(\\d{1,5})(-(\\d{1,5}))?$"));
            const auto trimmed = text.trimmed();
            if (trimmed.isEmpty()) return {};
            for (const auto &part : trimmed.split(',')) {
                const auto m = item.match(part.trimmed());
                const int from = m.captured(1).toInt();
                const int to = m.hasCaptured(3) ? m.captured(3).toInt() : from;
                if (!m.hasMatch() || from < 1 || to > 65535 || to < from) {
                    return QCoreApplication::translate("Dpi", "Write ports or ranges separated by commas, e.g. 80,443 or 50000-50100.");
                }
            }
            return {};
        }

        QString DpiHost(const QString &line) {
            if (line.section('#', 0, 0).trimmed().isEmpty()) return {};
            const auto host = dpi::NormalizeHost(line);
            if (QHostAddress(host).protocol() != QAbstractSocket::UnknownNetworkLayerProtocol) {
                return QCoreApplication::translate("Dpi", "Enter a domain name; site lists do not match IP addresses.");
            }
            return Validation::Host(host);
        }

        QString DpiStrategy(const QString &text) {
            dpi probe;
            probe.strategy = text;
            QString error;
            const auto tokens = probe.StrategyTokens(&error);
            if (!error.isEmpty()) return error;
            if (tokens.isEmpty()) return QCoreApplication::translate("Dpi", "Enter a strategy or choose a preset.");
            for (const auto &token : tokens) {
                for (const auto &option : dpiManagedOptions()) {
                    if (token.startsWith(option)) {
                        return QCoreApplication::translate("Dpi", "%1 is not allowed here: F-Dpi manages site lists and files itself.").arg(token.section('=', 0, 0));
                    }
                }
            }
            if (!std::any_of(tokens.begin(), tokens.end(), [](const QString &t) {
                    return t.startsWith(QStringLiteral("--dpi-desync"));
                })) {
                return QCoreApplication::translate("Dpi", "The strategy sets no --dpi-desync method, so winws would change nothing.");
            }
            return {};
        }
    } // namespace Checks
} // namespace Configs
