#include "drawingsurface.h"

#include <QPainter>
#include <QPaintEvent>
#include <QtGlobal>
#include <QBrush>
#include <QRadialGradient>
#include <QLinearGradient>
#include <QPoint>

#include <QtGlobal>
#include <QtDebug>

#include <cstring>
#include <cmath>
#include <algorithm>

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

    m_imageUpdateRect |= QRect(leftAffectedPx, i, affectedPxCount, i);
    return m_imageHolder->image().scanLine(i);
}

DrawingSurface::DrawingSurface(QWidget *parent)
    : QWidget{parent} {

    // TODO: for debugging only
    //m_mainImage = QImage(200, 200, QImage::Format_RGB32);
    //m_mainImage.fill(Qt::white);
    //m_scale = 2.0f;
    //recalcSize();
}

void DrawingSurface::setScale(float val) {
    if (val != m_scale) {
        m_scale = val;
        updateAll();
        emit scaleChanged(m_scale);
    }
}

void DrawingSurface::updateRect(QRect rect) {
    update(m_marginWidth + rect.x() * m_scale, m_marginWidth + rect.y() * m_scale,
        rect.width() * m_scale, rect.height() * m_scale);
}

void DrawingSurface::updateAll() {
    const QSize imageDisplaySize = m_mainImage.size() * m_scale;
    m_marginWidth = std::min({imageDisplaySize.width() / 2, imageDisplaySize.height() / 2, 32});
    m_size = {imageDisplaySize.width() + m_marginWidth * 2, imageDisplaySize.height() + m_marginWidth * 2};

    QGradientStops const grStops{{0, Qt::gray}, {1, Qt::white}};
    const auto gradWidth = m_marginWidth / 2;

    QRadialGradient gr0(m_marginWidth - 1, m_marginWidth - 1, gradWidth);
    gr0.setStops(grStops);
    m_segmentBrushes[0] = QBrush(gr0);

    QLinearGradient gr1(0, m_marginWidth - 1, 0, m_marginWidth - 1 - gradWidth);
    gr1.setStops(grStops);
    m_segmentBrushes[1] = QBrush(gr1);

    QRadialGradient gr2(m_size.width() - m_marginWidth, m_marginWidth - 1, gradWidth);
    gr2.setStops(grStops);
    m_segmentBrushes[2] = QBrush(gr2);

    QLinearGradient gr3(m_size.width() - m_marginWidth, 0, m_size.width() - m_marginWidth + gradWidth, 0);
    gr3.setStops(grStops);
    m_segmentBrushes[3] = QBrush(gr3);

    QRadialGradient gr4(m_size.width() - m_marginWidth, m_size.height() - m_marginWidth, gradWidth);
    gr4.setStops(grStops);
    m_segmentBrushes[4] = QBrush(gr4);

    QLinearGradient gr5(0, m_size.height() - m_marginWidth, 0, m_size.height() - m_marginWidth + gradWidth);
    gr5.setStops(grStops);
    m_segmentBrushes[5] = QBrush(gr5);

    QRadialGradient gr6(m_marginWidth - 1, m_size.height() - m_marginWidth, gradWidth);
    gr6.setStops(grStops);
    m_segmentBrushes[6] = QBrush(gr6);

    QLinearGradient gr7(m_marginWidth - 1, 0, m_marginWidth - 1 - gradWidth, 0);
    gr7.setStops(grStops);
    m_segmentBrushes[7] = QBrush(gr7);

    updateGeometry();
    update();
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

    const QSize imageDisplaySize = m_mainImage.size() * m_scale;

    // Display gradient borders around the main image
    //

    if (! QRect(0, 0, m_marginWidth, m_marginWidth).intersected(ev->rect()).isEmpty()) {
        painter.fillRect(0, 0, m_marginWidth, m_marginWidth, m_segmentBrushes[0]);
    }

    if (! QRect(m_marginWidth, 0, imageDisplaySize.width(), m_marginWidth).intersected(ev->rect()).isEmpty()) {
        painter.fillRect(m_marginWidth, 0, imageDisplaySize.width(), m_marginWidth, m_segmentBrushes[1]);
    }

    if (! QRect(m_size.width() - m_marginWidth, 0, imageDisplaySize.width(), m_marginWidth).intersected(ev->rect()).isEmpty()) {
        painter.fillRect(m_size.width() - m_marginWidth, 0, imageDisplaySize.width(), m_marginWidth, m_segmentBrushes[2]);
    }

    if (! QRect(m_size.width() - m_marginWidth, m_marginWidth, m_marginWidth, imageDisplaySize.height()).intersected(ev->rect()).isEmpty()) {
        painter.fillRect(m_size.width() - m_marginWidth, m_marginWidth, m_marginWidth, imageDisplaySize.height(), m_segmentBrushes[3]);
    }

    if (! QRect(m_size.width() - m_marginWidth, m_size.height() - m_marginWidth, m_marginWidth, m_marginWidth).intersected(ev->rect()).isEmpty()) {
        painter.fillRect(m_size.width() - m_marginWidth, m_size.height() - m_marginWidth, m_marginWidth, m_marginWidth, m_segmentBrushes[4]);
    }

    if (! QRect(m_marginWidth, m_size.height() - m_marginWidth, imageDisplaySize.width(), m_marginWidth).intersected(ev->rect()).isEmpty()) {
        painter.fillRect(m_marginWidth, m_size.height() - m_marginWidth, imageDisplaySize.width(), m_marginWidth, m_segmentBrushes[5]);
    }

    if (! QRect(0, m_size.height() - m_marginWidth, m_marginWidth, m_marginWidth).intersected(ev->rect()).isEmpty()) {
        painter.fillRect(0, m_size.height() - m_marginWidth, m_marginWidth, m_marginWidth, m_segmentBrushes[6]);
    }

    if (! QRect(0, m_marginWidth, m_marginWidth, imageDisplaySize.height()).intersected(ev->rect()).isEmpty()) {
        painter.fillRect(0, m_marginWidth, m_marginWidth, imageDisplaySize.height(), m_segmentBrushes[7]);
    }

    const QPoint tl(m_marginWidth, m_marginWidth);

    // Display the main image
    //

    // What should be updated on a screen somewhere on a place where the image is located
    auto imageDisplayRect = QRect(tl, imageDisplaySize).intersected(ev->rect());
    // Which part of original image should be taken for drawing it on a screen. Top-level point is shifted
    // to most top and most left place while been converted to integer. Size should converted up to 2 points
    // down-right in order to cover the whole space under image.
    QRect imageRect(dividePoints(imageDisplayRect.topLeft() - tl, m_scale, /*roundUp*/ false),
            dividePoints(imageDisplayRect.size(), m_scale, /*roundUp*/ true) + QSize(1, 1));
    // Let's guarantee that the part of an image doesn't exceed the image itself
    imageRect = m_mainImage.rect().intersected(imageRect);
    // Transform original image coordinates back to a screen coordinates
    imageDisplayRect = QRect(imageRect.topLeft() * m_scale + tl, imageRect.size() * m_scale);

    if (! imageDisplayRect.isEmpty()) {
        painter.drawImage(imageDisplayRect, m_mainImage, imageRect);
    }
}
