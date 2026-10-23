#include "include/ui/app/dialog_about.h"

#include <QDesktopServices>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "NkrVersion.h"
#include "include/global/Branding.hpp"
#include "include/global/Configs.hpp"

DialogAbout::DialogAbout(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("About %1").arg(software_name));

    auto *icon = new QLabel(this);
    icon->setPixmap(QIcon(QStringLiteral(":/Throne/Throne.png")).pixmap(64));
    icon->setAlignment(Qt::AlignTop);

    auto *title = new QLabel(software_name, this);
    auto titleFont = title->font();
    titleFont.setBold(true);
    if (titleFont.pointSizeF() > 0) {
        titleFont.setPointSizeF(titleFont.pointSizeF() * 1.5);
    } else {
        titleFont.setPixelSize(titleFont.pixelSize() * 3 / 2);
    }
    title->setFont(titleFont);

    const auto valueLabel = [this](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };
    auto *singBoxVersion = valueLabel(QStringLiteral("…"));
    auto *xrayVersion = valueLabel(QStringLiteral("…"));
    auto *goVersion = valueLabel(QStringLiteral("…"));

    auto *versions = new QFormLayout;
    versions->addRow(tr("Version:"), valueLabel(QStringLiteral(NKR_VERSION)));
    versions->addRow(QStringLiteral("sing-box:"), singBoxVersion);
    versions->addRow(QStringLiteral("Xray-core:"), xrayVersion);
    versions->addRow(QStringLiteral("Qt:"), valueLabel(QString::fromLatin1(qVersion())));
    versions->addRow(QStringLiteral("Go:"), goVersion);

    auto *description = new QLabel(tr("Desktop utility for getting around censorship and DPI blocking: a sing-box proxy and a zapret (winws) DPI "
                                      "bypass, never running at the same time.") +
                                       "\n\n" + tr("Based on %1 (GPL-3.0).").arg(QString::fromLatin1(Branding::UpstreamName)),
                                   this);
    description->setWordWrap(true);
    description->setMinimumWidth(fontMetrics().averageCharWidth() * 50);

    auto *text = new QVBoxLayout;
    text->addWidget(title);
    text->addWidget(description);
    text->addSpacing(fontMetrics().height() / 2);
    text->addLayout(versions);

    auto *top = new QHBoxLayout;
    top->setSpacing(fontMetrics().height());
    top->addWidget(icon);
    top->addLayout(text, 1);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    if (const auto homepage = QString::fromLatin1(Branding::HomepageUrl); !homepage.isEmpty()) {
        auto *github = new QPushButton(QStringLiteral("GitHub"), this);
        github->setAutoDefault(false);
        connect(github, &QPushButton::clicked, this, [homepage] { QDesktopServices::openUrl(QUrl(homepage)); });
        buttons->addWidget(github);
    }
    auto *upstream = new QPushButton(QString::fromLatin1(Branding::UpstreamName), this);
    upstream->setAutoDefault(false);
    connect(upstream, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(Branding::UpstreamUrl))); });
    buttons->addWidget(upstream);
    buttons->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addSpacing(fontMetrics().height() / 2);
    layout->addLayout(buttons);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    // A core predating --version still prints its sing-box and Xray lines before failing for want of a GUI socket.
    auto *core = new QProcess(this);
    core->setStandardInputFile(QProcess::nullDevice());
    const auto showCoreVersions = [=, this](const QByteArray &output) {
        QHash<QString, QString> found;
        for (const auto &line : QString::fromUtf8(output).split(QLatin1Char('\n'))) {
            const auto colon = line.indexOf(QLatin1String(": "));
            if (colon > 0) found.insert(line.left(colon), line.mid(colon + 2).trimmed());
        }
        const auto unknown = tr("Unknown");
        singBoxVersion->setText(found.value(QStringLiteral("sing-box"), unknown));
        xrayVersion->setText(found.value(QStringLiteral("Xray-core"), unknown));
        goVersion->setText(found.value(QStringLiteral("Go"), unknown));
    };
    connect(core, &QProcess::finished, this, [=] { showCoreVersions(core->readAllStandardOutput()); });
    connect(core, &QProcess::errorOccurred, this, [=](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) showCoreVersions({});
    });
    QTimer::singleShot(3000, core, [core] { core->kill(); });
    core->start(Configs::FindCoreRealPath(), {QStringLiteral("--version")});
}
