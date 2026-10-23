#include "include/dns/DnsWire.h"

#include <QUrl>

namespace Dns {
    namespace {
        quint16 dnsWireU16(const QByteArray &data, int pos) {
            return static_cast<quint16>((static_cast<quint8>(data[pos]) << 8) | static_cast<quint8>(data[pos + 1]));
        }

        // Moves pos past a (possibly compressed) name; false when it runs off the message.
        bool dnsWireSkipName(const QByteArray &data, int *pos) {
            for (int labels = 0; labels < 128; ++labels) {
                if (*pos >= data.size()) return false;
                const auto length = static_cast<quint8>(data[*pos]);
                if ((length & 0xC0) == 0xC0) {
                    *pos += 2;
                    return *pos <= data.size();
                }
                if (length == 0) {
                    *pos += 1;
                    return true;
                }
                *pos += 1 + length;
            }
            return false;
        }
    } // namespace

    QByteArray BuildQuery(const QString &domain, quint16 type, quint16 id) {
        QByteArray query;
        query.reserve(32 + domain.size());
        query.append(static_cast<char>(id >> 8)).append(static_cast<char>(id & 0xFF));
        // RD; one question, no other records.
        query.append('\x01').append('\x00');
        query.append('\x00').append('\x01');
        query.append(6, '\x00');
        const auto ace = QUrl::toAce(domain.trimmed().toLower());
        for (const auto &label : ace.split('.')) {
            if (label.isEmpty()) continue;
            query.append(static_cast<char>(qMin<qsizetype>(label.size(), 63))).append(label.left(63));
        }
        query.append('\x00');
        query.append(static_cast<char>(type >> 8)).append(static_cast<char>(type & 0xFF));
        query.append('\x00').append('\x01');
        return query;
    }

    bool ParseResponse(const QByteArray &data, Answer *out, QString *error) {
        *out = {};
        auto fail = [error](const QString &why) {
            if (error) *error = why;
            return false;
        };
        if (data.size() < 12) return fail(QStringLiteral("short DNS message"));
        out->id = dnsWireU16(data, 0);
        if ((static_cast<quint8>(data[2]) & 0x80) == 0) return fail(QStringLiteral("not a DNS response"));
        const int rcode = static_cast<quint8>(data[3]) & 0x0F;
        const int questions = dnsWireU16(data, 4);
        const int answers = dnsWireU16(data, 6);
        int pos = 12;
        for (int i = 0; i < questions; ++i) {
            if (!dnsWireSkipName(data, &pos)) return fail(QStringLiteral("truncated question"));
            pos += 4;
        }
        for (int i = 0; i < answers; ++i) {
            if (!dnsWireSkipName(data, &pos) || pos + 10 > data.size()) return fail(QStringLiteral("truncated answer"));
            const quint16 type = dnsWireU16(data, pos);
            const int length = dnsWireU16(data, pos + 8);
            pos += 10;
            if (pos + length > data.size()) return fail(QStringLiteral("truncated record"));
            if (type == A && length == 4) {
                const quint32 ip = (static_cast<quint32>(static_cast<quint8>(data[pos])) << 24) |
                                   (static_cast<quint32>(static_cast<quint8>(data[pos + 1])) << 16) |
                                   (static_cast<quint32>(static_cast<quint8>(data[pos + 2])) << 8) |
                                   static_cast<quint32>(static_cast<quint8>(data[pos + 3]));
                out->addresses << QHostAddress(ip);
            } else if (type == AAAA && length == 16) {
                out->addresses << QHostAddress(reinterpret_cast<const quint8 *>(data.constData() + pos));
            }
            pos += length;
        }
        out->rcode = rcode;
        return true;
    }
} // namespace Dns
