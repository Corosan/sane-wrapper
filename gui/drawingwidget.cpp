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

void DrawingWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_surfaceMouseOpsConsumer)
        m_surfaceMouseOpsConsumer->onSurfaceMouseMoveEvent(
            mapFromGlobal(QCursor::pos()));
}

void DrawingWidget::leaveEvent(QEvent*) {
    if (m_surfaceMouseOpsConsumer)
        m_surfaceMouseOpsConsumer->onSurfaceMouseLeaveEvent();
}
