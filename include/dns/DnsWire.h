#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QString>

// F-Dpi: the DNS wire format (RFC 1035), just enough to ask for A/AAAA records over DoH (RFC 8484) or plain UDP.
namespace Dns {
    enum RecordType : quint16 { A = 1, AAAA = 28 };

    constexpr int kRcodeNoError = 0;
    constexpr int kRcodeNxDomain = 3;

    // One question with recursion desired. The domain may be Unicode; it is sent as punycode.
    QByteArray BuildQuery(const QString &domain, quint16 type, quint16 id = 0);

    struct Answer {
        quint16 id = 0;
        // -1 when the message could not be parsed.
        int rcode = -1;
        // A and AAAA records only, in answer order; CNAMEs are followed by the server and skipped here.
        QList<QHostAddress> addresses;

        [[nodiscard]] bool resolved() const { return rcode == kRcodeNoError && !addresses.isEmpty(); }
    };

    // False (with *error) when data is not a well-formed DNS response.
    bool ParseResponse(const QByteArray &data, Answer *out, QString *error = nullptr);
} // namespace Dns
