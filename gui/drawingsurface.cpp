#include "drawingsurface.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QRadialGradient>
#include <QLinearGradient>
#include <QPoint>
#include <QTransform>
#include <QCursor>
#include <QRegion>

#include <QtGlobal>
#include <QtDebug>

#include <cstring>
#include <cmath>
#include <algorithm>

#ifdef QT_DEBUG
// #define DBG_DUMP_PAINTING_ACTIONS
#endif

void IImageHolder::ImageModifier::setHeight(int height) {
    if (m_imageHolder->image().height() != height) {
        QImage newImage(m_imageHolder->image().width(), height, m_imageHolder->image().format());
        std::memcpy(newImage.bits(), m_imageHolder->image().bits(),
            std::min(m_imageHolder->image().sizeInBytes(), newImage.sizeInBytes()));
        m_imageHolder->image().swap(newImage);
        m_doUpdateAll = true;
    }
}

unsigned char* IImageHolder::ImageModifier::scanLine(int i, int leftAffectedPx, int affectedPxCount) {
    if (m_imageHolder->image().height() <= i)
        setHeight(i + s_growHeight);

    m_imageUpdateRect |= QRect(leftAffectedPx, i, affectedPxCount, 1);
    return m_imageHolder->image().scanLine(i);
}


DrawingSurface::DrawingSurface(QWidget *parent)
    : QWidget(parent)
    , PlaneBase(this)
    , m_dashCursorPen(Qt::DashLine)
    , m_horzDashCursor(this, DashedCursorLine::Direction::Horizontal)
    , m_vertDashCursor(this, DashedCursorLine::Direction::Vertical) {
    //setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    m_dashCursorPen.setColor(QColor(32, 32, 255));
}

void DrawingSurface::setScale(float val) {
    if (val != m_scale) {
        m_scale = val;
        recalcImageGeometry();
        emit scaleChanged(m_scale);
    }
}

void DrawingSurface::showDashCursor(bool val) {
    if (m_showDashCursor != val) {
        m_showDashCursor = val;
        if (m_showDashCursor && m_cursorInWorkingArea) {
            const QPoint cursorPt = mapFromGlobal(QCursor::pos());
            int xOnScan, yOnScan;
            m_horzDashCursor.enterWindow(cursorPt, yOnScan);
            m_vertDashCursor.enterWindow(cursorPt, xOnScan);
            emit dashedCursorPoint(xOnScan, yOnScan);
        }
        if (! m_showDashCursor && m_cursorInWorkingArea) {
            m_horzDashCursor.leaveWindow();
            m_vertDashCursor.leaveWindow();
            emit dashedCursorPoint(-1, -1);
        }
    }
}

void DrawingSurface::redrawScannedDocImage(const QRect& rect) {
#ifdef DBG_DUMP_PAINTING_ACTIONS
    qDebug() << "request to update scanned image at" << rect;
#endif

    QPainter(&m_displayedPixmap)
        .drawImage(QRect(QPoint(0, 0), m_scannedDocImageDisplaySize), m_scannedDocImage);

    // Coordinates on input are the image coordinates, so they need to be translated to the screen ones.
    update(m_marginWidth + rect.x() * m_scale, m_marginWidth + rect.y() * m_scale,
        std::ceil(rect.width() * m_scale), std::ceil(rect.height() * m_scale));
}

