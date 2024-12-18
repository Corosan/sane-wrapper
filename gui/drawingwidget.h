#pragma once

#include "surface_widgets.h"

#include <QWidget>
#include <QPainter>
#include <QPoint>
#include <QEvent>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QMouseEvent>
#include <QKeyEvent>

#include <QtDebug>

class DrawingWidget
    : public QWidget, drawing::IUpdatePlane, public drawing::PlaneBase {
public:
    explicit DrawingWidget(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags())
        : QWidget(parent, f)
        , PlaneBase(this) {
    }

    void setMouseOpsConsumer(drawing::ISurfaceMouseOps* v) {
        m_surfaceMouseOpsConsumer = v;
        setMouseTracking(v);
    }

    void setKbdOpsConsumer(drawing::ISurfaceKbdOps* v) {
        m_surfaceKbdOpsConsumer = v;
        setFocusPolicy(v ? Qt::StrongFocus : Qt::NoFocus);
    }
private:
    // This widget receives editing mouse events which should be propagated to currently existing
    // drawing controller. A controller is hold my MainWindow object typically. It could be better
    // to not introduce one more interface and write boilerplate code here in order to capture
    // the events but instead capture them by MainWindow object with {filter-events} mechanism.
    // Unfortunatelly that mechanism can't differentiate enterEvent from generic Event due to
    // using just a base Event type for event objects provided into enterEvent(...) handler of a
    // Widget.
    drawing::ISurfaceMouseOps* m_surfaceMouseOpsConsumer = nullptr;
    drawing::ISurfaceKbdOps* m_surfaceKbdOpsConsumer = nullptr;

    void setCursorShape(Qt::CursorShape v) override {
        setCursor(v);
    }

    void paintEvent(QPaintEvent* ev) override {
        QPainter p(this);

        PlaneBase::draw(p, ev);
    }

    bool eventFilter(QObject *watched, QEvent *event) override;

#if QT_VERSION >= 0x060000
    void enterEvent(QEnterEvent*) override;
#else
    void enterEvent(QEvent*) override;
#endif
    void leaveEvent(QEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;

    // drawing::IUpdatePlane interface implementation

    void invalidatePlane(int x, int y, int w, int h) override { update(x, y, w, h); }
    void invalidatePlane(const QRect &rect) override { update(rect); }
    void invalidatePlane(const QRegion &rgn) override { update(rgn); }
    QSize planeSize() override { return size(); }
};
