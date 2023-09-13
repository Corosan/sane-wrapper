#pragma once

#include <QWidget>
#include <QPen>

class Ruller : public QWidget
{
public:
    enum class Orientation : char { Left, Top, Right, Bottom };
    Q_ENUM(Orientation)

private:
    Q_OBJECT

public:
    explicit Ruller(QWidget *parent = nullptr);

public slots:
    void setDashedCursorPen(const QPen& pen) {
        m_dashCursorPen = pen;
    }
    void setOrientation(Orientation val) {
        m_orientation = val;
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
    Orientation m_orientation = Orientation::Left;
    QPen m_dashCursorPen;
    int m_dashCursorPos = -1;
    int m_picOffsetPx;
    int m_picSizePx = 0;
    float m_scannerDPI;
    float m_picScale;
    int m_mmExp;
};
