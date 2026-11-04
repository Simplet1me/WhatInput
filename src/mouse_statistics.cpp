#include "mouse_statistics.h"
#include <QDialog>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QPainter>
#include <QMouseEvent>
#include <QToolTip>
#include <QToolButton>
#include <QSlider>
#include <QComboBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QCursor>
#include <QFrame>
#include <QSet>
#include <QImage>
#include <QResizeEvent>
#include <QLocale>
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <limits>

namespace {
QString number(wi::Count n){return QLocale(QLocale::Chinese).toString(qlonglong(n));}
QString screenKey(const wi::MousePosition& s){return QString::fromStdString(s.screenIdentity)+QString("|%1,%2,%3,%4").arg(s.left).arg(s.top).arg(s.width).arg(s.height);}
QColor heatColor(double value){
    const std::array<QColor,6> colors={QColor("#101187"),QColor("#0758ab"),QColor("#009f91"),QColor("#49bd48"),QColor("#e4e42c"),QColor("#f02b1c")};
    const double position=std::clamp(value,0.0,1.0)*5;const int index=std::min(4,int(position));const double t=position-index;
    return QColor::fromRgbF(colors[index].redF()*(1-t)+colors[index+1].redF()*t,colors[index].greenF()*(1-t)+colors[index+1].greenF()*t,colors[index].blueF()*(1-t)+colors[index+1].blueF()*t);
}
QString displayName(QString name){name.remove("\\\\.\\");return name;}
struct HeatPoint {double x=0,y=0;wi::Count count=0;};
std::vector<HeatPoint> clusterPoints(std::vector<wi::ViewResult::MousePoint> points,int radius){
    // Strongest points seed fixed-radius clusters. A spatial hash limits each
    // lookup to nine neighboring cells; raw coordinates remain untouched.
    std::sort(points.begin(),points.end(),[](const auto& a,const auto& b){return a.count!=b.count?a.count>b.count:std::tie(a.x,a.y,a.button)<std::tie(b.x,b.y,b.button);});
    struct Cluster {int anchorX,anchorY;long double weightedX=0,weightedY=0;wi::Count count=0;};
    std::vector<Cluster> clusters;
    std::unordered_map<std::uint64_t,std::vector<size_t>> cells;
    auto cellKey=[](int x,int y){return (std::uint64_t(std::uint32_t(x))<<32)|std::uint32_t(y);};
    const double radiusSquared=double(radius)*radius;
    for(const auto& point:points){
        const int x=point.x/radius,y=point.y/radius;size_t selected=std::numeric_limits<size_t>::max();double nearest=radiusSquared+1;
        for(int cy=y-1;cy<=y+1;++cy)for(int cx=x-1;cx<=x+1;++cx){
            auto found=cells.find(cellKey(cx,cy));if(found==cells.end())continue;
            for(auto index:found->second){const double dx=double(point.x)-clusters[index].anchorX,dy=double(point.y)-clusters[index].anchorY,distance=dx*dx+dy*dy;
                if(distance<=radiusSquared&&distance<nearest){nearest=distance;selected=index;}
            }
        }
        if(selected==std::numeric_limits<size_t>::max()){selected=clusters.size();clusters.push_back({point.x,point.y});cells[cellKey(x,y)].push_back(selected);}
        auto& cluster=clusters[selected];cluster.weightedX+=static_cast<long double>(point.x)*point.count;cluster.weightedY+=static_cast<long double>(point.y)*point.count;cluster.count+=point.count;
    }
    std::vector<HeatPoint> result;result.reserve(clusters.size());
    for(const auto& cluster:clusters)if(cluster.count>0)result.push_back({double(cluster.weightedX/cluster.count),double(cluster.weightedY/cluster.count),cluster.count});
    return result;
}
}
class MouseHeatmapTile : public QWidget {
public:
    wi::ViewResult::MouseScreen data;
    std::function<void()> clicked;
    bool fullscreen=false;
    bool showOriginalPoints=false;
    double heatOpacity=1.0;
    int buttonFilter=-1,clusterRadius=24;
    explicit MouseHeatmapTile(QWidget* parent=nullptr):QWidget(parent){setMinimumSize(300,220);setMouseTracking(true);setCursor(Qt::PointingHandCursor);}
    void setData(const wi::ViewResult::MouseScreen& screen){
        const bool changed=data.screen.width!=screen.screen.width||data.screen.height!=screen.screen.height||data.points!=screen.points;
        data=screen;if(changed)rebuild();update();
    }
    void setFilter(int button,int radius){
        radius=std::clamp(radius,1,200);if(buttonFilter==button&&clusterRadius==radius)return;
        buttonFilter=button;clusterRadius=radius;if(data.screen.width>0)rebuild();update();
    }
protected:
    QImage image_;
    std::vector<wi::ViewResult::MousePoint> filtered_;
    std::vector<HeatPoint> clusters_;
    void rebuild(){
        filtered_.clear();
        for(const auto& point:data.points)if(point.button==buttonFilter||(buttonFilter==-1&&(point.button==0||point.button==1)))filtered_.push_back(point);
        clusters_=clusterPoints(filtered_,clusterRadius);render();
    }
    QRectF mapRect()const{
        QRectF available=fullscreen?QRectF(rect()):QRectF(rect()).adjusted(8,8,-8,-8);
        const double ratio=double(std::max(1,data.screen.width))/std::max(1,data.screen.height);
        double w=available.width(),h=w/ratio;if(h>available.height()){h=available.height();w=h*ratio;}
        return {available.center().x()-w/2,available.center().y()-h/2,w,h};
    }
    void render(){
        const double scale=std::min(480.0/std::max(1,data.screen.width),270.0/std::max(1,data.screen.height));
        const int w=std::max(16,int(data.screen.width*scale)),h=std::max(16,int(data.screen.height*scale));
        std::vector<double> source(size_t(w)*h),horizontal(source.size()),blurred(source.size());
        for(const auto& point:clusters_){const int x=std::clamp(int(point.x*w/std::max(1,data.screen.width)),0,w-1),y=std::clamp(int(point.y*h/std::max(1,data.screen.height)),0,h-1);source[size_t(y)*w+x]+=double(point.count);}
        constexpr int radius=8;std::array<double,2*radius+1> kernel{};for(int k=-radius;k<=radius;++k)kernel[k+radius]=std::exp(-double(k*k)/18.0);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int k=-radius;k<=radius;++k)if(x+k>=0&&x+k<w)horizontal[size_t(y)*w+x]+=source[size_t(y)*w+x+k]*kernel[k+radius];
        double maximum=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto& value=blurred[size_t(y)*w+x];for(int k=-radius;k<=radius;++k)if(y+k>=0&&y+k<h)value+=horizontal[size_t(y+k)*w+x]*kernel[k+radius];maximum=std::max(maximum,value);}
        image_=QImage(w,h,QImage::Format_RGB32);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){const double value=maximum?std::log1p(blurred[size_t(y)*w+x])/std::log1p(maximum):0;image_.setPixelColor(x,y,heatColor(value));}
    }
    void paintEvent(QPaintEvent*)override{
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.setRenderHint(QPainter::SmoothPixmapTransform);
        if(fullscreen){
            painter.setCompositionMode(QPainter::CompositionMode_Source);painter.fillRect(rect(),Qt::transparent);painter.setCompositionMode(QPainter::CompositionMode_SourceOver);painter.setOpacity(heatOpacity);
        }else painter.fillRect(rect(),QColor("#17232e"));
        const auto area=mapRect();
        if(!fullscreen){painter.setPen(Qt::NoPen);painter.setBrush(QColor("#05080c"));painter.drawRoundedRect(area.adjusted(-5,-5,5,5),8,8);}
        painter.drawImage(area,image_);
        if(fullscreen){
            painter.setPen(QPen(QColor(255,255,255,180),1));
            if(showOriginalPoints){for(const auto& point:filtered_)painter.drawPoint(QPointF(area.left()+(point.x+0.5)/data.screen.width*area.width(),area.top()+(point.y+0.5)/data.screen.height*area.height()));}
            else {for(const auto& point:clusters_)painter.drawPoint(QPointF(area.left()+(point.x+0.5)/data.screen.width*area.width(),area.top()+(point.y+0.5)/data.screen.height*area.height()));}
        }
    }
    void mousePressEvent(QMouseEvent* event)override{if(event->button()==Qt::LeftButton&&!fullscreen&&clicked)clicked();}
    void mouseMoveEvent(QMouseEvent* event)override{
        const auto area=mapRect();if(!area.contains(event->position())){QToolTip::hideText();return;}
        const int x=std::clamp(int((event->position().x()-area.left())/area.width()*data.screen.width),0,data.screen.width-1);
        const int y=std::clamp(int((event->position().y()-area.top())/area.height()*data.screen.height),0,data.screen.height-1);
        wi::Count count=0;for(const auto& point:filtered_)if(point.x==x&&point.y==y)count+=point.count;
        QToolTip::showText(event->globalPosition().toPoint(),QString("(%1, %2) · %3 次").arg(x).arg(y).arg(number(count)),this);
    }
};
class FullscreenHeatmap : public QDialog {
public:
    MouseHeatmapTile* heatmap;
    QToolButton* exit;
    QFrame* opacityControls;
    explicit FullscreenHeatmap(const wi::ViewResult::MouseScreen& data,int button,int radius,QWidget* parent):QDialog(parent,Qt::Window|Qt::FramelessWindowHint){
        setAttribute(Qt::WA_DeleteOnClose);setAttribute(Qt::WA_TranslucentBackground);setAutoFillBackground(false);setStyleSheet("QDialog{background:transparent;}");setWindowTitle("鼠标点击热力图");heatmap=new MouseHeatmapTile(this);heatmap->fullscreen=true;heatmap->setAutoFillBackground(false);heatmap->setCursor(Qt::ArrowCursor);heatmap->setFilter(button,radius);heatmap->setData(data);
        opacityControls=new QFrame(this);opacityControls->setObjectName("opacityControls");opacityControls->setStyleSheet("QFrame#opacityControls{background:#263845;border:1px solid #738693;border-radius:6px;}QLabel{color:#e4edf3;font-size:13px;}");
        auto controls=new QHBoxLayout(opacityControls);controls->setContentsMargins(12,4,12,4);controls->setSpacing(10);
        auto filter=new QComboBox(opacityControls);filter->addItem("左右键",-1);filter->addItem("左键",0);filter->addItem("右键",1);filter->setCurrentIndex(filter->findData(button));filter->setAccessibleName("全屏热力图按键筛选");controls->addWidget(filter);
        controls->addWidget(new QLabel("半径",opacityControls));auto radiusControl=new QSpinBox(opacityControls);radiusControl->setRange(1,200);radiusControl->setSuffix(" px");radiusControl->setValue(radius);radiusControl->setAccessibleName("全屏热力点聚合半径");controls->addWidget(radiusControl);
        auto applyFilter=[this,filter,radiusControl]{heatmap->setFilter(filter->currentData().toInt(),radiusControl->value());};
        QObject::connect(filter,&QComboBox::currentIndexChanged,this,applyFilter);QObject::connect(radiusControl,&QSpinBox::valueChanged,this,applyFilter);
        auto original=new QToolButton(opacityControls);original->setText("原始点");original->setCheckable(true);original->setToolTip("显示未经聚合的原始点击位置");original->setStyleSheet("QToolButton{color:white;background:#263845;border:1px solid #738693;border-radius:4px;padding:5px;}QToolButton:checked{background:#345b50;}");controls->addWidget(original);QObject::connect(original,&QToolButton::toggled,this,[this](bool checked){heatmap->showOriginalPoints=checked;heatmap->update();});
        controls->addWidget(new QLabel("透明度",opacityControls));
        auto opacity=new QSlider(Qt::Horizontal,opacityControls);opacity->setRange(0,100);opacity->setValue(0);opacity->setAccessibleName("全屏热力图透明度");controls->addWidget(opacity,1);
        auto percentage=new QLabel("0%",opacityControls);percentage->setMinimumWidth(38);percentage->setAlignment(Qt::AlignRight|Qt::AlignVCenter);controls->addWidget(percentage);
        QObject::connect(opacity,&QSlider::valueChanged,this,[this,percentage](int value){heatmap->heatOpacity=1.0-value/100.0;percentage->setText(QString::number(value)+"%");heatmap->update();});
        exit=new QToolButton(this);exit->setText("X");exit->setToolTip("退出全屏");exit->setAccessibleName("退出全屏");exit->setStyleSheet("QToolButton{background:#263845;color:white;font-size:22px;border:1px solid #738693;border-radius:6px;}");
        QObject::connect(exit,&QToolButton::clicked,this,&QDialog::close);
    }
    void resizeEvent(QResizeEvent* event)override{QDialog::resizeEvent(event);heatmap->setGeometry(rect());const int controlWidth=std::min(760,width()-88);opacityControls->setGeometry(width()-controlWidth-74,14,controlWidth,44);exit->setGeometry(width()-62,14,48,44);opacityControls->raise();exit->raise();}
};
MouseStatistics::MouseStatistics(QWidget* parent):QWidget(parent){
    auto layout=new QHBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(28);
    auto mapPanel=new QFrame(this);mapPanel->setObjectName("panel");auto maps=new QVBoxLayout(mapPanel);maps->setContentsMargins(20,20,20,20);
    auto titleRow=new QHBoxLayout;auto title=new QLabel("鼠标点击热力图",mapPanel);title->setObjectName("sectionTitle");titleRow->addWidget(title);titleRow->addStretch();
    buttonFilter_=new QComboBox(mapPanel);buttonFilter_->addItem("左右键",-1);buttonFilter_->addItem("左键",0);buttonFilter_->addItem("右键",1);buttonFilter_->setAccessibleName("鼠标热力图按键筛选");titleRow->addWidget(buttonFilter_);
    titleRow->addWidget(new QLabel("半径",mapPanel));radius_=new QSpinBox(mapPanel);radius_->setRange(1,200);radius_->setValue(24);radius_->setSuffix(" px");radius_->setAccessibleName("热力点聚合半径");titleRow->addWidget(radius_);maps->addLayout(titleRow);
    auto applyFilter=[this]{for(auto tile:tiles_)tile->setFilter(buttonFilter_->currentData().toInt(),radius_->value());};
    QObject::connect(buttonFilter_,&QComboBox::currentIndexChanged,this,applyFilter);QObject::connect(radius_,&QSpinBox::valueChanged,this,applyFilter);
    screens_=new QGridLayout;maps->addLayout(screens_,1);empty_=new QLabel("暂无点击位置",mapPanel);empty_->setAlignment(Qt::AlignCenter);maps->addWidget(empty_,1);layout->addWidget(mapPanel,1);
    auto ratioPanel=new QFrame(this);ratioPanel->setObjectName("panel");ratioPanel->setFixedWidth(290);auto ratioLayout=new QVBoxLayout(ratioPanel);ratioLayout->setContentsMargins(14,20,14,20);
    auto heading=new QLabel("鼠标操作占比",ratioPanel);heading->setObjectName("sectionTitle");ratioLayout->addWidget(heading);
    ratios_=new QTableWidget(0,3,ratioPanel);ratios_->setHorizontalHeaderLabels({"操作","次数","占比"});ratios_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);ratios_->verticalHeader()->hide();ratios_->verticalHeader()->setDefaultSectionSize(36);ratios_->setShowGrid(false);ratios_->setEditTriggers(QAbstractItemView::NoEditTriggers);ratios_->setSelectionMode(QAbstractItemView::NoSelection);ratioLayout->addWidget(ratios_,1);layout->addWidget(ratioPanel);
}
void MouseStatistics::setStatistics(const wi::ViewResult& result){
    QSet<QString> keep;int index=0;
    for(const auto& screen:result.mouseScreens){
        const auto id=screenKey(screen.screen);keep.insert(id);auto tile=tiles_.value(id,nullptr);
        if(!tile){
            auto container=new QWidget(this);auto layout=new QVBoxLayout(container);layout->setContentsMargins(0,0,0,0);
            auto label=new QLabel(displayName(QString::fromStdString(screen.screen.screenName))+QString(" · %1 × %2").arg(screen.screen.width).arg(screen.screen.height),container);label->setAlignment(Qt::AlignCenter);layout->addWidget(label);
            tile=new MouseHeatmapTile(container);tile->setObjectName("mouseHeatmap");tile->clicked=[this,tile]{openFullscreen(tile->data);};layout->addWidget(tile,1);tiles_.insert(id,tile);
        }
        tile->setFilter(buttonFilter_->currentData().toInt(),radius_->value());tile->setData(screen);screens_->addWidget(tile->parentWidget(),index/2,index%2);++index;
    }
    const auto ids=tiles_.keys();for(const auto& id:ids)if(!keep.contains(id)){auto tile=tiles_.take(id);screens_->removeWidget(tile->parentWidget());delete tile->parentWidget();}
    empty_->setVisible(tiles_.isEmpty());
    std::array<wi::Count,4> counts{};
    for(const auto& control:result.controls){if(control.control=="mouse:button:0")counts[0]+=control.count;else if(control.control=="mouse:button:1")counts[1]+=control.count;else if(control.control=="mouse:wheel:0:+")counts[2]+=control.count;else if(control.control=="mouse:wheel:0:-")counts[3]+=control.count;}
    ratios_->clearSpans();ratios_->setRowCount(11);int row=0;
    auto group=[&](const QString& title,const std::vector<int>& items){
        auto heading=new QTableWidgetItem(title);heading->setBackground(QColor("#243442"));auto font=heading->font();font.setBold(true);heading->setFont(font);ratios_->setItem(row,0,heading);ratios_->setSpan(row,0,1,3);++row;
        wi::Count total=0;for(auto i:items)total+=counts[i];const QStringList labels={"左键","右键","滚轮上","滚轮下"};
        for(auto i:items){ratios_->setItem(row,0,new QTableWidgetItem(labels[i]));ratios_->setItem(row,1,new QTableWidgetItem(number(counts[i])));ratios_->setItem(row,2,new QTableWidgetItem(QString::number(total?double(counts[i])*100/double(total):0,'f',2)+"%"));++row;}
    };
    group("左右键占比",{0,1});group("滚轮上下占比",{2,3});group("左右键与滚轮合计占比",{0,1,2,3});
}
void MouseStatistics::openFullscreen(const wi::ViewResult::MouseScreen& screen){
    auto target=QGuiApplication::primaryScreen();const auto name=displayName(QString::fromStdString(screen.screen.screenName));
    for(auto monitor:QGuiApplication::screens())if(displayName(monitor->name())==name){target=monitor;break;}
    if(!target)return;
    auto dialog=new FullscreenHeatmap(screen,buttonFilter_->currentData().toInt(),radius_->value(),window());dialog->winId();if(dialog->windowHandle())dialog->windowHandle()->setScreen(target);dialog->setGeometry(target->geometry());dialog->showFullScreen();
}
