#pragma once

#include <QWidget>

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
    void setOrientation(Orientation val) {
        m_orientation = val;
    }

    void setParams(int picOffsetPx, int picSizePx, float dpi, float scale);
    void scrollBy(int delta);

    bool isCm() const { return m_mmExp >= 1; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Orientation m_orientation = Orientation::Left;
    int m_picOffsetPx;
    int m_picSizePx = 0;
    float m_scannerDPI;
    float m_picScale;
    int m_mmExp;
};
