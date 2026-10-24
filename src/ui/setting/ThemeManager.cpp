#include <QStyle>
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QPalette>
#include <QColor>
#include <QMap>

#ifdef Q_OS_MACOS
#include <QEvent>
#include <QFormLayout>
#include <QWidget>
#endif

#include <algorithm>
#include <cmath>

#include "include/ui/setting/ThemeManager.hpp"

#include <QGlobalStatic>

Q_GLOBAL_STATIC(ThemeManager, themeManagerInstance)

ThemeManager *themeManager() {
    return themeManagerInstance();
}

extern QString ReadFileText(const QString &path);

struct ThemeColors {
    QColor window, windowText;
    QColor base, alternateBase;
    QColor text;
    QColor button, buttonText;
    QColor brightText;
    QColor highlight, highlightedText;
    QColor link;            // paints the active/running config row
    QColor tooltipBase, tooltipText;
    QColor placeholder;
    QColor disabledText;
};

static QPalette buildThemePalette(const ThemeColors &c) {
    QPalette p;

    const auto setAll = [&](QPalette::ColorRole role, const QColor &col) {
        p.setColor(QPalette::Active, role, col);
        p.setColor(QPalette::Inactive, role, col);
        p.setColor(QPalette::Disabled, role, col);
    };

    setAll(QPalette::Window,          c.window);
    setAll(QPalette::WindowText,      c.windowText);
    setAll(QPalette::Base,            c.base);
    setAll(QPalette::AlternateBase,   c.alternateBase);
    setAll(QPalette::Text,            c.text);
    setAll(QPalette::Button,          c.button);
    setAll(QPalette::ButtonText,      c.buttonText);
    setAll(QPalette::BrightText,      c.brightText);
    setAll(QPalette::ToolTipBase,     c.tooltipBase);
    setAll(QPalette::ToolTipText,     c.tooltipText);
    setAll(QPalette::Highlight,       c.highlight);
    setAll(QPalette::HighlightedText, c.highlightedText);
    setAll(QPalette::Link,            c.link);
    setAll(QPalette::LinkVisited,     c.link);
    setAll(QPalette::PlaceholderText, c.placeholder);

    // Frames and bevels the stylesheet doesn't cover fall back to Qt's light defaults otherwise.
    setAll(QPalette::Light,    c.button.lighter(130));
    setAll(QPalette::Midlight, c.button.lighter(115));
    setAll(QPalette::Mid,      c.button.darker(130));
    setAll(QPalette::Dark,     c.button.darker(160));
    setAll(QPalette::Shadow,   c.window.darker(180));

    // Must follow setAll(), which wrote the Disabled group too.
    p.setColor(QPalette::Disabled, QPalette::WindowText,      c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text,            c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText,      c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::Link,            c.disabledText);

    return p;
}

// Lazy: a QPalette must not be constructed before QApplication exists. The keys also define "custom theme".
static const QMap<QString, QPalette> &customThemePalettes() {
    static const QMap<QString, QPalette> palettes = [] {
        QMap<QString, QPalette> m;

        m["flatgray"] = buildThemePalette({
            .window = "#FFFFFF", .windowText = "#57595B",
            .base = "#FFFFFF", .alternateBase = "#F6F6F6",
            .text = "#57595B",
            .button = "#F2F2F2", .buttonText = "#57595B",
            .brightText = "#FFFFFF",
            .highlight = "#D6D6D6", .highlightedText = "#2D2F31",
            .link = "#2A6CB0",
            .tooltipBase = "#FFFFFF", .tooltipText = "#57595B",
            .placeholder = "#9AA0A6", .disabledText = "#B0B0B0",
        });

        m["lightblue"] = buildThemePalette({
            .window = "#EAF7FF", .windowText = "#386487",
            .base = "#FFFFFF", .alternateBase = "#DAEFFF",
            .text = "#386487",
            .button = "#DEF0FE", .buttonText = "#386487",
            .brightText = "#FFFFFF",
            .highlight = "#C0DCF2", .highlightedText = "#1B3B57",
            .link = "#1D6FB8",
            .tooltipBase = "#EAF7FF", .tooltipText = "#386487",
            .placeholder = "#7F9DB5", .disabledText = "#A6BCCE",
        });

        m["softpink"] = buildThemePalette({
            .window = "#FFF0FB", .windowText = "#883983",
            .base = "#FFFFFF", .alternateBase = "#FBDDF5",
            .text = "#883983",
            .button = "#FCE1F6", .buttonText = "#883983",
            .brightText = "#FFFFFF",
            .highlight = "#F1C1E7", .highlightedText = "#5A2456",
            .link = "#B92BA6",
            .tooltipBase = "#FFF0FB", .tooltipText = "#883983",
            .placeholder = "#C08BBA", .disabledText = "#CBA6C6",
        });

        m["blacksoft"] = buildThemePalette({
            .window = "#444444", .windowText = "#DCDCDC",
            .base = "#444444", .alternateBase = "#525252",
            .text = "#DCDCDC",
            .button = "#484848", .buttonText = "#DCDCDC",
            .brightText = "#FFFFFF",
            .highlight = "#646464", .highlightedText = "#FFFFFF",
            .link = "#5AB0FF",
            .tooltipBase = "#484848", .tooltipText = "#DCDCDC",
            .placeholder = "#9A9A9A", .disabledText = "#808080",
        });

        // Mirrors the bundled darkstyle.qss.
        m["qdarkstyle"] = buildThemePalette({
            .window = "#19232D", .windowText = "#DFE1E2",
            .base = "#19232D", .alternateBase = "#37414F",
            .text = "#DFE1E2",
            .button = "#455364", .buttonText = "#DFE1E2",
            .brightText = "#FFFFFF",
            .highlight = "#346792", .highlightedText = "#DFE1E2",
            .link = "#6FC0FF",
            .tooltipBase = "#346792", .tooltipText = "#DFE1E2",
            .placeholder = "#9DA9B5", .disabledText = "#788D9C",
        });

        return m;
    }();
    return palettes;
}