void DrawingSurface::recalcScannedDocImageGeometry() {
    // Note that scale multiplication must use real-to-integer rounding always (getting nearest integer)
    // which is implemented here by operator* of QSize class.
    m_scannedDocImageDisplaySize = m_scannedDocImage.size() * m_scale;
    m_marginWidth = std::min(
        {m_scannedDocImageDisplaySize.width() / 2, m_scannedDocImageDisplaySize.height() / 2, 20});
    m_thisSurfaceSize = {
        m_scannedDocImageDisplaySize.width() + m_marginWidth * 2,
        m_scannedDocImageDisplaySize.height() + m_marginWidth * 2};

#ifdef DBG_DUMP_PAINTING_ACTIONS
    qDebug() << "request to recalculate drawing surface geometry:" << m_scannedDocImage.size() << "->" << m_scannedDocImageDisplaySize;
#endif

    m_displayedPixmap = QPixmap(m_scannedDocImageDisplaySize);

    QPainter(&m_displayedPixmap)
        .drawImage(QRect(QPoint(0, 0), m_scannedDocImageDisplaySize), m_scannedDocImage);

    QGradientStops const grStops{{0, Qt::gray}, {1, Qt::white}};
    const auto gradWidth = m_marginWidth / 2;

    QRadialGradient gr0(m_marginWidth - 1, m_marginWidth - 1, gradWidth);
    gr0.setStops(grStops);
    m_segmentBrushes[0] = QBrush(gr0);

    QLinearGradient gr1(0, m_marginWidth - 1, 0, m_marginWidth - 1 - gradWidth);
    gr1.setStops(grStops);
    m_segmentBrushes[1] = QBrush(gr1);

    QRadialGradient gr2(m_thisSurfaceSize.width() - m_marginWidth, m_marginWidth - 1, gradWidth);
    gr2.setStops(grStops);
    m_segmentBrushes[2] = QBrush(gr2);

    QLinearGradient gr3(m_thisSurfaceSize.width() - m_marginWidth, 0, m_thisSurfaceSize.width() - m_marginWidth + gradWidth, 0);
    gr3.setStops(grStops);
    m_segmentBrushes[3] = QBrush(gr3);

    QRadialGradient gr4(m_thisSurfaceSize.width() - m_marginWidth, m_thisSurfaceSize.height() - m_marginWidth, gradWidth);
    gr4.setStops(grStops);
    m_segmentBrushes[4] = QBrush(gr4);

    QLinearGradient gr5(0, m_thisSurfaceSize.height() - m_marginWidth, 0, m_thisSurfaceSize.height() - m_marginWidth + gradWidth);
    gr5.setStops(grStops);
    m_segmentBrushes[5] = QBrush(gr5);

    QRadialGradient gr6(m_marginWidth - 1, m_thisSurfaceSize.height() - m_marginWidth, gradWidth);
    gr6.setStops(grStops);
    m_segmentBrushes[6] = QBrush(gr6);

    QLinearGradient gr7(m_marginWidth - 1, 0, m_marginWidth - 1 - gradWidth, 0);
    gr7.setStops(grStops);
    m_segmentBrushes[7] = QBrush(gr7);

    // TODO: repainting of this inner widget is far from efficient - the whole area is repainted
    // on scrolling which is strange and redundant. QScrollArea::scrollContentsBy() (this method
    // should be called by scroll bars, as I understand) calls private class's updateWidgetPosition()
    // which calls this inner widget's move(...). Further steps are too complicated for superficial
    // analysis - why the whole widget is repainted eventually.

    //updateGeometry();
    //update();
    resize(m_thisSurfaceSize);
//    emit mainImageGeometryChanged(QRect(pos() + QPoint(m_marginWidth, m_marginWidth), imageDisplaySize));
}

void DrawingSurface::mirror(bool isVertical) {
    if (isVertical)
        m_scannedDocImage = m_scannedDocImage.mirrored(true, false);
    else
        m_scannedDocImage = m_scannedDocImage.mirrored();

    update();
}

void DrawingSurface::rotate(bool isClockWise) {
    QTransform tr;
    tr.rotate(isClockWise ? 90.0 : -90.0);
    m_scannedDocImage = m_scannedDocImage.transformed(tr);

    recalcScannedDocImageGeometry();
}

namespace {

QPoint dividePoints(QPoint p, float scale, bool roundUp) {
    if (roundUp)
        return QPoint(std::ceil(p.x() / scale), std::ceil(p.y() / scale));
    else
        return QPoint(std::floor(p.x() / scale), std::floor(p.y() / scale));
}

QSize dividePoints(QSize p, float scale, bool roundUp) {
    if (roundUp)
        return QSize(std::ceil(p.width() / scale), std::ceil(p.height() / scale));
    else
        return QSize(std::floor(p.width() / scale), std::floor(p.height() / scale));
}

} // ns anonymous

void DrawingSurface::paintEvent(QPaintEvent* ev) {
    QPainter painter(this);
    QRect r;

    // Display gradient borders around the main image
    //

#ifdef DBG_DUMP_PAINTING_ACTIONS
    QString dbgFlags;
#endif

    // Upper left corner
    r = {0, 0, m_marginWidth, m_marginWidth};
    if (! r.intersected(ev->rect()).isEmpty()) {
        painter.fillRect(r, m_segmentBrushes[0]);
#ifdef DBG_DUMP_PAINTING_ACTIONS
        dbgFlags += '1';
    } else
        dbgFlags += '0';
#else
    }
#endif

    // Upper line
    r = {m_marginWidth, 0, m_scannedDocImageDisplaySize.width(), m_marginWidth};
    if (! r.intersected(ev->rect()).isEmpty()) {
        painter.fillRect(r, m_segmentBrushes[1]);
#ifdef DBG_DUMP_PAINTING_ACTIONS
        dbgFlags += '2';
    } else
        dbgFlags += '0';
#else
    }
