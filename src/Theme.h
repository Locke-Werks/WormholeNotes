#pragma once

#include <QColor>
#include <QList>
#include <QFont>
#include <QString>

// The house material language, the same one AutoPM speaks: a wet near-black
// ground, violet-warmed ink, hairlines that are never white, and one accent
// spent on emission rather than on fill. Labels are small, uppercase and
// letterspaced; questions are set in the serif; the writing is in the body
// face.
//
// WormholeNotes' accent is cyan, the colour of the wormhole.
namespace Theme {

// Ground, stepping by lightness only.
inline QColor ground() { return QColor("#07050E"); }
inline QColor raised() { return QColor("#100B20"); }
inline QColor input() { return QColor("#17112F"); }
inline QColor pressed() { return QColor("#1F163E"); }

// Hairlines are tinted rather than grey.
inline QColor hairline() { return QColor(176, 92, 246, 41); }
inline QColor hairlineStrong() { return QColor(176, 92, 246, 87); }

inline QColor textPrimary() { return QColor("#F7EFFC"); }
inline QColor textBody() { return QColor("#D9C8E8"); }
inline QColor textSecondary() { return QColor("#B9A3CF"); }
inline QColor textLabel() { return QColor("#A98CC4"); }
inline QColor textFaint() { return QColor("#8D70A4"); }

inline QColor violet() { return QColor("#B05CF6"); }
inline QColor accent() { return QColor("#2EE8FF"); }
QColor accentAt(int alpha);

// The six house colour families. Any of them can be a sheet's ring colour.
struct Family
{
    QString name;
    QColor colour;
};
const QList<Family> &families();
// A sheet's stored colour, or the default ring colour when it has none.
QColor sheetColour(const QString &stored);
// The ring colour of every sheet not given one of its own; cyan, the
// accent, unless changed in the settings.
void setDefaultRing(const QColor &colour);
QColor defaultRing();
QColor withAlpha(QColor colour, int alpha);

// Loads the bundled faces. Each role falls back to a system face when its
// file is missing.
void loadFonts();
QFont labelFont(qreal pixelSize = 11);
QFont serifFont(qreal pixelSize = 20);
QFont bodyFont(qreal pixelSize = 14);

// For the few rectangular things left: the context and tray menus, tooltips.
QString styleSheet();

} // namespace Theme
