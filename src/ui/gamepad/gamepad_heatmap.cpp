#include "ui/gamepad/gamepad_heatmap.h"
#include "core/stick_direction.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QToolTip>
#include <QLocale>
#include <algorithm>
#include <array>

GamepadHeatmap::GamepadHeatmap(QWidget* parent):QWidget(parent){
    setMinimumSize(640,380);setMouseTracking(true);setAccessibleName("手柄输入热力图");
    controls_={
        {"pad:LT","LT",{110,12,100,42}}, {"pad:RT","RT",{590,12,100,42}},
        {"pad:256","LB",{95,66,130,36}}, {"pad:512","RB",{575,66,130,36}},
        {"pad:LS_MOVE","左摇杆移动",{115,135,132,132},true},
        {"pad:64","L3",{154,179,54,44},true},
        {"pad:RS_MOVE","右摇杆移动",{465,265,132,132},true},
        {"pad:128","R3",{504,309,54,44},true},
        {"pad:1","↑",{298,252,44,44}}, {"pad:2","↓",{298,340,44,44}},
        {"pad:4","←",{254,296,44,44}}, {"pad:8","→",{342,296,44,44}},
        {"pad:32","Back",{295,165,72,36}}, {"pad:16","Start",{433,165,72,36}},
        {"pad:4096","A",{624,223,48,48},true}, {"pad:8192","B",{674,173,48,48},true},
        {"pad:16384","X",{574,173,48,48},true}, {"pad:32768","Y",{624,123,48,48},true}
    };
}
QRectF GamepadHeatmap::area()const{
    const double scale=std::min((width()-24)/800.0,(height()-24)/430.0);
    return {(width()-800*scale)/2,(height()-430*scale)/2,800*scale,430*scale};
}
QRectF GamepadHeatmap::mapped(const QRectF& b)const{const auto a=area();return {a.left()+b.left()*a.width()/800,a.top()+b.top()*a.height()/430,b.width()*a.width()/800,b.height()*a.height()/430};}
void GamepadHeatmap::setStatistics(const wi::ViewResult& result){
    total_=maximum_=0;directions_={};for(auto& control:controls_)control.count=0;
    for(const auto& row:result.controls)if(row.type=="gamepad"){
        total_+=row.count;const auto id=QString::fromStdString(row.control);
        if(id.startsWith("pad:LS_MOVE:")||id.startsWith("pad:RS_MOVE:")){
            bool valid=false;const int direction=id.section(':',2,2).toInt(&valid);
            if(valid&&direction>=0&&direction<16){auto& count=directions_[id.startsWith("pad:LS")?0:1][direction];count+=row.count;maximum_=std::max(maximum_,count);}
        }else for(auto& control:controls_)if(control.id==id){control.count+=row.count;maximum_=std::max(maximum_,control.count);}
    }
    update();
}
void GamepadHeatmap::paintEvent(QPaintEvent*){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    QPainterPath body;body.moveTo(130,110);body.cubicTo(40,125,35,305,55,398);body.cubicTo(65,450,165,423,218,359);body.lineTo(575,359);body.cubicTo(635,423,735,450,745,398);body.cubicTo(765,305,760,125,670,110);body.closeSubpath();
    p.save();auto a=area();p.translate(a.topLeft());p.scale(a.width()/800,a.height()/430);p.setPen(QPen(QColor("#546473"),2));p.setBrush(QColor("#263642"));p.drawPath(body);p.restore();
    const std::array<QColor,6> colors={QColor("#292747"),QColor("#155784"),QColor("#008d92"),QColor("#48a33c"),QColor("#d8bc26"),QColor("#d34132")};
    for(const auto& control:controls_){
        if(control.id.endsWith("_MOVE")){
            const auto r=mapped(control.rect);const auto& counts=directions_[control.id=="pad:LS_MOVE"?0:1];
            for(int direction=0;direction<16;++direction){
                const double value=maximum_?double(counts[direction])/maximum_*5:0;const int index=std::min(4,int(value));const double t=value-index;
                p.setBrush(QColor(int(colors[index].red()*(1-t)+colors[index+1].red()*t),int(colors[index].green()*(1-t)+colors[index+1].green()*t),int(colors[index].blue()*(1-t)+colors[index+1].blue()*t)));
                p.setPen(QPen(QColor("#758391"),0.7));
                p.drawPie(r,int((90-direction*22.5-11.25)*16),360);
            }
            p.setPen(QColor("#eef3f7"));p.drawText(QRectF(r.left()-10,r.top()-24,r.width()+20,22),Qt::AlignCenter,control.id=="pad:LS_MOVE"?"左摇杆":"右摇杆");
            continue;
        }
        const double value=maximum_?double(control.count)/maximum_*5:0;const int index=std::min(4,int(value));const double t=value-index;
        QColor color(int(colors[index].red()*(1-t)+colors[index+1].red()*t),int(colors[index].green()*(1-t)+colors[index+1].green()*t),int(colors[index].blue()*(1-t)+colors[index+1].blue()*t));
        auto r=mapped(control.rect);p.setPen(QPen(QColor("#758391"),1));p.setBrush(color);if(control.round)p.drawEllipse(r);else p.drawRoundedRect(r,6,6);
        p.setPen(QColor("#eef3f7"));auto font=p.font();font.setPixelSize(std::max(10,int(14*a.width()/800)));p.setFont(font);
        auto labelRect=r;if(control.id.endsWith("_MOVE")){labelRect.setTop(r.top()+r.height()*0.71);labelRect.setHeight(r.height()*0.23);}
        p.drawText(labelRect,Qt::AlignCenter,control.label);
    }
}
void GamepadHeatmap::mouseMoveEvent(QMouseEvent* event){
    for(auto it=controls_.rbegin();it!=controls_.rend();++it){auto rect=mapped(it->rect);bool inside=rect.contains(event->position());if(inside&&it->round){const auto d=event->position()-rect.center();inside=d.x()*d.x()/(rect.width()*rect.width()/4)+d.y()*d.y()/(rect.height()*rect.height()/4)<=1;}
        if(inside){
            auto label=it->label;auto count=it->count;
            if(it->id.endsWith("_MOVE")){
                const auto delta=event->position()-rect.center();const int direction=wi::stickDirection(delta.x()/rect.width(),-delta.y()/rect.height());
                count=directions_[it->id=="pad:LS_MOVE"?0:1][direction];label+=" · "+QString::fromUtf8(wi::stickDirectionName(direction))+QString("（%1°）").arg(direction*22.5);
            }
            QToolTip::showText(event->globalPosition().toPoint(),label+QString("\n%1 次 · %2%").arg(QLocale().toString(qlonglong(count))).arg(total_?100.0*count/total_:0,0,'f',2),this);return;}}
    QToolTip::hideText();
}
void GamepadHeatmap::leaveEvent(QEvent* event){QToolTip::hideText();QWidget::leaveEvent(event);}