#endif

    // Upper right corner
    r = {m_thisSurfaceSize.width() - m_marginWidth, 0, m_marginWidth, m_marginWidth};
    if (! r.intersected(ev->rect()).isEmpty()) {
        painter.fillRect(r, m_segmentBrushes[2]);
#ifdef DBG_DUMP_PAINTING_ACTIONS
        dbgFlags += '3';
    } else
        dbgFlags += '0';
#else
    }
#endif

    // Right vertical line
    r = {m_thisSurfaceSize.width() - m_marginWidth, m_marginWidth, m_marginWidth, m_scannedDocImageDisplaySize.height()};
    if (! r.intersected(ev->rect()).isEmpty()) {
        painter.fillRect(r, m_segmentBrushes[3]);
#ifdef DBG_DUMP_PAINTING_ACTIONS
        dbgFlags += '4';
    } else
        dbgFlags += '0';
#else
    }
#endif

    // Lower right corner
    r = {m_thisSurfaceSize.width() - m_marginWidth, m_thisSurfaceSize.height() - m_marginWidth, m_marginWidth, m_marginWidth};
    if (! r.intersected(ev->rect()).isEmpty()) {
        painter.fillRect(r, m_segmentBrushes[4]);
#ifdef DBG_DUMP_PAINTING_ACTIONS
        dbgFlags += '5';
    } else
        dbgFlags += '0';
#else
    }
#endif

    // Lower line
    r = {m_marginWidth, m_thisSurfaceSize.height() - m_marginWidth, m_scannedDocImageDisplaySize.width(), m_marginWidth};
    if (! r.intersected(ev->rect()).isEmpty()) {
        painter.fillRect(r, m_segmentBrushes[5]);
#ifdef DBG_DUMP_PAINTING_ACTIONS
        dbgFlags += '6';
    } else
        dbgFlags += '0';
#else
    }
#endif

    // Lower left corner
    r = {0, m_thisSurfaceSize.height() - m_marginWidth, m_marginWidth, m_marginWidth};
    if (! r.intersected(ev->rect()).isEmpty()) {
        painter.fillRect(r, m_segmentBrushes[6]);
#ifdef DBG_DUMP_PAINTING_ACTIONS
        dbgFlags += '7';
    } else
        dbgFlags += '0';
#else
    }
#endif

    // Left vertical line
    r = {0, m_marginWidth, m_marginWidth, m_scannedDocImageDisplaySize.height()};
    if (! r.intersected(ev->rect()).isEmpty()) {
        painter.fillRect(r, m_segmentBrushes[7]);
#ifdef DBG_DUMP_PAINTING_ACTIONS
        dbgFlags += '8';
    } else
        dbgFlags += '0';
#else
    }
#endif

#ifdef DBG_DUMP_PAINTING_ACTIONS
    qDebug() << "redrawing, update rect:" << ev->rect() << ", frame flags" << dbgFlags;
#endif

    const QPoint tl(m_marginWidth, m_marginWidth);

    // Display the main image
    //

    // What should be updated on a screen somewhere on a place where the image is located
    auto imageDisplayRect = QRect(tl, m_scannedDocImageDisplaySize).intersected(ev->rect());

    if (! imageDisplayRect.isEmpty())
        painter.drawPixmap(imageDisplayRect, m_displayedPixmap, imageDisplayRect.translated(-tl));

    m_horzDashCursor.draw(painter, ev);
    m_vertDashCursor.draw(painter, ev);

    drawing::PlaneBase::draw(painter, ev);
}

void DrawingSurface::moveEvent(QMoveEvent* ev) {
    m_currentlyScrolledBy += ev->pos() - ev->oldPos();

    emit scannedDocImageMovedOnDisplay(ev->pos() + QPoint(m_marginWidth, m_marginWidth),
        ev->oldPos() + QPoint(m_marginWidth, m_marginWidth));
}

void DrawingSurface::resizeEvent(QResizeEvent* ev) {
    emit scannedDocImageDisplayGeometryChanged(
        QRect(
            QPoint(m_marginWidth, m_marginWidth),
            m_scannedDocImageDisplaySize));
}

