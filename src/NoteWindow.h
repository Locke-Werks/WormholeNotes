#pragma once

#include <QFont>
#include <QList>
#include <QTimer>
#include <QWidget>

#include <functional>

class ConfirmBand;
class NoteFace;
class QMenu;

// One stop on the rim's ring of open places.
struct RingPlace
{
    QString key;
    QString label;
    bool written = false; // has a sheet with something on it
    QColor colour;        // its first written sheet's ring colour
};

// The note: a round, frameless window with a paper face and a rim. The rim
// carries the place's name across the top, a notch for every open place and
// a binder clip on the current one, and three pushers at the upper right:
// new sheet, delete sheet, put away. Dragging the rim moves the note;
// dragging its outer edge resizes the circle.
//
// It opens out of the hole and folds back into it. Nothing is ever unsaved,
// because every edit goes straight to the sheet.
class NoteWindow : public QWidget
{
    Q_OBJECT

public:
    explicit NoteWindow(QWidget *parent = nullptr);
    ~NoteWindow() override;

    // Loads a sheet, one of count on its place, at its newest writing. Its
    // edits come back through textEdited.
    void setSheet(const QString &label, const QString &text, int index, int count);
    // The open places, and which one the clip is on.
    void setRing(const QList<RingPlace> &places, int current);
    // Selects a stretch of the sheet, as a search hit.
    void selectRange(int start, int length);
    // The sheet's ring colour, which lights the rim, the bloom and the caret.
    void setSheetColour(const QColor &colour);
    QString text() const;
    // Opens the circle centred on a point in logical screen coordinates,
    // pulled in as far as needed to stay on that screen.
    void openAt(const QPoint &globalCenter);
    void putAway();
    // Its centre, in physical pixels.
    QPoint physicalCenter() const;
    QColor sheetColour() const { return m_colour; }

Q_SIGNALS:
    void textEdited(const QString &text);
    void putAwayRequested();
    void quitRequested();
    // The clip was turned to another place.
    void placeTurned(const QString &key);
    // Turned past this sheet's first or last page.
    void sheetTurnRequested(int direction);
    void newSheetRequested();
    void deleteSheetRequested();
    void tearOffRequested();
    void colourChosen(const QColor &colour);
    // Dragging the note by its rim: started, under way, finished.
    void moveStarted();
    void moving();
    void moved();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    enum Pusher { NewPusher, DeletePusher, ColourPusher, AwayPusher, PusherCount };
    enum class Zone { Outside, Face, Rim, Edge, Pusher, Clip };


    // Geometry, all from the radius.
    QPointF center() const;
    qreal rimWidth() const;
    qreal faceRadius() const;
    qreal rimMid() const;
    qreal pusherRadius() const;
    QPointF pusherCenter(Pusher pusher) const;
    Zone zoneAt(const QPointF &pos, int *index = nullptr) const;
    void setCircle(const QPoint &globalCenter, int radius);
    int maximumRadius() const;

    // The ring of places.
    qreal placeAngle(int index) const;
    qreal clipAngle() const;
    QPointF clipCenter() const;
    int nearestPlace(qreal angle) const;
    void turnToPlace(int index);

    void applyColours();
    void drawPusher(QPainter &p, Pusher pusher) const;
    void drawRing(QPainter &p) const;
    void nextColour();

    void pressPusher(Pusher pusher);
    void confirmDelete();
    void about();
    // A question across the middle of the face. Answers come back through
    // the callback; Escape or clicking away is no.
    void ask(const QString &question, const QString &yes, const QString &no, std::function<void(bool)> answer);
    bool asking() const;
    void checkStillActive();
    void zoomBy(int steps);
    void applyFont();
    void loadSettings();
    void saveSettings() const;

    NoteFace *m_face = nullptr;
    ConfirmBand *m_band = nullptr;
    QMenu *m_menu = nullptr;
    QMenu *m_colourMenu = nullptr;
    QColor m_colour;

    int m_radius = 210;
    int m_restoreRadius = 210;
    int m_zoom = 100;

    int m_hoverPusher = -1;
    bool m_hoverEdge = false;
    int m_pressedPusher = -1;
    bool m_resizing = false;
    QPointF m_resizeCenter;
    qreal m_resizeOffset = 0;

    QList<RingPlace> m_ring;
    int m_ringCurrent = 0;
    bool m_clipDragging = false;
    qreal m_clipDragAngle = 0;
    QString m_clipHint;
    int m_wheel = 0;
};
