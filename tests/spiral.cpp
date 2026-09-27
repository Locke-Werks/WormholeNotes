// SpiralLayout: clicks land on the letter under them, turns step along the
// radius, and long writing pages without splitting a word.

#include "../src/SpiralLayout.h"

#include <QGuiApplication>
#include <QTextDocument>
#include <QTextStream>

#include <cmath>
#include <numbers>

static int failures = 0;

static void check(bool ok, const QString &what)
{
    QTextStream(stdout) << (ok ? "ok   " : "FAIL ") << what << Qt::endl;
    if (!ok)
        ++failures;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QFont font(QStringLiteral("Segoe UI"));
    font.setPixelSize(15);

    SpiralLayout layout;
    layout.setFont(font);
    layout.setDiameter(400);

    QTextDocument doc;
    doc.setPlainText(QStringLiteral("Review the Forge schema before wiring the installer.\nAsk about the layout."));
    layout.layout(&doc);
    check(layout.positionCount() == doc.characterCount() - 1, "one cursor stop per character, and one at the end");
    check(layout.pageCount() == 1, "a short note fits one page");

    // The middle of a glyph's caret stroke is on the groove, so clicking
    // just past it lands on the next position.
    bool clicks = true;
    for (int i = 0; i < layout.positionCount(); i += 7) {
        const QLineF caret = layout.caret(i);
        if (layout.positionAt(0, caret.center()) != i)
            clicks = false;
    }
    check(clicks, "clicking on a caret position finds that position");

    check(layout.turnOf(layout.positionCount()) == 0, "the newest writing is on the outer turn");
    check(std::abs(layout.screenAngle(layout.positionCount()) - SpiralLayout::anchor()) < 1e-9,
          "the newest writing ends at the top");
    check(layout.turnOf(0) >= layout.turnOf(layout.positionCount()), "older writing is further in");
    const int turns = layout.turnCount(0);
    check(turns >= 1, QStringLiteral("the note runs to %1 turn(s)").arg(turns));
    if (turns > 1) {
        const int onSecond = layout.positionOnTurn(0, 1, 0.5);
        check(layout.turnOf(onSecond) == 1, "a position asked for on turn 2 is on turn 2");
        const int back = layout.positionOnTurn(0, 0, layout.angleOf(onSecond));
        check(std::abs(layout.angleOf(back) - layout.angleOf(onSecond)) < 0.2, "Up keeps the angle");
    }

    QString longText;
    for (int i = 0; i < 120; ++i)
        longText += QStringLiteral("wormhole%1 ").arg(i);
    doc.setPlainText(longText);
    layout.layout(&doc);
    check(layout.pageCount() > 1, QStringLiteral("long writing runs onto %1 pages").arg(layout.pageCount()));
    // At every page boundary one side or the other is a space: no word runs
    // across two pages.
    bool whole = true;
    for (int page = 1; page < layout.pageCount(); ++page) {
        const int first = layout.firstOf(page);
        if (first > 0 && !longText.at(first - 1).isSpace() && !longText.at(first).isSpace())
            whole = false;
    }
    check(whole, "no word is split between pages");
    check(layout.pageOf(layout.positionCount()) == layout.pageCount() - 1, "the newest writing is on the last page");

    doc.setPlainText(QString());
    layout.layout(&doc);
    check(layout.positionCount() == 0 && layout.pageCount() == 1, "an empty note is one page with one stop");
    return failures == 0 ? 0 : 1;
}
