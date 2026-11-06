#pragma once
#include <QPainter>
#include <QLocale>
#include <algorithm>
#include <array>
#include <cmath>

inline QColor heatColor(double value) {
    static const std::array<QColor,6> colors={QColor("#233440"),QColor("#205984"),QColor("#157e88"),QColor("#438951"),QColor("#ad9236"),QColor("#c34e3c")};
    const double position=std::clamp(value,0.0,1.0)*5;
    const int index=std::min(4,int(position));const double t=position-index;
    return QColor::fromRgbF(colors[index].redF()*(1-t)+colors[index+1].redF()*t,colors[index].greenF()*(1-t)+colors[index+1].greenF()*t,colors[index].blueF()*(1-t)+colors[index+1].blueF()*t);
}
inline void paintHeatLegend(QPainter& p,int width,double maximum,const QString& unit="次") {
    p.save();const int left=std::max(12,width-340);
    QLinearGradient gradient(left+35,0,left+205,0);
    for(int i=0;i<=10;++i)gradient.setColorAt(i/10.0,heatColor(i/10.0));
    p.setPen(QColor("#b9cbd5"));p.drawText(QRect(left,8,30,24),Qt::AlignVCenter,"0");
    p.fillRect(QRect(left+35,14,170,10),gradient);
    p.drawText(QRect(left+215,8,120,24),Qt::AlignVCenter,QLocale().toString(maximum,'f',maximum==std::floor(maximum)?0:2)+" "+unit);
    p.restore();
}
