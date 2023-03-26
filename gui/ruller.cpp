#include "ruller.h"

#include <QPainter>
#include <QLine>
#include <QPaintEvent>
//#include <QMoveEvent>
//#include <QResizeEvent>

#include <QtGlobal>
#include <QDebug>

#include <cmath>
#include <limits>
#include <algorithm>

static const double g_InchMm = 25.4;

Ruller::Ruller(QWidget *parent)
    : QWidget{parent}
    , m_mmExp{std::numeric_limits<int>::min()} {

    setAutoFillBackground(true);
}

void Ruller::setParams(int picOffsetPx, int picSizePx, float dpi, float scale) {
    if (m_picOffsetPx == picOffsetPx
        && m_picSizePx == picSizePx
        && m_scannerDPI == dpi
        && m_picScale == scale)
        return;

    m_picOffsetPx = picOffsetPx;
    m_picSizePx = picSizePx;
    m_scannerDPI = dpi;
    m_picScale = scale;

    // calculate a power of 10 to get major stick step in 10^m_mmExp millimeters
    int fontSize = fontInfo().pixelSize();
    if (fontSize <= 0)
        fontSize = 10;

    m_mmExp = static_cast<int>(std::ceil(std::log(fontSize * 2 * g_InchMm / m_picScale / m_scannerDPI) / std::log(10.0)));

    update();

    qDebug() << "ruller [" << m_orientation << "]: offset =" << m_picOffsetPx
        << ", size =" << m_picSizePx << ", dpi =" << m_scannerDPI << ", scale =" << m_picScale;
}

void Ruller::scrollBy(int delta) {
    m_picOffsetPx += delta;

    if (m_orientation == Orientation::Top || m_orientation == Orientation::Bottom)
        scroll(delta, 0, QRect(1, 1, width() - 2, height() - 2));
    else
        scroll(0, delta, QRect(1, 1, width() - 2, height() - 2));
}

void Ruller::paintEvent(QPaintEvent* ev) {
    QPainter painter(this);

    // Draw border around the ruller
    //

    painter.setPen(QColor(64, 64, 64));
    painter.drawRect(0, 0, width() - 1, height() - 1);

    // Calculate constants for drawing the main ruller sticks
    //

    const double InchMm = 25.4;

    if (m_scannerDPI <= 0.0)
        // Can't draw any reasonable ruller without info about DPI
        return;

    painter.setPen(QColor(0, 0, 0));

    const double stepLabel = std::pow(10.0, m_mmExp <= 0 ? m_mmExp : m_mmExp - 1);
    const double stepL = m_scannerDPI * m_picScale / InchMm * std::pow(10.0, m_mmExp);
    const int maxPosPx = (m_orientation == Orientation::Top || m_orientation == Orientation::Bottom
        ? ev->rect().x() + ev->rect().width() : ev->rect().y() + ev->rect().height()) + static_cast<int>(stepL);
    const int minFullStickSetWidthPx = 40;
    QLine mainStickLine, subStickLine, smallStickLine;

    switch (m_orientation)
    {
    case Orientation::Top:
        mainStickLine = {0, height() - 2, 0, height() - 2 - 2 * height() / 3};
        subStickLine = {0, height() - 2, 0, height() - 2 - height() / 2};
        smallStickLine = {0, height() - 2, 0, height() - 2 - height() / 3};
        break;
    case Orientation::Bottom:
        mainStickLine = {0, 2, 0, 2 + 2 * height() / 3};
        subStickLine = {0, 2, 0, 2 + height() / 2};
        smallStickLine = {0, 2, 0, 2 + height() / 3};
        break;
    case Orientation::Left:
        mainStickLine = {width() - 2, 0, width() - 2 - 2 * width() / 3, 0};
        subStickLine = {width() - 2, 0, width() - 2 - width() / 2, 0};
        smallStickLine = {width() - 2, 0, width() - 2 - width() / 3, 0};
        break;
    case Orientation::Right:
        mainStickLine = {2, 0, 2 + 2 * width() / 3, 0};
        subStickLine = {2, 0, 2 + width() / 2, 0};
        smallStickLine = {2, 0, 2 + width() / 3, 0};
        break;
    }

    int k = m_orientation == Orientation::Top || m_orientation == Orientation::Bottom
        ? (ev->rect().x() >= m_picOffsetPx ? static_cast<int>((ev->rect().x() - m_picOffsetPx) / stepL) : 0)
        : (ev->rect().y() >= m_picOffsetPx ? static_cast<int>((ev->rect().y() - m_picOffsetPx) / stepL) : 0);

    bool run = true;

    // Draw major sticks, sub-sticks at 0.5 and minor sticks at 0.1 step
    //

    do {
        int posPx = m_picOffsetPx + static_cast<int>(k * stepL);

        if (posPx > maxPosPx)
            break;

        QLine l = (m_orientation == Orientation::Top || m_orientation == Orientation::Bottom)
            ? mainStickLine.translated(posPx, 0) : mainStickLine.translated(0, posPx);
        painter.drawLine(l);

        auto labelText = QLocale().toString(k * stepLabel);

        switch (m_orientation)
        {
        case Orientation::Top:
            painter.drawText(posPx + 2, fontInfo().pixelSize(), labelText);
            break;
        case Orientation::Bottom:
            painter.drawText(posPx + 2, height() - 3, labelText);
            break;
        case Orientation::Left:
            painter.drawText(2, posPx + 2 + fontInfo().pixelSize(), labelText);
            break;
        case Orientation::Right:
            painter.drawText(QRectF(0, posPx, width() - 2, fontInfo().pixelSize() + 4), labelText, QTextOption{Qt::AlignRight});
            break;
        }

        if (posPx >= m_picOffsetPx + m_picSizePx)
            break;

        if (stepL >= minFullStickSetWidthPx) {
            for (int i = 1; i < 10; ++i) {
                posPx = m_picOffsetPx + static_cast<int>((k + i / 10.0) * stepL);

                //if (posPx > maxPosPx) {
                //    run = false;
                //    break;
                //}

                l = (m_orientation == Orientation::Top || m_orientation == Orientation::Bottom)
                    ? (i == 5 ? subStickLine.translated(posPx, 0) : smallStickLine.translated(posPx, 0))
                    : (i == 5 ? subStickLine.translated(0, posPx) : smallStickLine.translated(0, posPx));

                painter.drawLine(l);

                //if (posPx >= m_picOffsetPx + m_picSizePx) {
                //    run = false;
                //    break;
                //}
            }
        } else {
            posPx = m_picOffsetPx + static_cast<int>((k + 0.5) * stepL);

            l = (m_orientation == Orientation::Top || m_orientation == Orientation::Bottom)
                ? smallStickLine.translated(posPx, 0) : smallStickLine.translated(0, posPx);

            painter.drawLine(l);
            //if (posPx >= m_picOffsetPx + m_picSizePx)
            //    break;
        }

        ++k;
    }
    while (run);
}
