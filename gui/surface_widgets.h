#pragma once

#include <QPainter>
#include <QPaintEvent>
#include <QRect>
#include <QSize>
#include <QRegion>
#include <QList>
#include <QPair>
#include <QPoint>

#include <utility>
#include <functional>

/*!
 * \file Widgets and Planes
 *
 * The idea is to invent an abstract drawing layer for various graphic primitives
 * which represent auxiliary editing instruments on a scanned image.
 *
 * Visual model:
 *
 *     origin(0,0)
 *        \   +-------------------+
 *         \  |    top ruller     |
 *          \ +-------------------+
 *     +---+  +-------------------+  +---+
 *     |   |  |                   |  | r |
 *     | l |  |  +-----------+    |  | i |
 *     | e |  |  |           |    |  | g |
 *     | f |  |  |  scanned  |    |  | h |
 *     | t |  |  |   image   |    |  | t |
 *     |   |  |  |           |    |  |   |
 *     | r |  |  +-----------+    |  | r |
 *     | u |  |                   |  | u |
 *     | l |  |                   |  | l |
 *     | l |  |                   |  | l |
 *     | e |  |     drawing       |  | e |
 *     | r |  |    Qt widget      |  | r |
 *     +---+  +------------------ +  +---+
 *            +-------------------+
 *            |   bottom ruller   |
 *            +-------------------+
 *
 * Each graphical block here is an abstract drawing plane. A widget (not a Qt's
 * QWidget object but a graphical primitive) can request an interface to any
 * plane and draw on it.
 *
 * A coordinate system (CS) of a plane can be shifted on any value related to
 * mouse CS for mouse events. The plane coordinate system is consistent between
 * drawing and updating operations - what is requested to invalidate through
 * IUpdatePlane::invalidatePlane(...) - is the same which can be repainted in
 * IPlaneWidget::draw(...).
 *
 * Mouse CS is aligned to a drawing Qt widget on a screen because assumed that
 * user interacts with the Qt widget by mouse for making any actions on scanned
 * image.
 *
 * Ruller CSs are shifted related to mouse CS as it would be the rullers
 * situated on a top left corner of the surface plane:
 *
 *     +---+--------------------+
 *     |   |                    |
 *     +---+--------------------+
 *     |   |         ^          |
 *     |   |         |          |
 *     |   |     top, bottom    |
 *     |   |       rullers      |
 *     |   |                    |
 *     |   |                    |
 *     |   | <-- left, right    |
 *     |   |       rullers      |
 *     |   |                    |
 *     +---+--------------------+
 *
 * Mouse CS = Drawing Qt Widget CS = Other Planes' CS + VisualOffset. Thus when
 * a dot must be placed on a {point[mouse CS]}, drawing primitives must be called
 * with {point[plane CS]} = {point[mouse CS]} - Visual Offset;
 */

namespace drawing {

/*!
 * \brief Visual plane for drawing on by widgets
 */
struct IUpdatePlane {
    virtual void invalidatePlane(int x, int y, int w, int h) = 0;
    virtual void invalidatePlane(const QRect &rect) = 0;
    virtual void invalidatePlane(const QRegion &rgn) = 0;
    virtual QSize planeSize() = 0;
    virtual QPoint visualOffset() { return {}; }

protected:
    ~IUpdatePlane() = default;
};

/*!
 * \brief Widget assuming to draw on a plane possibly with other widgets
 */
struct IPlaneWidget {
    virtual void draw(QPainter&, QPaintEvent*) = 0;
    virtual void setUpdatePlane(IUpdatePlane*) = 0;

protected:
    ~IPlaneWidget() = default;
};

struct IPlane {
    virtual void insertWidget(IPlaneWidget&, unsigned) = 0;
    virtual void removeWidget(IPlaneWidget&) = 0;

protected:
    ~IPlane() = default;
};

class PlaneBase : public IPlane {
protected:
    PlaneBase(IUpdatePlane* parentPlane) : m_parentPlane(parentPlane) {
    }

    void insertWidget(IPlaneWidget&, unsigned) override;
    void removeWidget(IPlaneWidget&) override;

    void draw(QPainter& p, QPaintEvent* ev) {
        for (auto [w, _] : m_planeWidgets)
            w->draw(p, ev);
    }

private:
    // Sorted by zOrder (second field of a pair) from lowest to biggest,
    // so the biggest one will be drawn above all other widgets
    QList<QPair<drawing::IPlaneWidget*, unsigned>> m_planeWidgets;
    IUpdatePlane* m_parentPlane;
};

template <class Der, class Tag>
struct PlaneWidgetPart : IPlaneWidget {
    void draw(QPainter& p, QPaintEvent* ev) override {
        static_cast<Der*>(this)->draw(m_plane, p, ev, Tag{});
    }

    void setUpdatePlane(IUpdatePlane* p) override {
        static_cast<Der*>(this)->onUpdatePlane(m_plane, p, Tag{});
        m_plane = p;
    }

