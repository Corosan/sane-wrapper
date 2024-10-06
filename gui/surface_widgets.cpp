#include "surface_widgets.h"

#include <QtGlobal>
#include <QDebug>

#include <cmath>
#include <algorithm>

namespace drawing {

void PlaneBase::insertWidget(drawing::IPlaneWidget& w, unsigned zOrder) {
    m_planeWidgets.insert(
        std::lower_bound(m_planeWidgets.begin(), m_planeWidgets.end(), zOrder,
            [](const auto& v, unsigned p){ return v.second <= p; }),
        qMakePair(&w, zOrder));

    w.setUpdatePlane(m_parentPlane);
}

void PlaneBase::removeWidget(drawing::IPlaneWidget& w) {
    auto it = std::find_if(m_planeWidgets.begin(), m_planeWidgets.end(),
        [&w](const auto& v){ return v.first == std::addressof(w); });

    if (it != m_planeWidgets.end()) {
        m_planeWidgets.erase(it);
        w.setUpdatePlane(nullptr);
    }
}

//-----------------------------------------------------------------------------
template <bool V>
QRect DashedCursorLineWidget<V>::getRect(int cursorPos, IUpdatePlane* plane, bool isForDrawn) {
    if (cursorPos >= 0) {
        QSize sz = plane->planeSize();
        QPoint scrolledBy = plane->visualOffset();

        // Anti-aliased drawing is made in the middle of two pixels - 0.5 of pixel is drawn
        // on a '-1' position, and 0.5 - on '0' position.
        if (s_isHorizontal)
            return {
                1 - scrolledBy.x(),
                cursorPos - scrolledBy.y() - (isForDrawn ? 0 : 1),
                sz.width() - 2,
                isForDrawn ? 1 : 2
            };
        else
            return {
                cursorPos - scrolledBy.x() - (isForDrawn ? 0 : 1),
                1 - scrolledBy.y(),
                isForDrawn ? 1 : 2,
                sz.height() - 2
            };
    }

    return {};
}

template <bool V>
void DashedCursorLineWidget<V>::draw(IUpdatePlane* pl, QPainter& p, QPaintEvent* ev, ...) {
    if (auto r = getRect(m_cursorPosition, pl, /*isForDrawn*/ true); ! r.isNull())
        if (! r.intersected(ev->rect()).isEmpty()) {
            p.setPen(m_dashCursorPen);
            p.drawLine(r.topLeft(), r.bottomRight());
        }
}

template <bool V>
void DashedCursorLineWidget<V>::onUpdatePlane(IUpdatePlane* old, IUpdatePlane* nw, ...) {
    IUpdatePlane* p = nw ? nw : old;
    if (auto r = getRect(m_cursorPosition, p, /*isForDrawn*/ false); ! r.isNull())
        p->invalidatePlane(r);
}

template <bool V>
void DashedCursorLineWidget<V>::setCursorPos(int v) {
    if (m_cursorPosition == v)
        return;

    const int oldCursorPosition = m_cursorPosition;
    m_cursorPosition = v;

    for (auto pl : this->m_planes) {
        auto rcOld = getRect(oldCursorPosition, *pl, /*isForDrawn*/ false);
        auto rcNew = getRect(m_cursorPosition, *pl, /*isForDrawn*/ false);
        if (rcOld.isNull() && ! rcNew.isNull())
            (*pl)->invalidatePlane(rcNew);
        else if (! rcOld.isNull() && rcNew.isNull())
            (*pl)->invalidatePlane(rcOld);
        else if (! rcOld.isNull() && ! rcNew.isNull())
            (*pl)->invalidatePlane(QRegion(rcOld) + rcNew);
    }
}

template class DashedCursorLineWidget<true>;
template class DashedCursorLineWidget<false>;

void DashedRectWidget::draw(IUpdatePlane* pl, QPainter& p, QPaintEvent* ev, SurfaceTag) {
    if (! m_rc.isValid())
        return;

    auto rc = m_rc.translated(-pl->visualOffset());
    if (! rc.intersected(ev->rect()).isEmpty()) {
        p.setPen(m_dashPen);
        p.drawRect(rc);
    }
}

static QRegion getUpdateRegionAroundRect(const QRect& rc) {
    if (rc.isNull())
        return {};

    QRegion rg{QRect{rc.x() - 1, rc.y() - 1, rc.width() + 2, 2}};
    rg += QRect{rc.x() + rc.width() - 1, rc.y() - 1, 2, rc.height() + 2};
    rg += QRect{rc.x() - 1, rc.y() - 1, 2, rc.height() + 2};
    rg += QRect{rc.x() - 1, rc.y() + rc.height() - 1, rc.width() + 2, 2};
    return rg;
}

void DashedRectWidget::onUpdatePlane(IUpdatePlane* old, IUpdatePlane* nw, SurfaceTag) {
    IUpdatePlane* p = nw ? nw : old;

    if (! m_rc.isValid())
        return;

    p->invalidatePlane(getUpdateRegionAroundRect(m_rc.translated(-p->visualOffset())));
}

void DashedRectWidget::setRect(const QRect& rc) {
    if (rc == m_rc)
        return;

    auto old = m_rc;
    m_rc = rc;
    m_plane->invalidatePlane(
        getUpdateRegionAroundRect(old) + getUpdateRegionAroundRect(m_rc));
}

//-----------------------------------------------------------------------------
DashedCursorController::DashedCursorController(IPlaneProvider& pp)
    : m_pp(&pp) {
    m_pp->getRullerLeftPlane().insertWidget(m_horzLine.getView<RullerLeftTag>(), 0);
    m_pp->getRullerRightPlane().insertWidget(m_horzLine.getView<RullerRightTag>(), 0);
    m_pp->getSurfacePlane().insertWidget(m_horzLine.getView<SurfaceTag>(), 0);
    m_pp->getRullerTopPlane().insertWidget(m_vertLine.getView<RullerTopTag>(), 0);
    m_pp->getRullerBottomPlane().insertWidget(m_vertLine.getView<RullerBottomTag>(), 0);
    m_pp->getSurfacePlane().insertWidget(m_vertLine.getView<SurfaceTag>(), 0);
}

DashedCursorController::~DashedCursorController() {
    // TODO: could be automated also
    m_pp->getRullerLeftPlane().removeWidget(m_horzLine.getView<RullerLeftTag>());
    m_pp->getRullerRightPlane().removeWidget(m_horzLine.getView<RullerRightTag>());
    m_pp->getSurfacePlane().removeWidget(m_horzLine.getView<SurfaceTag>());
    m_pp->getRullerTopPlane().removeWidget(m_vertLine.getView<RullerTopTag>());
    m_pp->getRullerBottomPlane().removeWidget(m_vertLine.getView<RullerBottomTag>());
    m_pp->getSurfacePlane().removeWidget(m_vertLine.getView<SurfaceTag>());
}

void DashedCursorController::setSurfaceScale(float v) {
    m_surfaceScale = v;
    recalcCross();
}

void DashedCursorController::setSurfaceImageRect(const QRect& rc) {
    m_surfaceRect = rc;
    recalcCross();
}

void DashedCursorController::onSurfaceMouseEnterEvent(QPoint localPos) {
    m_lastCursorPos = localPos;
    recalcCross();
}

void DashedCursorController::onSurfaceMouseMoveEvent(QPoint newLocalPos) {
    m_lastCursorPos = newLocalPos;
    recalcCross();
}

void DashedCursorController::onSurfaceMouseLeaveEvent() {
    m_lastCursorPos = {-1, -1};
    recalcCross();
}

void DashedCursorController::recalcCross() {
    if (m_lastCursorPos == QPoint{-1, -1}) {
        m_horzLine.setCursorPos(-1);
        m_vertLine.setCursorPos(-1);
        return;
    }

    int x = std::min(std::max(m_lastCursorPos.x(), m_surfaceRect.x()),
        m_surfaceRect.x() + m_surfaceRect.width());
    int y = std::min(std::max(m_lastCursorPos.y(), m_surfaceRect.y()),
        m_surfaceRect.y() + m_surfaceRect.height());

    auto old = m_surfaceScannedCoords;

    m_surfaceScannedCoords.setX(
        std::round((x - m_surfaceRect.x()) / m_surfaceScale));
    m_surfaceScannedCoords.setY(
        std::round((y - m_surfaceRect.y()) / m_surfaceScale));

    m_vertLine.setCursorPos(
        m_surfaceRect.x() + (int)std::round(m_surfaceScannedCoords.x() * m_surfaceScale));
    m_horzLine.setCursorPos(
        m_surfaceRect.y() + (int)std::round(m_surfaceScannedCoords.y() * m_surfaceScale));

    if (m_surfaceScannedCoords != old && m_scannedCoordsChangedCb)
        m_scannedCoordsChangedCb(m_surfaceScannedCoords);
}

} // ns drawing
