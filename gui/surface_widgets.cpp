#include "surface_widgets.h"

#include <QtGlobal>
#include <QDebug>

#include <cmath>
#include <cassert>
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
    : m_pp(pp) {
    m_pp.getRullerLeftPlane().insertWidget(m_horzLine.getView<RullerLeftTag>(), 0);
    m_pp.getRullerRightPlane().insertWidget(m_horzLine.getView<RullerRightTag>(), 0);
    m_pp.getSurfacePlane().insertWidget(m_horzLine.getView<SurfaceTag>(), 0);
    m_pp.getRullerTopPlane().insertWidget(m_vertLine.getView<RullerTopTag>(), 0);
    m_pp.getRullerBottomPlane().insertWidget(m_vertLine.getView<RullerBottomTag>(), 0);
    m_pp.getSurfacePlane().insertWidget(m_vertLine.getView<SurfaceTag>(), 0);
}

DashedCursorController::~DashedCursorController() {
    // TODO: could be automated also
    m_pp.getRullerLeftPlane().removeWidget(m_horzLine.getView<RullerLeftTag>());
    m_pp.getRullerRightPlane().removeWidget(m_horzLine.getView<RullerRightTag>());
    m_pp.getSurfacePlane().removeWidget(m_horzLine.getView<SurfaceTag>());
    m_pp.getRullerTopPlane().removeWidget(m_vertLine.getView<RullerTopTag>());
    m_pp.getRullerBottomPlane().removeWidget(m_vertLine.getView<RullerBottomTag>());
    m_pp.getSurfacePlane().removeWidget(m_vertLine.getView<SurfaceTag>());
}

void DashedCursorController::onSurfaceMouseEnterEvent(QPoint localPos) {
    m_lastCursorPos = localPos;
    visualUpdate();
}

void DashedCursorController::onSurfaceMouseMoveEvent(QPoint newLocalPos) {
    m_lastCursorPos = newLocalPos;
    visualUpdate();
}

void DashedCursorController::onSurfaceMouseLeaveEvent() {
    m_lastCursorPos = {-1, -1};
    visualUpdate();
}

void DashedCursorController::visualUpdate() {
    const auto old = m_surfaceScannedCoords;
    const auto oldVisual = m_visualCoords;

    if (m_lastCursorPos != QPoint{-1, -1}) {
        int x = std::min(std::max(m_lastCursorPos.x(), m_surfaceRect.x()),
            m_surfaceRect.x() + m_surfaceRect.width());
        int y = std::min(std::max(m_lastCursorPos.y(), m_surfaceRect.y()),
            m_surfaceRect.y() + m_surfaceRect.height());

        m_surfaceScannedCoords.setX(
            std::round((x - m_surfaceRect.x()) / m_surfaceScale));
        m_surfaceScannedCoords.setY(
            std::round((y - m_surfaceRect.y()) / m_surfaceScale));

        m_visualCoords.setX(
            m_surfaceRect.x() + (int)std::round(m_surfaceScannedCoords.x() * m_surfaceScale));
        m_visualCoords.setY(
            m_surfaceRect.y() + (int)std::round(m_surfaceScannedCoords.y() * m_surfaceScale));
    } else {
        m_surfaceScannedCoords = {-1, -1};
        m_visualCoords = {-1, -1};
    }

    m_vertLine.setCursorPos(m_visualCoords.x());
    m_horzLine.setCursorPos(m_visualCoords.y());

    if (m_scannedCoordsChangedCb && (m_surfaceScannedCoords != old || m_visualCoords != oldVisual))
        m_scannedCoordsChangedCb(m_surfaceScannedCoords, m_visualCoords);
}

//-----------------------------------------------------------------------------
RectSelectorController::RectSelectorController(IPlaneProvider& pp)
    : m_pp(pp)
    , m_dashedCursor(pp) {
    m_pp.getSurfacePlane().insertWidget(m_rectWidget.getView(), 1);
    m_dashedCursor.setScannedCoordsChangedCb([this](const auto& v1, const auto& v2){
            onScannedCoordsChanged(v1, v2);
        });
}

RectSelectorController::~RectSelectorController() {
    m_pp.getSurfacePlane().removeWidget(m_rectWidget.getView());
}

void RectSelectorController::keyPressEvent(QKeyEvent* ev) {
    if (ev->key() == Qt::Key_Control)
        m_isCtrlPressed = true;
}

void RectSelectorController::keyReleaseEvent(QKeyEvent* ev) {
    if (ev->key() == Qt::Key_Control)
        m_isCtrlPressed = false;
}