static double relLuminance(const QColor &c) {
    const auto channel = [](double v) {
        v /= 255.0;
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) + 0.0722 * channel(c.blue());
}

static double contrastRatio(const QColor &a, const QColor &b) {
    const double la = relLuminance(a), lb = relLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

static QColor blendToward(const QColor &from, const QColor &to, double keep) {
    return QColor::fromRgbF(from.redF()   * keep + to.redF()   * (1 - keep),
                            from.greenF() * keep + to.greenF() * (1 - keep),
                            from.blueF()  * keep + to.blueF()  * (1 - keep));
}

// Walks HSL lightness away from `surface` until the ratio is met; hue and saturation survive.
static QColor separate(QColor c, const QColor &surface, double target) {
    const int dir = relLuminance(surface) > 0.5 ? -1 : 1;
    for (int i = 0; i < 24 && contrastRatio(c, surface) < target; ++i) {
        int h, s, l, a;
        c.getHsl(&h, &s, &l, &a);
        const int next = qBound(0, l + dir * 10, 255);
        if (next == l) break;
        c.setHsl(h < 0 ? 0 : h, s, next, a); // getHsl reports -1 for achromatic; s is 0 there anyway
    }
    return c;
}

static QColor paletteAccent(const QPalette &pal) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    return pal.color(QPalette::Active, QPalette::Accent);
#else
    return pal.color(QPalette::Active, QPalette::Highlight);
#endif
}

static ThemeTokens resolveTokens(const QPalette &pal) {
    ThemeTokens t;
    t.surface   = pal.color(QPalette::Active, QPalette::Window);
    t.onSurface = pal.color(QPalette::Active, QPalette::WindowText);

    t.accent  = separate(paletteAccent(pal), t.surface, 3.0);
    t.muted   = separate(blendToward(t.onSurface, t.surface, 0.62), t.surface, 4.0);
    t.tag     = separate(QColor(0xFB, 0x72, 0x99), t.surface, 4.0);
    t.danger  = separate(QColor(0xC6, 0x28, 0x28), t.surface, 4.5);
    t.success = separate(QColor(0x2E, 0x7D, 0x32), t.surface, 4.5);
    t.info    = separate(QColor(0x32, 0x99, 0xFF), t.surface, 4.0);
    const std::array<QColor, 5> series{QColor(0x2A, 0x78, 0xD6), QColor(0xEB, 0x68, 0x34), QColor(0x1B, 0xAF, 0x7A),
                                       QColor(0xE0, 0x69, 0x9A), QColor(0x5B, 0x4B, 0xC4)};
    for (size_t i = 0; i < series.size(); ++i) t.chartSeries[i] = separate(series[i], t.surface, 3.0);
    t.chartContext = separate(QColor(0x7D, 0x87, 0x96), t.surface, 3.0);
    return t;
}

