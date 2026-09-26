#pragma once

#include <QFont>
#include <QTimer>
#include <QWidget>

class QAction;
class QActionGroup;
class QMenu;
class RadialMenu;
class RadialPrompt;
class RoundEdit;

// One stop on the bezel's ring of open places.
struct RingPlace
{
    QString key;
    QString label;
    bool written = false; // has a sheet with something on it
};

struct SearchOptions
{
    QString needle;
    QString replacement;
    bool matchCase = false;
    bool wrapAround = true;
    bool backward = false;
};

// The note: Tondo's round window, holding one place's sheet. A frameless,
// translucent top level that paints itself as a disc. The bezel is the
// chrome: its upper-left arc is the menu bar, its top arc the place, its
// upper right the window buttons and its bottom arc the status bar. Dragging
// it moves the note and dragging its outer edge resizes the circle.
//
// It opens out of the hole and folds back into it. Closing it, pressing
// Escape or switching to another app puts it away; nothing is ever unsaved,
// because every edit goes straight to the sheet.
class NoteWindow : public QWidget
{
    Q_OBJECT

public:
    explicit NoteWindow(QWidget *parent = nullptr);
    ~NoteWindow() override;

    // Loads a sheet, one of count on its place. Its edits come back through
    // textEdited. fromEnd opens it on its last page, for turning backwards.
    void setSheet(const QString &label, const QString &text, int index, int count, bool fromEnd = false);
    // The open places, and which one the clip is on.
    void setRing(const QList<RingPlace> &places, int current);
    QString text() const;
    // Opens the circle centred on a point in logical screen coordinates,
    // pulled in as far as needed to stay on that screen.
    void openAt(const QPoint &globalCenter);
    void putAway();

    // For tests and screenshots.
    RoundEdit *editor() const { return m_edit; }
    RadialMenu *radialMenu() const { return m_radial; }
    RadialPrompt *prompt() const { return m_prompt; }
    void openMenu(int header, bool fromKeyboard);
    void showFind(bool replaceMode);

Q_SIGNALS:
    void textEdited(const QString &text);
    void putAwayRequested();
    void quitRequested();
    // The clip was turned to another place.
    void placeTurned(const QString &key);
    // The hub turned past this sheet's first or last page.
    void sheetTurnRequested(int direction);
    void newSheetRequested();
    void deleteSheetRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    enum Button { MaximizeButton, CloseButton, ButtonCount };
    enum class Zone { Outside, Face, Ring, Edge, Button, Header, Clip };
    enum PromptId { FindNextId = 1, ReplaceId, ReplaceAllId, CloseId, OkId, CancelId };

    struct Theme
    {
        QColor face, faceEdge, ink, ringTop, ringBottom, ringInk, ringDim, selection, band, bandEdge, field;
        bool dark = false;
    };

    struct Header
    {
        QMenu *menu = nullptr;
        QString label;
        qreal from = 0;
        qreal sweep = 0;
    };

    // Geometry, all derived from the radius.
    QPointF center() const;
    int ringWidth() const;
    qreal innerRadius() const;
    qreal ringMid() const;
    qreal buttonRadius() const;
    QPointF buttonCenter(Button button) const;
    QFont bezelFont(qreal scale, bool bold) const;
    QList<Header> headers() const;
    Zone zoneAt(const QPointF &pos, int *index = nullptr) const;
    void setCircle(const QPoint &globalCenter, int radius);
    int maximumRadius() const;

    // The ring of places.
    qreal placeAngle(int index) const;
    qreal clipAngle() const;
    QPointF clipCenter() const;
    int nearestPlace(qreal angle) const;
    void turnToPlace(int index);

    // Painting.
    Theme theme() const;
    void drawButton(QPainter &p, Button button, const Theme &t) const;
    void drawRing(QPainter &p, const Theme &t) const;

    // Chrome.
    void triggerButton(Button button);
    void toggleMaximize();

    // The editor.
    void createActions();
    void rebuildFamilyMenu();
    void rebuildSizeMenu();
    bool modalOpen() const;
    void applyTheme();
    void applyFont();
    void zoomBy(int steps);
    void setZoom(int percent);
    void updateActions();
    QString statusText() const;
    void flash(const QString &message);
    void checkStillActive();

    int ask(const QStringList &lines, const QList<QPair<QString, int>> &buttons, int defaultId, int cancelId);
    void tell(const QStringList &lines);

    SearchOptions promptOptions() const;
    void onPromptButton(int id);
    bool findNext(bool backward);
    bool findWith(const SearchOptions &options);
    void replaceOne();
    void replaceAll();
    void goToLine();
    void insertTimeDate();
    void chooseFont();
    void about();

    void loadSettings();
    void saveSettings() const;

    RoundEdit *m_edit = nullptr;
    RadialMenu *m_radial = nullptr;
    RadialPrompt *m_prompt = nullptr;

    int m_radius = 220;
    bool m_maximized = false;
    QPoint m_restoreCenter;
    int m_restoreRadius = 220;

    int m_hoverButton = -1;
    int m_hoverHeader = -1;
    int m_pressed = -1;
    bool m_resizing = false;
    QPointF m_resizeCenter;
    qreal m_resizeOffset = 0;

    QList<RingPlace> m_ring;
    int m_ringCurrent = 0;
    bool m_clipDragging = false;
    qreal m_clipDragAngle = 0;
    int m_wheelAccum = 0;

    bool m_loading = false;
    bool m_choosingFont = false;
    QFont m_baseFont;
    int m_zoom = 100;
    bool m_statusVisible = true;
    SearchOptions m_lastSearch;
    QString m_flash;
    QTimer m_flashTimer;

    enum class PromptMode { None, Find, Replace };
    PromptMode m_promptMode = PromptMode::None;
    int m_findField = -1;
    int m_replaceField = -1;
    int m_caseToggle = -1;
    int m_wrapToggle = -1;
    int m_upToggle = -1;

    QList<QMenu *> m_menus; // the menu bar, in order
    QMenu *m_rootMenu = nullptr;
    QMenu *m_contextMenu = nullptr;
    QMenu *m_familyMenu = nullptr;
    QMenu *m_sizeMenu = nullptr;
    QActionGroup *m_familyGroup = nullptr;
    QActionGroup *m_sizeGroup = nullptr;

    QAction *m_undo = nullptr;
    QAction *m_redo = nullptr;
    QAction *m_cut = nullptr;
    QAction *m_copy = nullptr;
    QAction *m_delete = nullptr;
    QAction *m_findNextAction = nullptr;
    QAction *m_findPrevAction = nullptr;
    QAction *m_statusAction = nullptr;
    QAction *m_boldAction = nullptr;
    QAction *m_italicAction = nullptr;
};