void RectSelectorController::onScannedCoordsChanged(const QPoint& scanned, const QPoint& visual) {
    m_lastScannedCoordsCursorPos = scanned;

    switch (m_state) {
    case State::SelectingPressed:
    case State::Selecting:
        if (scanned != QPoint{-1, -1}) {
            int x = std::min(m_startPoint.x(), scanned.x());
            int y = std::min(m_startPoint.y(), scanned.y());
            m_selectedScannedRect = {
                x,
                y,
                std::max(m_startPoint.x(), scanned.x()) - x,
                std::max(m_startPoint.y(), scanned.y()) - y
            };
            selectedRectVisualUpdate();
        }
        break;
    case State::Resizing:
        if (scanned != QPoint{-1, -1}) {
            int x, y;
            QPoint br = {(int)std::round(m_surfaceRect.width() / m_surfaceScale),
                (int)std::round(m_surfaceRect.height() / m_surfaceScale)};

            switch (m_resizeState) {
            case ResizeState::Left:
                x = std::min(scanned.x(), m_selectedScannedRect.right());
                m_selectedScannedRect = {
                    x,
                    m_selectedScannedRect.y(),
                    m_selectedScannedRect.right() + 1 - x,
                    m_selectedScannedRect.height()
                };
                break;
            case ResizeState::TopLeft:
                x = std::min(scanned.x(), m_selectedScannedRect.right());
                y = std::min(scanned.y(), m_selectedScannedRect.bottom());
                if (m_isCtrlPressed) {
                    float scale =
                        std::min({
                            std::max(
                                (float)(m_selectedScannedRect.right() + 1 - x) / m_selectedScannedRectBeforeEdit.width(),
                                (float)(m_selectedScannedRect.bottom() + 1 - y) / m_selectedScannedRectBeforeEdit.height()),
                            (float)(m_selectedScannedRect.right() + 1) / m_selectedScannedRectBeforeEdit.width(),
                            (float)(m_selectedScannedRect.bottom() + 1) / m_selectedScannedRectBeforeEdit.height()});
                    x = m_selectedScannedRect.right() + 1 - (int)std::round(m_selectedScannedRectBeforeEdit.width() * scale);
                    y = m_selectedScannedRect.bottom() + 1 - (int)std::round(m_selectedScannedRectBeforeEdit.height() * scale);
                    assert(x >= 0);
                    assert(y >= 0);
                }
                m_selectedScannedRect = {
                    x,
                    y,
                    m_selectedScannedRect.right() + 1 - x,
                    m_selectedScannedRect.bottom() + 1 - y
                };
                break;
            case ResizeState::Top:
                y = std::min(scanned.y(), m_selectedScannedRect.bottom());
                m_selectedScannedRect = {
                    m_selectedScannedRect.x(),
                    y,
                    m_selectedScannedRect.width(),
                    m_selectedScannedRect.bottom() + 1 - y
                };
                break;
            case ResizeState::TopRight:
                x = std::max(scanned.x(), m_selectedScannedRect.x() + 1);
                y = std::min(scanned.y(), m_selectedScannedRect.bottom());
                if (m_isCtrlPressed) {
                    float scale =
                        std::min({
                            std::max(
                                (float)(x - m_selectedScannedRect.x()) / m_selectedScannedRectBeforeEdit.width(),
                                (float)(m_selectedScannedRect.bottom() + 1 - y) / m_selectedScannedRectBeforeEdit.height()),
                            (float)(br.x() - m_selectedScannedRect.x()) / m_selectedScannedRectBeforeEdit.width(),
                            (float)(m_selectedScannedRect.bottom() + 1) / m_selectedScannedRectBeforeEdit.height()});
                    x = m_selectedScannedRect.x() + (int)round(m_selectedScannedRectBeforeEdit.width() * scale);
                    y = m_selectedScannedRect.bottom() + 1 - (int)round(m_selectedScannedRectBeforeEdit.height() * scale);
                    assert(x >= 0);
                    assert(y >= 0);
                }
                m_selectedScannedRect = {
                    m_selectedScannedRect.x(),
                    y,
                    x - m_selectedScannedRect.x(),
                    m_selectedScannedRect.bottom() + 1 - y
                };
                break;
            case ResizeState::Right:
                x = std::max(m_selectedScannedRect.x() + 1, scanned.x());
                m_selectedScannedRect = {
                    m_selectedScannedRect.x(),
                    m_selectedScannedRect.y(),
                    x - m_selectedScannedRect.x(),
                    m_selectedScannedRect.height()
                };
                break;
            case ResizeState::BottomRight:
                x = std::max(scanned.x(), m_selectedScannedRect.x() + 1);
                y = std::max(scanned.y(), m_selectedScannedRect.y() + 1);
                if (m_isCtrlPressed) {
                    float scale =
                        std::min({
                            std::max(
                                (float)(x - m_selectedScannedRect.x()) / m_selectedScannedRectBeforeEdit.width(),
                                (float)(y - m_selectedScannedRect.y()) / m_selectedScannedRectBeforeEdit.height()),
                            (float)(br.x() - m_selectedScannedRect.x()) / m_selectedScannedRectBeforeEdit.width(),
                            (float)(br.y() - m_selectedScannedRect.y()) / m_selectedScannedRectBeforeEdit.height()});
                    x = m_selectedScannedRect.x() + (int)round(m_selectedScannedRectBeforeEdit.width() * scale);
                    y = m_selectedScannedRect.y() + (int)round(m_selectedScannedRectBeforeEdit.height() * scale);
                    assert(x >= 0);
                    assert(y >= 0);
                }
                m_selectedScannedRect = {
                    m_selectedScannedRect.x(),
                    m_selectedScannedRect.y(),
                    x - m_selectedScannedRect.x(),
                    y - m_selectedScannedRect.y()
                };
                break;
            case ResizeState::Bottom:
                y = std::max(m_selectedScannedRect.y() + 1, scanned.y());
                m_selectedScannedRect = {
                    m_selectedScannedRect.x(),
                    m_selectedScannedRect.y(),
                    m_selectedScannedRect.width(),
                    y - m_selectedScannedRect.y()
                };
                break;
            case ResizeState::BottomLeft:
                x = std::min(scanned.x(), m_selectedScannedRect.right());
                y = std::max(scanned.y(), m_selectedScannedRect.y() + 1);
                if (m_isCtrlPressed) {
                    float scale =
                        std::min({
                            std::max(
                                (float)(m_selectedScannedRect.right() + 1 - x) / m_selectedScannedRectBeforeEdit.width(),
                                (float)(y - m_selectedScannedRect.y()) / m_selectedScannedRectBeforeEdit.height()),
                            (float)(m_selectedScannedRect.right() + 1) / m_selectedScannedRectBeforeEdit.width(),
                            (float)(br.y() - m_selectedScannedRect.y()) / m_selectedScannedRectBeforeEdit.height()});
                    x = m_selectedScannedRect.right() + 1 - (int)std::round(m_selectedScannedRectBeforeEdit.width() * scale);
                    y = m_selectedScannedRect.y() + (int)std::round(m_selectedScannedRectBeforeEdit.height() * scale);
                    assert(x >= 0);
                    assert(y >= 0);
                }
                m_selectedScannedRect = {
                    x,
                    m_selectedScannedRect.y(),
                    m_selectedScannedRect.right() + 1 - x,
                    y - m_selectedScannedRect.y()
                };
            default:
                break;
            }
            selectedRectVisualUpdate();
        }
        break;
    default:
        break;
    };

    if (m_cursorOrAreaChangedCb)
        m_cursorOrAreaChangedCb(m_lastScannedCoordsCursorPos, m_selectedScannedRect);
}

