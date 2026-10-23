#include "include/ui/profile/edit_dpi.h"

#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>

#include "include/dpi/DpiPresets.h"
#include "include/ui/utils/ValidationForm.h"

EditDpi::EditDpi(QWidget *parent) : QWidget(parent) {
    auto *layout = new QFormLayout(this);

    m_preset = new QComboBox(this);
    for (const auto &preset : Dpi::Presets()) m_preset->addItem(Dpi::PresetName(preset), preset.id);
    m_preset->addItem(tr("Custom"), QString());
    layout->addRow(tr("Preset"), m_preset);

    m_wfTcp = new QLineEdit(this);
    m_wfTcp->setPlaceholderText(QStringLiteral("80,443"));
    m_wfTcp->setToolTip(tr("TCP ports winws captures (--wf-tcp). Separate ports and ranges with commas."));
    layout->addRow(tr("TCP ports"), m_wfTcp);

    m_wfUdp = new QLineEdit(this);
    m_wfUdp->setPlaceholderText(QStringLiteral("443"));
    m_wfUdp->setToolTip(tr("UDP ports winws captures (--wf-udp), e.g. 443 for QUIC. Leave empty to leave UDP alone."));
    layout->addRow(tr("UDP ports"), m_wfUdp);

    m_strategy = new QPlainTextEdit(this);
    m_strategy->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_strategy->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_strategy->setMinimumHeight(150);
    m_strategy->setToolTip(tr("winws options. Separate profiles with --new; # starts a comment; %ZAPRET% is the winws folder."));
    layout->addRow(tr("Strategy"), m_strategy);

    auto *hint = new QLabel(tr("Proxy and DPI Bypass never run together: starting this profile stops the running proxy."), this);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    layout->addRow(hint);

    connect(m_preset, &QComboBox::currentIndexChanged, this, [this](int index) { applyPreset(index); });
    connect(m_wfTcp, &QLineEdit::textEdited, this, [this] { markCustom(); });
    connect(m_wfUdp, &QLineEdit::textEdited, this, [this] { markCustom(); });
    connect(m_strategy, &QPlainTextEdit::textChanged, this, [this] { markCustom(); });
}

void EditDpi::onStart(std::shared_ptr<Configs::Profile> _ent) {
    ent = _ent;
    const auto *config = ent->Dpi();
    m_loading = true;
    m_wfTcp->setText(config->wfTcp);
    m_wfUdp->setText(config->wfUdp);
    m_strategy->setPlainText(config->strategy);
    const int index = m_preset->findData(config->preset);
    m_preset->setCurrentIndex(index >= 0 ? index : m_preset->count() - 1);
    m_loading = false;
}

bool EditDpi::onEnd() {
    auto *config = ent->Dpi();
    config->preset = m_preset->currentData().toString();
    config->wfTcp = m_wfTcp->text().trimmed();
    config->wfUdp = m_wfUdp->text().trimmed();
    config->strategy = m_strategy->toPlainText();
    return true;
}

void EditDpi::bindValidation(Validation::Form &form) {
    form.bind(m_wfTcp, Configs::Checks::DpiPorts, this);
    form.bind(m_wfUdp, Configs::Checks::DpiPorts, this);
    form.bind(m_strategy, Configs::Checks::DpiStrategy, this);
    form.addRule({m_wfTcp, m_wfUdp}, [this] {
        if (!m_wfTcp->text().trimmed().isEmpty() || !m_wfUdp->text().trimmed().isEmpty()) return QString();
        return tr("Enter TCP or UDP ports; with neither, no traffic is captured.");
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
