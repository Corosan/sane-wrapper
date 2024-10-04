#pragma once

#include "surface_widgets.h"

#include <QWidget>
#include <QPen>

class Ruller
    : public QWidget, drawing::IUpdatePlane, public drawing::PlaneBase
{
public:
    enum class Position : char { Left, Top, Right, Bottom };
    Q_ENUM(Position)

private:
    Q_OBJECT

    void invalidatePlane(int x, int y, int w, int h) override { update(x, y, w, h); }
    void invalidatePlane(const QRect &rect) override { update(rect); }
    void invalidatePlane(const QRegion &rgn) override { update(rgn); }
    QSize planeSize() override { return size(); }
    QPoint visualOffset() override { return m_offsetToSurface; }

public:
    explicit Ruller(QWidget *parent = nullptr);

public slots:
    void setDashedCursorPen(const QPen& pen) {
        m_dashCursorPen = pen;
    }
    void setOrientation(Ruller::Position val) {
        m_orientation = val;
    }
    void setOffsetToSurface(QPoint v) {
        m_offsetToSurface = v;
    }

    /*!
     * \brief adopt current scaling vs DPI and relative position of scanned doc surface scaled
     *        to display pixels
     * \param picOffsetPx is an offset between the surface zero point and window edge (usually
     *        negative when scrolled)
     * \param picSizePx is a scanned doc surface's size in display pixels
     * \param scannerDPI is used for calculating real distance units to be shown (mm, cm)
     * \param scale is a multiplier for calculations from real scanned doc image into display pixels
     */
    void setParams(int picOffsetPx, int picSizePx, float scannerDPI, float scale);
    void scrollBy(int delta);

    void updateDashedCursor(int startDispSurfaceRedrawPos, int stopDispSurfaceRedrawPos, int cursorPos);

    bool isCm() const { return m_mmExp >= 1; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Position m_orientation = Position::Left;
    QPoint m_offsetToSurface;
    QPen m_dashCursorPen;
    int m_dashCursorPos = -1;
    int m_picOffsetPx;
    int m_picSizePx = 0;
    float m_scannerDPI;
    float m_picScale;
    int m_mmExp;
};