void RectSelectorController::onSurfaceMouseEnterEvent(QPoint localPos) {
    m_dashedCursor.onSurfaceMouseEnterEvent(localPos);
}

void RectSelectorController::onSurfaceMouseMoveEvent(QPoint localPos) {
    m_dashedCursor.onSurfaceMouseMoveEvent(localPos);

    switch (m_state) {
    case State::StartPressed:
        m_state = State::SelectingPressed;
        break;
    case State::Selected:
        if ((localPos - m_selectedDrawnRect.topLeft()).manhattanLength() < 8) {
            m_rectWidget.setCursorShape(Qt::SizeFDiagCursor);
            m_resizeState = ResizeState::TopLeft;
        } else if ((localPos - m_selectedDrawnRect.topRight() - QPoint(1, 0)).manhattanLength() < 8) {
            m_rectWidget.setCursorShape(Qt::SizeBDiagCursor);
            m_resizeState = ResizeState::TopRight;
        } else if ((localPos - m_selectedDrawnRect.bottomRight() - QPoint(1, 1)).manhattanLength() < 8) {
            m_rectWidget.setCursorShape(Qt::SizeFDiagCursor);
            m_resizeState = ResizeState::BottomRight;
        } else if ((localPos - m_selectedDrawnRect.bottomLeft() - QPoint(0, 1)).manhattanLength() < 8) {
            m_rectWidget.setCursorShape(Qt::SizeBDiagCursor);
            m_resizeState = ResizeState::BottomLeft;
        } else if (std::abs(localPos.x() - m_selectedDrawnRect.x()) < 4
                   && localPos.y() > m_selectedDrawnRect.y() && localPos.y() < m_selectedDrawnRect.bottom() + 1) {
            m_rectWidget.setCursorShape(Qt::SizeHorCursor);
            m_resizeState = ResizeState::Left;
        } else if (std::abs(localPos.x() - m_selectedDrawnRect.x() - m_selectedDrawnRect.width()) < 4
                   && localPos.y() > m_selectedDrawnRect.y() && localPos.y() < m_selectedDrawnRect.bottom() + 1) {
            m_rectWidget.setCursorShape(Qt::SizeHorCursor);
            m_resizeState = ResizeState::Right;
        } else if (std::abs(localPos.y() - m_selectedDrawnRect.y()) < 4
                   && localPos.x() > m_selectedDrawnRect.x() && localPos.x() < m_selectedDrawnRect.right() + 1) {
            m_rectWidget.setCursorShape(Qt::SizeVerCursor);
            m_resizeState = ResizeState::Top;
        } else if (std::abs(localPos.y() - m_selectedDrawnRect.y() - m_selectedDrawnRect.height()) < 4
                   && localPos.x() > m_selectedDrawnRect.x() && localPos.x() < m_selectedDrawnRect.right() + 1) {
            m_rectWidget.setCursorShape(Qt::SizeVerCursor);
            m_resizeState = ResizeState::Bottom;
        } else {
            m_rectWidget.setCursorShape(Qt::ArrowCursor);
            m_resizeState = ResizeState::None;
        }
        break;
    default:
        break;
    }
}