// Literal hex only, so no rule here can resolve against the wrong palette or be served stale
// from QStyleSheetStyle's render-rule cache.
static QString overlayStyleSheet(const ThemeTokens &t) {
    const auto hex = [](const QColor &c) { return c.name(QColor::HexRgb); };
    QString sheet = QStringLiteral(
        "*[colorRole=\"muted\"] { color: %1; }\n"
        "*[colorRole=\"tag\"] { color: %2; }\n"
        "*[colorRole=\"danger\"] { color: %3; }\n"
        "*[colorRole=\"success\"] { color: %4; }\n"
        // Colour only: any border rule makes QStyleSheetStyle replace the native frame.
        "QLineEdit[invalid=\"true\"], QLineEdit[invalid=\"true\"]:focus, QLineEdit[invalid=\"true\"]:hover,\n"
        "QPlainTextEdit[invalid=\"true\"], QTextEdit[invalid=\"true\"],\n"
        "QAbstractSpinBox[invalid=\"true\"], QAbstractSpinBox[invalid=\"true\"]:focus, QAbstractSpinBox[invalid=\"true\"]:hover { color: %3; }\n"
        // A menu-style popup paints its items under the combo's rule, as :active and never :editable or :read-only.
        "QComboBox[invalid=\"true\"]:editable, QComboBox[invalid=\"true\"]:editable:focus, QComboBox[invalid=\"true\"]:editable:hover,\n"
        "QComboBox[invalid=\"true\"]:read-only, QComboBox[invalid=\"true\"]:read-only:focus, QComboBox[invalid=\"true\"]:read-only:hover,\n"
        // Qt polishes a non-editable combo's inactive palette without :read-only.
        "QComboBox[invalid=\"true\"]:!editable:!active { color: %3; }\n"
    ).arg(hex(t.muted), hex(t.tag), hex(t.danger), hex(t.success));
#ifdef Q_OS_MACOS
    // QMacStyle centres non-document tabs and elides them instead of scrolling.
    sheet += QStringLiteral(
        "QTabWidget::tab-bar { alignment: left; }\n"
        "QTabBar { tabbar-prefer-no-arrows: 0; tabbar-elide-mode: %1; }\n"
    ).arg(int(Qt::ElideNone));
#endif
    return sheet;
}

static QColor paneBorder(const ThemeTokens &t) {
    return separate(blendToward(t.onSurface, t.surface, 0.32), t.surface, 1.9);
}

// F-Dpi: joined tabs. They share their borders; the selected one is lighter and opens into the pane, the others sit darker
// with a muted label.
static QString windows11TabStyleSheet(const QPalette &pal, const ThemeTokens &t) {
    const auto hex = [](const QColor &c) { return c.name(QColor::HexRgb); };
    const QColor border = paneBorder(t);
    const QColor pane = pal.color(QPalette::Active, QPalette::Base);
    const bool light = relLuminance(pane) > 0.5;
    const QColor selected = light ? pane : blendToward(pane, QColor(0xFF, 0xFF, 0xFF), 0.90);
    const QColor unselected = blendToward(pane, QColor(0, 0, 0), light ? 0.93 : 0.80);
    const QColor hover = blendToward(selected, unselected, 0.5);
    return QStringLiteral(
        "QTabWidget::pane { top: -1px; border: 1px solid %1; background: %7; }\n"
        "#profilesTableView, #masterLogBrowser, #connections { border: none; }\n"
        "QTabBar { background: transparent; qproperty-drawBase: 0; }\n"
        "QTabBar::tab {\n"
        "    background: %5;\n"
        "    color: %6;\n"
        "    border: 1px solid %1;\n"
        "    border-top-left-radius: 3px;\n"
        "    border-top-right-radius: 3px;\n"
        "    padding: 3px 10px;\n"
        "    margin-right: -1px;\n"
        "}\n"
        "QTabBar::tab:hover:!selected { background: %3; color: %2; }\n"
        "QTabBar::tab:selected { background: %4; color: %2; border-bottom-color: %4; }\n"
        "QTabBar::tab:disabled { color: %6; }\n"
    ).arg(hex(border), hex(t.onSurface), hex(hover), hex(selected), hex(unselected), hex(t.muted), hex(pane));
}

// F-Dpi: a grey accent in place of the Windows accent colour (check boxes, default button, focus, selection).
static void neutralAccent() {
    QPalette pal = qApp->palette();
    const bool light = relLuminance(pal.color(QPalette::Active, QPalette::Window)) > 0.5;
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    pal.setColor(QPalette::Accent, light ? QColor(0x5A, 0x5A, 0x5A) : QColor(0xC8, 0xC8, 0xC8));
#endif
    pal.setColor(QPalette::Highlight, light ? QColor(0xCC, 0xCC, 0xCC) : QColor(0x55, 0x55, 0x55));
    pal.setColor(QPalette::HighlightedText, light ? QColor(0x1A, 0x1A, 0x1A) : QColor(0xFF, 0xFF, 0xFF));
    qApp->setPalette(pal);
}

