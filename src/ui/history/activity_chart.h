#pragma once
#include "engine/engine.h"
#include <QWidget>
#include <QColor>
#include <QRectF>
class ActivityChart : public QWidget {
public:
    explicit ActivityChart(QWidget* parent=nullptr);
    void setData(const wi::ViewResult& result);
    void setSeriesVisible(int series,bool visible);
    static QColor seriesColor(int series);
    static QString seriesName(int series);
protected:
    void paintEvent(QPaintEvent*)override;
    void mouseMoveEvent(QMouseEvent*)override;
    void leaveEvent(QEvent*)override;
private:
    std::vector<wi::HourCount> bars_;
    wi::Count start_=0,end_=1;
    std::array<bool,4> visible_={true,true,true,true};
    QRectF plot()const;
    double maximum()const;
    QRectF barRect(const wi::HourCount& bar,int series,double maximum)const;
};
