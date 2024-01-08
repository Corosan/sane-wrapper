#pragma once

#include <QSize>
#include <QRect>
#include <QWidget>
#include <QImage>
#include <QPixmap>
#include <QBrush>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QPair>

struct IImageHolder {
    /*!
     * \brief The class for making elementary modifications on an internal image representation and
     *        tracking what should be updated later
     *
     * The interface is intended to be used by an image capturer getting data from a scanner device.
     * A capturer can set initial underlying image when it knows image properties like color depth.
     * It can modify raw scan lines of the underlying image, change its height (if precise height
     * is unknown initially).
     */
    class ImageModifier {
        static constexpr int s_growHeight = 32;

    public:
        ImageModifier() = default;

        ImageModifier(ImageModifier&& r)
            : m_imageHolder{r.m_imageHolder}
            , m_imageUpdateRect{std::move(r.m_imageUpdateRect)}
            , m_doUpdateAll{r.m_doUpdateAll} {
            r.m_imageHolder = nullptr;
        }

        ~ImageModifier() {
            if (m_imageHolder) {
                if (m_doUpdateAll)
                    m_imageHolder->recalcImageGeometry();
                else
                    m_imageHolder->redrawImageRect(m_imageUpdateRect);
            }
        }

        int height() const {
            return m_imageHolder->image().height();
        }
        int width() const {
            return m_imageHolder->image().width();
        }
        void setImage(QImage img) {
            m_imageHolder->image() = img;
            m_doUpdateAll = true;
        }
        void setHeight(int height);
        unsigned char* scanLine(int i, int leftAffectedPx, int affectedPxCount);

    private:
        friend IImageHolder;

        IImageHolder* m_imageHolder = nullptr;
        QRect m_imageUpdateRect;
        bool m_doUpdateAll = false;

        ImageModifier(IImageHolder& imageHolder)
            : m_imageHolder{&imageHolder} {
        }
    };

    virtual ~IImageHolder() = default;

    virtual ImageModifier modifier() {
        return {*this};
    }

protected:
    virtual QImage& image() = 0;

    /*!
     * \brief an implementer must guarantee a visual space displaying specified rect is updated
     */
    virtual void redrawImageRect(const QRect&) = 0;

    /*!
     * \brief an implementer must guarantee a whole virtual plane is redrawn - the image is
     *        resized
     */
    virtual void recalcImageGeometry() = 0;
};

/*!
 * \brief A scanned doc image holder and displaying widget with scrolling support
 */
class DrawingSurface : public QWidget, public IImageHolder {
    Q_OBJECT

    Q_PROPERTY(float scale READ scale WRITE setScale NOTIFY scaleChanged)
    Q_PROPERTY(bool showDashCursor READ dashCursorShown WRITE showDashCursor NOTIFY dashCursorVisibilityChanged)

public:
    explicit DrawingSurface(QWidget *parent = nullptr);

    const QPen& getDashCursorPen() const { return m_dashCursorPen; }
    QSize sizeHint() const override { return m_thisSurfaceSize; }
    const QImage& getImage() const { return m_scannedDocImage; }

    // Available operations on the underlying image

    void mirror(bool isVertical);
    void rotate(bool isClockWise);

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void moveEvent(QMoveEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    class DashedCursorLine {
    public:
        enum class Direction { Horizontal, Vertical };

        explicit DashedCursorLine(DrawingSurface* parentWindow, Direction direction)
            : m_parentWindow(parentWindow)
            , m_direction(direction) {
        }

        void enterWindow(QPoint, int&);
        void leaveWindow();
        bool moveMouse(QMouseEvent*, int&);
        void draw(QPainter&, QPaintEvent*);

    private:
        DrawingSurface* const m_parentWindow;
        const Direction m_direction;
        int m_cursorPosition = -1;

        QRect getCursorRect(int pos, bool bounding = false) const;
        QPair<int, int> getCursorPosition(QPointF) const;
    };

    /*!
     * \brief the main storage for an image being scanned
     */
    QImage m_scannedDocImage;

    /*!
     * \brief the scanned image prepared to be displayed (transformed and scaled accordingly)
     */
    QPixmap m_displayedPixmap;
    QSize m_scannedDocImageDisplaySize;
    QSize m_thisSurfaceSize;
    float m_scale = 1.0f;
    int m_marginWidth = 0;
    bool m_showDashCursor = false;
    bool m_cursorInWorkingArea = false;

    QBrush m_segmentBrushes[8];
    QPen m_dashCursorPen;
    DashedCursorLine m_horzDashCursor, m_vertDashCursor;

    // IImageHolder interface implementation

    QImage& image() override final { return m_scannedDocImage; }
    void redrawImageRect(const QRect& r) override final { redrawScannedDocImage(r); }
    void recalcImageGeometry() override final { recalcScannedDocImageGeometry(); }

    // internal methods

    void redrawScannedDocImage(const QRect&);
    void recalcScannedDocImageGeometry();
    void redrawRullerZoneInner(bool, int, int, int);

public slots:
    float scale() const { return m_scale; }
    void setScale(float);
    bool dashCursorShown() const { return m_showDashCursor; }
    void showDashCursor(bool);

signals:
    void scaleChanged(float);
    void dashCursorVisibilityChanged(bool);

    /*!
     * \param pNew is a new point inside the drawing surface's scroll area where
     *             the scanned image is displayed after move
     * \param pOld is an old point inside the drawing surface's scroll area where
     *             the scanned image was displayed
     */
    void scannedDocImageMovedOnDisplay(QPoint pNew, QPoint pOld);

    /*!
     * \param r is a rectangular where the scanned image is located now inside
     *          the scroll area of the drawing surface. r.x() and r.y() are negative
     *          if the image is scrolled somewhere up and left.
     */
    void scannedDocImageDisplayGeometryChanged(QRect r);

    /*!
     * \brief request rullers to redraw their part in order to display a dashed line cursor
     *        on a new position. All coordinates - realative to scanned doc surface display position
     *        (so 0,0 denotes upper-left corner of the scanned doc surface).
     *
     * \param isHorizontal denotes horizontal dashed line
     */
    void redrawRullerZone(bool isHorizontal, int startRedrawPos, int stopRedrawPos, int cursorPos);

    void dashedCursorPoint(int xPxOnScan, int yPxOnScan);
};


inline QRect DrawingSurface::DashedCursorLine::getCursorRect(int pos, bool bounding) const {
    // Anti-aliased renderer tries to draw line between -0.5 ... 0.5 of current position, so getting
    // rect for paint invalidation should include -0.5 ... 0 part.
    return (m_direction == Direction::Horizontal)
        ? QRect(0, bounding ? pos - 1 : pos, m_parentWindow->m_thisSurfaceSize.width(), bounding ? 2 : 1)
        : QRect(bounding ? pos - 1 : pos, 0, bounding ? 2 : 1, m_parentWindow->m_thisSurfaceSize.height());
}