// QMacStyle cuts its pane border where its own centred tab bar would be (clipTabBarFrame), so the sheet draws the pane.
static QString macPaneStyleSheet(const ThemeTokens &t) {
    return QStringLiteral(
        "QTabWidget::pane { border: 1px solid %1; border-radius: 6px; }\n"
        "#profilesTableView, #masterLogBrowser, #connections { border: none; }\n"
    ).arg(paneBorder(t).name(QColor::HexRgb));
}

#ifdef Q_OS_MACOS
// Rewrites only values equal to QMacStyle's form defaults: centred form, right-aligned labels, fields at size hint.
class UniformFormLayouts : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Polish && watched->isWidgetType()) {
            if (auto *layout = static_cast<QWidget *>(watched)->layout()) {
                normalize(qobject_cast<QFormLayout *>(layout));
                for (auto *form : layout->findChildren<QFormLayout *>()) normalize(form);
            }
        }
        return false;
    }

private:
    static void normalize(QFormLayout *form) {
        if (!form) return;
        if (form->fieldGrowthPolicy() == QFormLayout::FieldsStayAtSizeHint)
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        if (form->formAlignment() == (Qt::AlignHCenter | Qt::AlignTop))
            form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        if (form->labelAlignment() == Qt::AlignRight)
            form->setLabelAlignment(Qt::AlignLeft);
    }
};
#endif

void ThemeManager::ApplyTheme(const QString &theme, bool force) {
    if (this->system_style_name.isEmpty()) {
        this->system_style_name = qApp->style()->name();
        this->system_palette = qApp->palette();
#ifdef Q_OS_MACOS
        qApp->installEventFilter(new UniformFormLayouts(qApp));
#endif
    }

    if (this->current_theme == theme && !force) {
        return;
    }

    const auto lowerTheme = theme.toLower();
    const auto &palettes = customThemePalettes();
    const bool leavingCustom = palettes.contains(current_theme.toLower());
    const bool enteringCustom = palettes.contains(lowerTheme);

    QString themeSheet;
    bool windows11Tabs = false;
    bool macosPane = false;

    if (enteringCustom) {
        // The whole palette goes on first, or a colour role leaks from Qt or the previous theme.
        qApp->setPalette(palettes.value(lowerTheme));
        themeSheet = lowerTheme == "qdarkstyle" ? ReadFileText(":/qdarkstyle/dark/darkstyle.qss")
                                                : ReadFileText(":/qss/" + lowerTheme + ".css");
    } else {
        if (leavingCustom) {
            // Drop the outgoing sheet before restyling, or its rules paint a frame against the
            // incoming palette. A QStyleFactory style owns its own palette.
            qApp->setStyleSheet("");
            qApp->setPalette(system_palette);
        }
        const QString styleName = lowerTheme == "system" ? system_style_name : theme;
        qApp->setStyle(styleName);
        windows11Tabs = styleName.compare(QStringLiteral("windows11"), Qt::CaseInsensitive) == 0;
        if (windows11Tabs) neutralAccent();
        macosPane = styleName.compare(QStringLiteral("macos"), Qt::CaseInsensitive) == 0;
    }

    // After setStyle(), which reinstalls the style's palette. Setting the sheet last is also
    // what clears the render-rule cache; a bare setPalette() does not.
    tokens = resolveTokens(qApp->palette());
    QString sheet = themeSheet + overlayStyleSheet(tokens);
    if (windows11Tabs) sheet += windows11TabStyleSheet(qApp->palette(), tokens);
    if (macosPane) sheet += macPaneStyleSheet(tokens);
    qApp->setStyleSheet(sheet);

    // Every setStyle() above - setStyleSheet() runs one itself whenever it installs or drops the
    // proxy - refills Qt's per-class platform font table (QMenu/QAbstractItemView/QMessageBox...),
    // which outranks the app font. Re-asserting the font drops the table; the second sheet call is
    // a plain repolish that re-resolves the widgets the table already stamped (#1829).
    qApp->setFont(qApp->font());
    qApp->setStyleSheet(sheet);

    current_theme = theme;
    standardIcons.clear();
    // Loaded with the restyle, so the next dialog's validation marks don't pay for them.
    for (const auto pixmap : {QStyle::SP_MessageBoxCritical, QStyle::SP_MessageBoxWarning}) standardIcon(pixmap);

    emit themeChanged(theme);
}

QIcon ThemeManager::standardIcon(QStyle::StandardPixmap pixmap) {
    auto it = standardIcons.constFind(pixmap);
    if (it == standardIcons.constEnd()) it = standardIcons.insert(pixmap, QApplication::style()->standardIcon(pixmap));
    return *it;
}
