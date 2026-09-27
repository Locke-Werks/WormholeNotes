#include "Theme.h"

#include <QFontDatabase>

namespace Theme {
namespace {

QString g_label = QStringLiteral("Segoe UI");
QString g_serif = QStringLiteral("Georgia");
QString g_body = QStringLiteral("Segoe UI");

QString firstAvailable(const QStringList &candidates, const QString &fallback)
{
    const QStringList families = QFontDatabase::families();
    for (const QString &candidate : candidates) {
        for (const QString &family : families) {
            if (family.compare(candidate, Qt::CaseInsensitive) == 0)
                return family;
        }
    }
    return fallback;
}

QString rgba(const QColor &c)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

} // namespace

QColor accentAt(int alpha)
{
    return withAlpha(accent(), alpha);
}

QColor withAlpha(QColor colour, int alpha)
{
    colour.setAlpha(alpha);
    return colour;
}

const QList<Family> &families()
{
    static const QList<Family> all = {
        { QStringLiteral("Cyan"), QColor("#2EE8FF") },    { QStringLiteral("Violet"), QColor("#B05CF6") },
        { QStringLiteral("Blue"), QColor("#3D7DFF") },    { QStringLiteral("Magenta"), QColor("#FF2D95") },
        { QStringLiteral("Crimson"), QColor("#FF1E3C") }, { QStringLiteral("Ember"), QColor("#FF5A2A") },
    };
    return all;
}

QColor sheetColour(const QString &stored)
{
    const QColor colour(stored);
    return colour.isValid() ? colour : accent();
}

void loadFonts()
{
    for (const char *file : { "ChakraPetch-SemiBold.ttf", "ChakraPetch-Medium.ttf", "InstrumentSerif-Regular.ttf",
                              "Outfit-Variable.ttf" })
        QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/") + QLatin1String(file));
    g_label = firstAvailable({ QStringLiteral("Chakra Petch"), QStringLiteral("Segoe UI Semibold") },
                             QStringLiteral("Segoe UI"));
    g_serif = firstAvailable({ QStringLiteral("Instrument Serif"), QStringLiteral("Constantia"),
                               QStringLiteral("Georgia") },
                             QStringLiteral("Georgia"));
    g_body = firstAvailable({ QStringLiteral("Outfit"), QStringLiteral("Segoe UI Variable Text") },
                            QStringLiteral("Segoe UI"));
}

QFont labelFont(qreal pixelSize)
{
    QFont font(g_label);
    font.setPixelSize(qMax(7, qRound(pixelSize)));
    font.setWeight(QFont::DemiBold);
    font.setCapitalization(QFont::AllUppercase);
    // Spaced out far enough to read as a label rather than as small text.
    font.setLetterSpacing(QFont::AbsoluteSpacing, pixelSize * 0.16);
    return font;
}

QFont serifFont(qreal pixelSize)
{
    QFont font(g_serif);
    font.setPixelSize(qMax(8, qRound(pixelSize)));
    font.setLetterSpacing(QFont::AbsoluteSpacing, -0.2);
    return font;
}

QFont bodyFont(qreal pixelSize)
{
    QFont font(g_body);
    font.setPixelSize(qMax(7, qRound(pixelSize)));
    return font;
}

QString styleSheet()
{
    return QStringLiteral(R"(
QToolTip {
    background: %1;
    color: %2;
    border: 1px solid %3;
    padding: 6px 9px;
}
QMenu {
    background: %1;
    color: %4;
    border: 1px solid %3;
    padding: 5px;
}
QMenu::item { padding: 7px 22px 7px 14px; border-radius: 3px; }
QMenu::item:selected { background: %5; color: %2; }
QMenu::item:disabled { color: %6; }
QMenu::separator { height: 1px; background: %7; margin: 5px 8px; }
)")
        .arg(raised().name(), textPrimary().name(), rgba(hairlineStrong()), textBody().name(), rgba(accentAt(30)),
             textFaint().name(), rgba(hairline()));
}

} // namespace Theme
