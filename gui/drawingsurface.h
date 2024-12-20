#pragma once

#include "surface_widgets.h"

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

public:
    using QWidget::QWidget;

    QSize sizeHint() const override { return m_thisSurfaceSize; }
    const QImage& getImage() const { return m_scannedDocImage; }
    QRect scannedDocImageDisplayGeometry() const {
        return {QPoint(m_marginWidth, m_marginWidth), m_scannedDocImageDisplaySize};
    }

    // Available operations on the underlying image

    void mirror(bool isVertical);
    void rotate(bool isClockWise);
    void crop(const QRect& scannedRc);

private:
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

    QBrush m_segmentBrushes[8];

    QPoint m_currentlyScrolledBy;

    // IImageHolder interface implementation

    QImage& image() override final { return m_scannedDocImage; }
    void redrawImageRect(const QRect& r) override final { redrawScannedDocImage(r); }
    void recalcImageGeometry() override final { recalcScannedDocImageGeometry(); }

    // QWidget overrides

    void paintEvent(QPaintEvent*) override;
    void moveEvent(QMoveEvent*) override;
    void resizeEvent(QResizeEvent*) override;

    // internal methods

    void redrawScannedDocImage(const QRect&);
    void recalcScannedDocImageGeometry();

public slots:
    float scale() const { return m_scale; }
    void setScale(float);

signals:
    void scaleChanged(float);

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
};
