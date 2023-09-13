#include "ruller.h"

#include <QPainter>
#include <QLine>
#include <QPaintEvent>

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

void Ruller::setParams(int picOffsetPx, int picSizePx, float scannerDPI, float scale) {
    if (m_picOffsetPx == picOffsetPx
        && m_picSizePx == picSizePx
        && m_scannerDPI == scannerDPI
        && m_picScale == scale)
        return;

    m_picOffsetPx = picOffsetPx;
    m_picSizePx = picSizePx;
    m_scannerDPI = scannerDPI;
    m_picScale = scale;

    // calculate a power of 10 to get major stick step in 10^m_mmExp millimeters
    int fontSize = fontInfo().pixelSize();
    if (fontSize <= 0)
        fontSize = 10;

    m_mmExp = static_cast<int>(std::ceil(std::log(fontSize * 2 * g_InchMm / m_picScale / m_scannerDPI) / std::log(10.0)));

    update();
}

void Ruller::scrollBy(int delta) {
    m_picOffsetPx += delta;

    if (m_orientation == Orientation::Top || m_orientation == Orientation::Bottom)
        scroll(delta, 0, QRect(1, 1, width() - 2, height() - 2));
    else
        scroll(0, delta, QRect(1, 1, width() - 2, height() - 2));
}

void Ruller::updateDashedCursor(int startDispSurfaceRedrawPos, int stopDispSurfaceRedrawPos, int cursorPos) {
    m_dashCursorPos = cursorPos;

    if (m_orientation == Orientation::Top || m_orientation == Orientation::Bottom)
        update(startDispSurfaceRedrawPos + m_picOffsetPx, 1, stopDispSurfaceRedrawPos - startDispSurfaceRedrawPos + 1, height() - 2);
    else
        update(1, startDispSurfaceRedrawPos + m_picOffsetPx, width() - 2, stopDispSurfaceRedrawPos - startDispSurfaceRedrawPos + 1);
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

    if (m_scannerDPI > 0.0) {
        painter.setPen(QColor(0, 0, 0));

        const double stepLabel = std::pow(10.0, m_mmExp <= 0 ? m_mmExp : m_mmExp - 1);
        const double stepL = m_scannerDPI * m_picScale / InchMm * std::pow(10.0, m_mmExp);
        int k = m_orientation == Orientation::Top || m_orientation == Orientation::Bottom
            ? std::max((int)std::floor((ev->rect().x() - m_picOffsetPx) / stepL), 0)
            : std::max((int)std::floor((ev->rect().y() - m_picOffsetPx) / stepL), 0);
        const int KLimit = m_orientation == Orientation::Top || m_orientation == Orientation::Bottom
            ? (int)std::ceil(std::min(ev->rect().right() + 1 - m_picOffsetPx, m_picSizePx) / stepL)
            : (int)std::ceil(std::min(ev->rect().bottom() + 1 - m_picOffsetPx, m_picSizePx) / stepL);
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

        // Draw major sticks, sub-sticks at 0.5 and minor sticks at 0.1 step
        //

        for (; k <= KLimit; ++k) {
            int posPx = m_picOffsetPx + static_cast<int>(k * stepL);

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

            // Do not draw minor sticks after the last major stick
            if (k == KLimit)
                break;

            if (stepL >= minFullStickSetWidthPx) {
                for (int i = 1; i < 10; ++i) {
                    posPx = m_picOffsetPx + static_cast<int>((k + i / 10.0) * stepL);

                    l = (m_orientation == Orientation::Top || m_orientation == Orientation::Bottom)
                        ? (i == 5 ? subStickLine.translated(posPx, 0) : smallStickLine.translated(posPx, 0))
                        : (i == 5 ? subStickLine.translated(0, posPx) : smallStickLine.translated(0, posPx));

                    painter.drawLine(l);
                }
            } else {
                posPx = m_picOffsetPx + static_cast<int>((k + 0.5) * stepL);

                l = (m_orientation == Orientation::Top || m_orientation == Orientation::Bottom)
                    ? smallStickLine.translated(posPx, 0) : smallStickLine.translated(0, posPx);

                painter.drawLine(l);
            }
        }
    }

    if (m_dashCursorPos >= 0) {
        QRect r;
        if (m_orientation == Orientation::Top || m_orientation == Orientation::Bottom)
            r = {m_dashCursorPos + m_picOffsetPx, 1, 1, height() - 2};
        else
            r = {1, m_dashCursorPos + m_picOffsetPx, width() - 2, 1};

        if (! r.intersected(ev->rect()).isEmpty()) {
            painter.setPen(m_dashCursorPen);
            painter.drawLine(r.topLeft(), r.bottomRight());
        }
    }
}
