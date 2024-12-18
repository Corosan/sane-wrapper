#include "drawingwidget.h"

bool DrawingWidget::eventFilter(QObject *watched, QEvent *event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        resize(static_cast<QResizeEvent*>(event)->size());
    }
    if (watched == parentWidget() && event->type() == QEvent::Move) {
        move(static_cast<QMoveEvent*>(event)->pos());
    }

    return QWidget::eventFilter(watched, event);
}

#if QT_VERSION >= 0x060000
void DrawingWidget::enterEvent(QEnterEvent*) {
#else
void DrawingWidget::enterEvent(QEvent*) {
#endif
    if (m_surfaceMouseOpsConsumer)
        m_surfaceMouseOpsConsumer->onSurfaceMouseEnterEvent(
            mapFromGlobal(QCursor::pos()));
}

void DrawingWidget::mouseMoveEvent(QMouseEvent* ev) {
    if (m_surfaceMouseOpsConsumer)
        m_surfaceMouseOpsConsumer->onSurfaceMouseMoveEvent(ev->pos());
}

void DrawingWidget::leaveEvent(QEvent*) {
    if (m_surfaceMouseOpsConsumer)
        m_surfaceMouseOpsConsumer->onSurfaceMouseLeaveEvent();
}

void DrawingWidget::mousePressEvent(QMouseEvent* ev) {
    if (m_surfaceMouseOpsConsumer)
        m_surfaceMouseOpsConsumer->onSurfaceMousePressEvent(ev->pos());
}

void DrawingWidget::mouseReleaseEvent(QMouseEvent* ev) {
    if (m_surfaceMouseOpsConsumer)
        m_surfaceMouseOpsConsumer->onSurfaceMouseReleaseEvent(ev->pos());
}

void DrawingWidget::keyPressEvent(QKeyEvent* ev) {
    if (m_surfaceKbdOpsConsumer)
        m_surfaceKbdOpsConsumer->keyPressEvent(ev);
    QWidget::keyPressEvent(ev);
}

void DrawingWidget::keyReleaseEvent(QKeyEvent* ev) {
    if (m_surfaceKbdOpsConsumer)
        m_surfaceKbdOpsConsumer->keyReleaseEvent(ev);
    QWidget::keyReleaseEvent(ev);
}