    IUpdatePlane* m_plane = nullptr;
};

template <class Der, class ... Tags>
struct PlaneWidgetParts : PlaneWidgetPart<Der, Tags> ... {
    PlaneWidgetParts() {
        init(std::index_sequence_for<Tags ...>{});
    }

    IUpdatePlane** m_planes[sizeof... (Tags)];

private:
    template <size_t ... I>
    void init(std::index_sequence<I ...>) {
        (void)((m_planes[I] = &static_cast<PlaneWidgetPart<Der, Tags>*>(this)->m_plane), ...);
    }
};

struct RullerTopTag {};
struct RullerBottomTag {};
struct RullerLeftTag {};
struct RullerRightTag {};
struct SurfaceTag {};

struct IPlaneProvider {
    virtual IPlane& getRullerTopPlane() = 0;
    virtual IPlane& getRullerBottomPlane() = 0;
    virtual IPlane& getRullerLeftPlane() = 0;
    virtual IPlane& getRullerRightPlane() = 0;
    virtual IPlane& getSurfacePlane() = 0;

protected:
    ~IPlaneProvider() = default;
};

struct ISurfaceMouseOps {
    virtual void onSurfaceMouseEnterEvent(QPoint localPos) {}
    virtual void onSurfaceMouseMoveEvent(QPoint newLocalPos) {}
    virtual void onSurfaceMouseLeaveEvent() {}
    virtual void onSurfaceMousePressEvent(QPoint localPos) {}
    virtual void onSurfaceMouseReleaseEvent(QPoint localPos) {}

protected:
    ~ISurfaceMouseOps() = default;
};

//-----------------------------------------------------------------------------

namespace details {

template <class T, bool Horizontal> struct DashedCursorWidgetParts
    : PlaneWidgetParts<T, RullerLeftTag, RullerRightTag, SurfaceTag> {};

template <class T> struct DashedCursorWidgetParts<T, false>
    : PlaneWidgetParts<T, RullerTopTag, RullerBottomTag, SurfaceTag> {};

}

template <bool Horizontal>
class DashedCursorLineWidget
    : details::DashedCursorWidgetParts<DashedCursorLineWidget<Horizontal>, Horizontal> {

    template <class, class> friend class PlaneWidgetPart;
    static constexpr bool s_isHorizontal = Horizontal;

public:
    DashedCursorLineWidget()
        : m_dashCursorPen(Qt::DashLine) {
        m_dashCursorPen.setColor(QColor(32, 32, 255));
    }

    template <class Tag>
    IPlaneWidget& getView() {
        return *static_cast<PlaneWidgetPart<DashedCursorLineWidget, Tag>*>(this);
    }

    void setCursorPos(int v);

private:
    QPen m_dashCursorPen;
    int m_cursorPosition = -1;

    static QRect getRect(int cursorPos, IUpdatePlane*, bool isForDrawn);

    void draw(IUpdatePlane*, QPainter&, QPaintEvent*, ...);
    void onUpdatePlane(IUpdatePlane*, IUpdatePlane*, ...);
};

class DashedRectWidget : PlaneWidgetPart<DashedRectWidget, SurfaceTag> {
    friend PlaneWidgetPart<DashedRectWidget, SurfaceTag>;

public:
    DashedRectWidget()
        : m_dashPen(Qt::DashLine) {
        m_dashPen.setColor(QColor(255, 32, 32));
    }

    IPlaneWidget& getView() { return static_cast<IPlaneWidget&>(*this); }

    void setRect(const QRect& rc);

private:
    QPen m_dashPen;
    QRect m_rc;

    void draw(IUpdatePlane*, QPainter&, QPaintEvent*, SurfaceTag);
    void onUpdatePlane(IUpdatePlane*, IUpdatePlane*, SurfaceTag);
};

class DashedCursorController : public ISurfaceMouseOps {
public:
    DashedCursorController(IPlaneProvider& pp);
    ~DashedCursorController();

    void setSurfaceScale(float);
    void setSurfaceImageRect(const QRect&);
    void setScannedCoordsChangedCb(std::function<void(QPoint)> cb) {
        m_scannedCoordsChangedCb = std::move(cb);
    }
    void onSurfaceMouseEnterEvent(QPoint localPos) override;
    void onSurfaceMouseMoveEvent(QPoint localPos) override;
    void onSurfaceMouseLeaveEvent() override;

private:
    IPlaneProvider* m_pp;
    float m_surfaceScale = 1.0;
    QRect m_surfaceRect;
    DashedCursorLineWidget<true> m_horzLine;
    DashedCursorLineWidget<false> m_vertLine;
    QPoint m_lastCursorPos{-1, -1};
    QPoint m_surfaceScannedCoords;
    std::function<void(QPoint)> m_scannedCoordsChangedCb;

    void recalcCross();
};


} // ns drawing