#if QT_VERSION >= 0x060000
void DrawingSurface::enterEvent(QEnterEvent*) {
#else
void DrawingSurface::enterEvent(QEvent*) {
#endif
    m_cursorInWorkingArea = true;
    if (m_showDashCursor) {
        int xOnScan, yOnScan;
        const QPoint cursorPt = mapFromGlobal(QCursor::pos());
        m_horzDashCursor.enterWindow(cursorPt, yOnScan);
        m_vertDashCursor.enterWindow(cursorPt, xOnScan);
        emit dashedCursorPoint(xOnScan, yOnScan);
    }

    if (m_surfaceMouseOpsConsumer)
        m_surfaceMouseOpsConsumer->onSurfaceMouseEnterEvent(
            parentWidget()->mapFromGlobal(QCursor::pos()));
}

void DrawingSurface::mouseMoveEvent(QMouseEvent* event) {
    int xOnScan = 0, yOnScan = 0;
    bool moved = m_horzDashCursor.moveMouse(event, yOnScan);
    moved |= m_vertDashCursor.moveMouse(event, xOnScan);

    if (moved)
        emit dashedCursorPoint(xOnScan, yOnScan);

    if (m_surfaceMouseOpsConsumer)
        m_surfaceMouseOpsConsumer->onSurfaceMouseMoveEvent(
            parentWidget()->mapFromGlobal(QCursor::pos()));
}

void DrawingSurface::leaveEvent(QEvent*) {
    m_horzDashCursor.leaveWindow();
    m_vertDashCursor.leaveWindow();
    if (m_cursorInWorkingArea)
        emit dashedCursorPoint(-1, -1);
    m_cursorInWorkingArea = false;

    if (m_surfaceMouseOpsConsumer)
        m_surfaceMouseOpsConsumer->onSurfaceMouseLeaveEvent();
}

void DrawingSurface::redrawRullerZoneInner(bool isHorizontal, int startRedrawPos, int stopRedrawPos, int cursorPos) {
    emit redrawRullerZone(isHorizontal, startRedrawPos - m_marginWidth, stopRedrawPos - m_marginWidth, cursorPos - m_marginWidth);
}

QPair<int, int> DrawingSurface::DashedCursorLine::getCursorPosition(QPointF pos) const {
    auto singlePos = (m_direction == Direction::Horizontal)
        ? pos.y() : pos.x();
    auto imageSize = (m_direction == Direction::Horizontal)
        ? m_parentWindow->m_scannedDocImageDisplaySize.height() : m_parentWindow->m_scannedDocImageDisplaySize.width();

    if (singlePos < m_parentWindow->m_marginWidth)
        return {m_parentWindow->m_marginWidth, 0};
    else if (singlePos >= m_parentWindow->m_marginWidth + imageSize)
        return {m_parentWindow->m_marginWidth + imageSize, (int)std::round(imageSize / m_parentWindow->m_scale)};
    else {
        int v = std::round((singlePos - m_parentWindow->m_marginWidth) / m_parentWindow->m_scale);
        return {m_parentWindow->m_marginWidth + (int)std::round(v * m_parentWindow->m_scale), v};
    }
}

void DrawingSurface::DashedCursorLine::enterWindow(QPoint cursorPt, int& pointOnScan) {
    auto p = getCursorPosition(cursorPt);
    m_cursorPosition = p.first;
    pointOnScan = p.second;
    m_parentWindow->update(getCursorRect(m_cursorPosition, true));
    m_parentWindow->redrawRullerZoneInner(m_direction == Direction::Horizontal,
        m_cursorPosition - 1, m_cursorPosition, m_cursorPosition);
}

void DrawingSurface::DashedCursorLine::leaveWindow() {
    if (m_cursorPosition >= 0) {
        int old = m_cursorPosition;
        m_cursorPosition = -1;
        m_parentWindow->update(getCursorRect(old, true));
        m_parentWindow->redrawRullerZoneInner(m_direction == Direction::Horizontal, old - 1, old, m_cursorPosition);
    }
}

bool DrawingSurface::DashedCursorLine::moveMouse(QMouseEvent* event, int& pointOnScan) {
    if (m_cursorPosition == -1)
        return false;

#if QT_VERSION >= 0x060000
    auto p = getCursorPosition(event->position());
#else
    auto p = getCursorPosition(event->pos());
#endif
    int newPos = p.first;
    pointOnScan = p.second;
    if (newPos == m_cursorPosition)
        return false;

    QRegion rgn;
    int edge1 = -1, edge2;

    if (m_cursorPosition >= 0) {
        edge1 = m_cursorPosition;
        rgn += getCursorRect(m_cursorPosition, true);
    }

    m_cursorPosition = edge2 = newPos;
    rgn += getCursorRect(m_cursorPosition, true);

    if (edge1 == -1)
        edge1 = edge2;
    if (edge1 > edge2)
        std::swap(edge1, edge2);

    m_parentWindow->update(rgn);
    m_parentWindow->redrawRullerZoneInner(m_direction == Direction::Horizontal, edge1 - 1, edge2, m_cursorPosition);
    return true;
}

void DrawingSurface::DashedCursorLine::draw(QPainter& painter, QPaintEvent* ev) {
    if (m_cursorPosition >= 0) {
        auto r = getCursorRect(m_cursorPosition);
        if (! r.intersected(ev->rect()).isEmpty()) {
            painter.setPen(m_parentWindow->m_dashCursorPen);
            painter.drawLine(r.topLeft(), r.bottomRight());
        }
    }
}
