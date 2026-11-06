#include "ui/history/activity_chart.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <QLocale>
#include <algorithm>
#include <cmath>

ActivityChart::ActivityChart(QWidget* parent):QWidget(parent){setMouseTracking(true);setMinimumHeight(200);setAccessibleName("按时间分组的输入活动堆叠柱状图");}
QColor ActivityChart::seriesColor(int series){static const char* colors[]={"#a9edc8","#e3ac35","#568bdf","#bc83da"};return QColor(colors[series]);}
QString ActivityChart::seriesName(int series){return QStringList{"总和","鼠标","键盘","手柄"}[series];}
void ActivityChart::setSeriesVisible(int series,bool visible){visible_[series]=visible;QToolTip::hideText();update();}
void ActivityChart::setData(const wi::ViewResult& result){
    bars_=result.hours;start_=result.rangeStart;end_=std::max(start_+1,result.rangeEnd);
    setMinimumWidth(0);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);update();
}
QRectF ActivityChart::plot()const{return QRectF(80,20,std::max(1,width()-104),std::max(1,height()-82));}
double ActivityChart::maximum()const{
    double value=1;for(const auto& bar:bars_){double sum=0;for(int series=1;series<4;++series)if(visible_[series])sum+=bar.series[series];value=std::max(value,sum);}
    const double scale=std::pow(10.0,std::floor(std::log10(value)));
    return std::ceil(value*1.08/scale*2)/2*scale;
}
QRectF ActivityChart::barRect(const wi::HourCount& bar,int series,double max)const{
    const auto a=plot();const double span=double(end_-start_);
    const double left=a.left()+double(bar.start-start_)/span*a.width(),right=a.left()+double(bar.end-start_)/span*a.width();
    const double groupWidth=std::max(0.1,right-left),slot=std::min(64.0,groupWidth*0.64);
    const double height=double(bar.series[series])/max*a.height();
    double below=0;for(int i=1;i<series;++i)if(visible_[i])below+=bar.series[i];
    return {left+(groupWidth-slot)/2,a.bottom()-height-below/max*a.height(),std::max(0.1,slot),height};
}
void ActivityChart::paintEvent(QPaintEvent* event){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#17232e"));
    const auto a=plot();const auto max=maximum();auto locale=QLocale(QLocale::Chinese);
    for(int i=0;i<=4;++i){const double y=a.bottom()-i*a.height()/4;
        p.setPen(QColor("#334451"));p.drawLine(QPointF(a.left(),y),QPointF(a.right(),y));p.setPen(QColor("#c0ccd5"));
        p.drawText(QRectF(0,y-10,a.left()-10,20),Qt::AlignRight|Qt::AlignVCenter,locale.toString(qlonglong(std::llround(max*i/4))));}
    p.setPen(QColor("#91a7b7"));p.drawLine(a.bottomLeft(),a.topLeft());p.drawLine(a.bottomLeft(),a.bottomRight());
    const int stride=std::max(1,int(std::ceil(double(bars_.size())/std::max(1,int(a.width()/95)))));
    for(int i=0;i<int(bars_.size());i+=stride){const auto& bar=bars_[i];const double x=a.left()+double(bar.start-start_+(bar.end-bar.start)/2)/double(end_-start_)*a.width();const auto time=bar.start;
        p.drawLine(QPointF(x,a.bottom()),QPointF(x,a.bottom()+5));const double labelWidth=140;
        const double left=std::clamp(x-labelWidth/2,0.0,double(width())-labelWidth);
        const auto format=bar.end-bar.start<80000?"MM/dd\nHH:mm":bar.end-bar.start>30000000?"yyyy":bar.end-bar.start>2400000?"yyyy/MM":"MM/dd";
        p.drawText(QRectF(left,a.bottom()+10,labelWidth,44),Qt::AlignHCenter|Qt::AlignTop,QDateTime::fromSecsSinceEpoch(time).toLocalTime().toString(format));}
    for(const auto& bar:bars_)for(int series=1;series<4;++series)if(visible_[series]&&bar.series[series]>0){
        const auto bounds=barRect(bar,series,max);if(!bounds.intersects(event->rect()))continue;p.setPen(Qt::NoPen);p.setBrush(seriesColor(series));p.drawRect(bounds);}
    if(bars_.empty()){p.setPen(QColor("#91a7b7"));p.drawText(a,Qt::AlignCenter,"此范围内暂无活动");}
}
void ActivityChart::mouseMoveEvent(QMouseEvent* event){
    const auto max=maximum();for(const auto& bar:bars_)for(int series=1;series<4;++series)if(visible_[series]&&bar.series[series]>0&&barRect(bar,series,max).contains(event->position())){
        const auto start=QDateTime::fromSecsSinceEpoch(bar.start).toLocalTime().toString("yyyy-MM-dd HH:mm:ss t");
        const auto end=QDateTime::fromSecsSinceEpoch(bar.end).toLocalTime().toString("yyyy-MM-dd HH:mm:ss t");
        QToolTip::showText(event->globalPosition().toPoint(),start+" → "+end+"\n"+seriesName(series)+"："+QLocale().toString(qlonglong(bar.series[series]))+" 次\n总输入："+QLocale().toString(qlonglong(bar.series[0]))+" 次",this);return;}
    QToolTip::hideText();
}
void ActivityChart::leaveEvent(QEvent* event){QToolTip::hideText();QWidget::leaveEvent(event);}
