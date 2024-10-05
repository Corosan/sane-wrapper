#pragma once

#include "surface_widgets.h"

#include <QWidget>
#include <QPainter>
#include <QEvent>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QMouseEvent>

class DrawingWidget : public QWidget {
public:
    using QWidget::QWidget;

    void setMouseOpsConsumer(drawing::ISurfaceMouseOps* v) {
        m_surfaceMouseOpsConsumer = v;
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

    void paintEvent(QPaintEvent* ev) override {
        QPainter p(this);

        p.drawLine(0, 0, size().width(), size().height());
    }

    bool eventFilter(QObject *watched, QEvent *event) override;

#if QT_VERSION >= 0x060000
    void enterEvent(QEnterEvent*) override;
#else
    void enterEvent(QEvent*) override;
#endif
    void leaveEvent(QEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
};