void RectSelectorController::onSurfaceMouseLeaveEvent() {
    m_dashedCursor.onSurfaceMouseLeaveEvent();
}

void RectSelectorController::onSurfaceMousePressEvent(QPoint localPos) {
    switch (m_state) {
    case State::Initial:
        if (m_lastScannedCoordsCursorPos != QPoint{-1, -1}) {
            m_startPoint = m_lastScannedCoordsCursorPos;
            m_selectedScannedRect = {};
            selectedRectVisualUpdate();
            m_state = State::StartPressed;

            if (m_cursorOrAreaChangedCb)
                m_cursorOrAreaChangedCb(m_lastScannedCoordsCursorPos, m_selectedScannedRect);
        }
        break;
    case State::Selecting:
        m_state = State::SelectingWaitRelease;
        break;
    case State::Selected:
        if (m_resizeState != ResizeState::None) {
            m_selectedScannedRectBeforeEdit = m_selectedScannedRect;
            m_state = State::Resizing;
        } else {
            m_state = State::Initial;
            onSurfaceMousePressEvent(localPos);
        }
        break;
    default:
        break;
    }
}

void RectSelectorController::onSurfaceMouseReleaseEvent(QPoint localPos) {
    switch (m_state) {
    case State::StartPressed:
        m_state = State::Selecting;
        break;
    case State::SelectingPressed:
    case State::SelectingWaitRelease:
    case State::Resizing:
        m_state = m_selectedScannedRect.isEmpty() ? State::Initial : State::Selected;
        break;
    default:
        break;
    }
}

void RectSelectorController::selectedRectVisualUpdate() {
    if (m_selectedScannedRect.isNull()) {
        m_rectWidget.setRect(QRect{});
        return;
    }

    // '-1' is required because {bottomRight} argument obeys old rules about 'right' and 'bottom'
    // positions which are '-1' from {x + width} or {y + height}. Thus internally QRect calculates
    // width and height by adding '1' to the second argument's parts.
    m_selectedDrawnRect = {
        QPoint{
            m_surfaceRect.x() + (int)std::round(m_selectedScannedRect.x() * m_surfaceScale),
            m_surfaceRect.y() + (int)std::round(m_selectedScannedRect.y() * m_surfaceScale)
        },
        QPoint{
            m_surfaceRect.x() + (int)std::round((m_selectedScannedRect.x() + m_selectedScannedRect.width()) * m_surfaceScale) - 1,
            m_surfaceRect.y() + (int)std::round((m_selectedScannedRect.y() + m_selectedScannedRect.height()) * m_surfaceScale) - 1
        }
    };

    m_rectWidget.setRect(m_selectedDrawnRect);
}

} // ns drawing
