#pragma once

#include <QSize>
#include <QRect>
#include <QWidget>
#include <QImage>
#include <QBrush>

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
                    m_imageHolder->updateAll();
                else
                    m_imageHolder->updateRect(m_imageUpdateRect);
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
    virtual void updateRect(QRect) = 0;

    /*!
     * \brief an implementer must guarantee a whole virtual plane is redrawn because the image is
     *        resized
     */
    virtual void updateAll() = 0;
};


class DrawingSurface : public QWidget, public IImageHolder
{
    Q_OBJECT
    Q_PROPERTY(float scale READ getScale WRITE setScale NOTIFY scaleChanged)

public:
    explicit DrawingSurface(QWidget *parent = nullptr);

    QSize sizeHint() const override {
        return m_size;
    }

protected:
    void paintEvent(QPaintEvent*) override;
    void moveEvent(QMoveEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QImage m_mainImage;
    QSize m_size;
    float m_scale = 1.0f;
    int m_marginWidth = 0;
    QBrush m_segmentBrushes[8];

    QImage& image() override {
        return m_mainImage;
    }
    void updateRect(QRect) override;
    void updateAll() override;

public slots:
    float getScale() const {
        return m_scale;
    }
    void setScale(float);

signals:
    void scaleChanged(float);
    void mainImageMoved(QPoint, QPoint);
    void mainImageGeometryChanged(QRect);
};
