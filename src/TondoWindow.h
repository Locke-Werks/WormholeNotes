#pragma once

#include "TextFile.h"

#include <QFont>
#include <QTimer>
#include <QWidget>

class QAction;
class QActionGroup;
class QMenu;
class QPrinter;
class RadialMenu;
class RadialPrompt;
class RoundEdit;

struct SearchOptions
{
    QString needle;
    QString replacement;
    bool matchCase = false;
    bool wrapAround = true;
    bool backward = false;
};

// The application window: a frameless, translucent top level that paints
// itself as a disc. The bezel is the chrome. Its upper-left arc is the menu
// bar, its top arc the title, its upper right the window buttons and its
// bottom arc the status bar; dragging it moves the window and dragging its
// outer edge resizes the circle. The face is a RoundEdit, with a RadialMenu
// and a RadialPrompt drawn over it when a menu or a question is open.
class TondoWindow : public QWidget
{
    Q_OBJECT

public:
    explicit TondoWindow(QWidget *parent = nullptr);
    ~TondoWindow() override;

    bool openPath(const QString &path);

    // For tests and screenshots.
    RoundEdit *editor() const { return m_edit; }
    RadialMenu *radialMenu() const { return m_radial; }
    RadialPrompt *prompt() const { return m_prompt; }
    void openMenu(int header, bool fromKeyboard);
    void showFind(bool replaceMode);

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
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    enum Button { MinimizeButton, MaximizeButton, CloseButton, ButtonCount };
    enum class Zone { Outside, Face, Ring, Edge, Button, Header };
    enum PromptId { FindNextId = 1, ReplaceId, ReplaceAllId, CloseId, OkId, CancelId, SaveId, DiscardId };

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

    // Painting.
    Theme theme() const;
    void drawButton(QPainter &p, Button button, const Theme &t) const;

    // Chrome.
    void triggerButton(Button button);
    void toggleMaximize();

    // Notepad.
    void createActions();
    void rebuildFontMenus();
    void applyTheme();
    void applyFont();
    void zoomBy(int steps);
    void setZoom(int percent);
    void updateTitle();
    void updateActions();
    QString statusText() const;
    void flash(const QString &message);

    int ask(const QStringList &lines, const QList<QPair<QString, int>> &buttons, int defaultId, int cancelId);
    void tell(const QStringList &lines);

    bool maybeSave();
    void newFile();
    void newWindow();
    void openFile();
    bool save();
    bool saveAs();
    bool writeTo(const QString &path);
    void pageSetup();
    void print();
    void printTo(QPrinter *printer);

    SearchOptions promptOptions() const;
    void onPromptButton(int id);
    bool findNext(bool backward);
    bool findWith(const SearchOptions &options);
    void replaceOne();
    void replaceAll();
    void goToLine();
    void insertTimeDate();
    void chooseFont();
    void setEncoding(Encoding encoding);
    void setLineEnding(LineEnding lineEnding);
    void setAlwaysOnTop(bool on);
    void about();

    void loadSettings();
    void saveSettings() const;

    RoundEdit *m_edit = nullptr;
    RadialMenu *m_radial = nullptr;
    RadialPrompt *m_prompt = nullptr;
    QPrinter *m_printer = nullptr;

    int m_radius = 300;
    bool m_maximized = false;
    QPoint m_restoreCenter;
    int m_restoreRadius = 300;

    int m_hoverButton = -1;
    int m_hoverHeader = -1;
    int m_pressed = -1;
    bool m_resizing = false;
    QPointF m_resizeCenter;
    qreal m_resizeOffset = 0;

    QString m_path;
    Encoding m_encoding = Encoding::Utf8;
    LineEnding m_lineEnding = LineEnding::CRLF;
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

    QAction *m_undo = nullptr;
    QAction *m_redo = nullptr;
    QAction *m_cut = nullptr;
    QAction *m_copy = nullptr;
    QAction *m_delete = nullptr;
    QAction *m_findNextAction = nullptr;
    QAction *m_findPrevAction = nullptr;
    QAction *m_statusAction = nullptr;
    QAction *m_onTopAction = nullptr;
    QAction *m_boldAction = nullptr;
    QAction *m_italicAction = nullptr;
    QActionGroup *m_encodingGroup = nullptr;
    QActionGroup *m_lineEndingGroup = nullptr;
};
