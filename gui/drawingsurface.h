#pragma once

#include <QWidget>
#include <QPainter>

class DrawingSurface : public QWidget
{
    Q_OBJECT
public:
    explicit DrawingSurface(QWidget *parent = nullptr);

    QSize sizeHint() const override { return {500, 500}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.drawLine(10, 10, 490, 490);
    }

signals:

};
