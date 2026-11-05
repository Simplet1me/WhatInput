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
#include <QFrame>
#include <QSet>
#include <QResizeEvent>
#include <QLocale>
#include <array>
#include <algorithm>
#include <functional>

namespace {
QString number(wi::Count n){return QLocale(QLocale::Chinese).toString(qlonglong(n));}
QString screenKey(const wi::MousePosition& s){return QString::fromStdString(s.screenIdentity)+QString("|%1,%2,%3,%4").arg(s.left).arg(s.top).arg(s.width).arg(s.height);}
QString displayName(QString name){name.remove("\\\\.\\");return name;}
QComboBox* buttonChoices(QWidget* parent,int selected=-1){
    auto combo=new QComboBox(parent);combo->addItem("左右键",-1);combo->addItem("左键",0);combo->addItem("右键",1);combo->setCurrentIndex(combo->findData(selected));combo->setAccessibleName("热力图按键筛选");return combo;
}
QSpinBox* gridControl(QWidget* parent,int value=10){
    auto spin=new QSpinBox(parent);spin->setRange(5,100);spin->setSingleStep(5);spin->setValue(value);spin->setSuffix(" px");spin->setAccessibleName("热力图网格边长");spin->setToolTip("按物理像素划分正方形网格，默认 10 × 10");return spin;
}
QSpinBox* radiusControl(QWidget* parent,int value=24){
    auto spin=new QSpinBox(parent);spin->setRange(1,200);spin->setValue(value);spin->setSuffix(" px");spin->setAccessibleName("普通热力图平滑半径");return spin;
}
QSpinBox* edgeControl(QWidget* parent,int value=0){
    auto spin=new QSpinBox(parent);spin->setRange(0,4);spin->setValue(value);spin->setSuffix(" px");spin->setAccessibleName("网格边界衔接宽度");spin->setToolTip("仅处理格子交界，0 为硬边界；最大不超过半格");return spin;
}
QComboBox* scaleChoices(QWidget* parent,bool logarithmic=true){
    auto combo=new QComboBox(parent);combo->addItem("对数",true);combo->addItem("线性",false);combo->setCurrentIndex(logarithmic?0:1);combo->setAccessibleName("热力图颜色映射");return combo;
}
QSpinBox* strengthControl(QWidget* parent,int value=100){
    auto spin=new QSpinBox(parent);spin->setRange(25,400);spin->setValue(value);spin->setSuffix("%");spin->setSingleStep(25);spin->setAccessibleName("热力图颜色强度");return spin;
}
bool sameSources(const HeatmapSources& a,const HeatmapSources& b){
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)if(a[i].screen!=b[i].screen||a[i].points!=b[i].points)return false;
    return true;
}
}
class MouseHeatmapTile : public QWidget {
public:
    std::function<void()> clicked;
    bool fullscreen=false,showOriginalPoints=false;
    double heatOpacity=1.0;
    HeatmapFrame frame;
    explicit MouseHeatmapTile(QWidget* parent=nullptr):QWidget(parent){setMinimumSize(300,220);setMouseTracking(true);setCursor(Qt::PointingHandCursor);}
    void setFrame(HeatmapFrame value){frame=std::move(value);update();}
protected:
    QRectF mapRect()const{
        QRectF available=fullscreen?QRectF(rect()):QRectF(rect()).adjusted(8,8,-8,-8);
        const double ratio=frame.density?double(std::max(1,frame.density->screen.width))/std::max(1,frame.density->screen.height):16.0/9;
        double w=available.width(),h=w/ratio;if(h>available.height()){h=available.height();w=h*ratio;}
        return {available.center().x()-w/2,available.center().y()-h/2,w,h};
    }
    void paintEvent(QPaintEvent*)override{
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.setRenderHint(QPainter::SmoothPixmapTransform);
        if(fullscreen){painter.setCompositionMode(QPainter::CompositionMode_Source);painter.fillRect(rect(),Qt::transparent);painter.setCompositionMode(QPainter::CompositionMode_SourceOver);painter.setOpacity(heatOpacity);}
        else painter.fillRect(rect(),QColor("#17232e"));
        if(!frame.density||frame.levels.empty())return;
        const auto area=mapRect();
        if(!fullscreen){painter.setPen(Qt::NoPen);painter.setBrush(QColor("#05080c"));painter.drawRoundedRect(area.adjusted(-5,-5,5,5),8,8);}
        // Pick a pyramid level for the actual display density. The full view
        // uses the same field and colors, never a separately normalized image.
        const double neededWidth=area.width()*devicePixelRatioF(),neededHeight=area.height()*devicePixelRatioF();
        size_t level=0;for(size_t i=1;i<frame.levels.size();++i){if(frame.levels[i].width()<neededWidth||frame.levels[i].height()<neededHeight)break;level=i;}
        painter.drawImage(area,frame.levels[level]);
        if(fullscreen&&showOriginalPoints){
            painter.setPen(QPen(QColor(255,255,255,180),1));
            const auto& field=*frame.density;
            for(const auto& point:field.points)painter.drawPoint(QPointF(area.left()+(point.x+0.5)/field.screen.width*area.width(),area.top()+(point.y+0.5)/field.screen.height*area.height()));
        }
    }
    void mousePressEvent(QMouseEvent* event)override{if(event->button()==Qt::LeftButton&&!fullscreen&&clicked)clicked();}
    void mouseMoveEvent(QMouseEvent* event)override{
        if(!frame.density)return;const auto area=mapRect();if(!area.contains(event->position())){QToolTip::hideText();return;}
        const auto& field=*frame.density;
        const int x=std::clamp(int((event->position().x()-area.left())/area.width()*field.screen.width),0,field.screen.width-1);
        const int y=std::clamp(int((event->position().y()-area.top())/area.height()*field.screen.height),0,field.screen.height-1);
        if(field.grid&&(!fullscreen||!showOriginalPoints)){
            const int column=x/field.cellSize,row=y/field.cellSize;
            const auto count=field.cellCounts[size_t(row)*field.columns+column];
            QToolTip::showText(event->globalPosition().toPoint(),QString("网格 (%1, %2)–(%3, %4)\n%5 次")
                .arg(column*field.cellSize).arg(row*field.cellSize)
                .arg(std::min((column+1)*field.cellSize,field.screen.width)-1)
                .arg(std::min((row+1)*field.cellSize,field.screen.height)-1).arg(number(count)),this);return;
        }
        wi::Count count=0;for(const auto& point:field.points)if(point.x==x&&point.y==y)count+=point.count;
        QToolTip::showText(event->globalPosition().toPoint(),QString("(%1, %2) · %3 次").arg(x).arg(y).arg(number(count)),this);
    }
    void leaveEvent(QEvent* event)override{QToolTip::hideText();QWidget::leaveEvent(event);}
};

class FullscreenHeatmap : public QDialog {
public:
    MouseHeatmapTile* heatmap;
    QToolButton* exit;
    QFrame* controlsPanel;
    HeatmapRenderer* renderer;
    std::shared_ptr<const HeatmapBatch> batch;
    QString selectedScreen;
    FullscreenHeatmap(std::shared_ptr<const HeatmapBatch> initial,QString selected,QWidget* parent)
        :QDialog(parent,Qt::Window|Qt::FramelessWindowHint),batch(std::move(initial)),selectedScreen(std::move(selected)){
        setAttribute(Qt::WA_DeleteOnClose);setAttribute(Qt::WA_TranslucentBackground);setAutoFillBackground(false);setStyleSheet("QDialog{background:transparent;}");setWindowTitle("鼠标点击热力图");
        heatmap=new MouseHeatmapTile(this);heatmap->fullscreen=true;heatmap->setAutoFillBackground(false);heatmap->setCursor(Qt::ArrowCursor);
        for(const auto& frame:batch->frames)if(screenKey(frame.density->screen)==selectedScreen){heatmap->setFrame(frame);break;}
        renderer=new HeatmapRenderer(this);renderer->ready=[this](std::shared_ptr<const HeatmapBatch> rendered){
            batch=std::move(rendered);for(const auto& frame:batch->frames)if(screenKey(frame.density->screen)==selectedScreen){heatmap->setFrame(frame);break;}
        };
        controlsPanel=new QFrame(this);controlsPanel->setObjectName("opacityControls");controlsPanel->setStyleSheet("QFrame#opacityControls{background:#263845;border:1px solid #738693;border-radius:6px;}QLabel{color:#e4edf3;font-size:13px;}");
        auto rows=new QVBoxLayout(controlsPanel);rows->setContentsMargins(12,6,12,6);rows->setSpacing(5);auto parameters=new QHBoxLayout;parameters->setSpacing(8);
        auto filter=buttonChoices(controlsPanel,batch->settings.button);parameters->addWidget(filter);
        parameters->addWidget(new QLabel(batch->settings.grid?"网格":"平滑",controlsPanel));auto cellSize=gridControl(controlsPanel,batch->settings.cellSize);parameters->addWidget(cellSize);cellSize->setVisible(batch->settings.grid);
        auto radius=radiusControl(controlsPanel,batch->settings.radius);parameters->addWidget(radius);radius->setVisible(!batch->settings.grid);
        auto mode=scaleChoices(controlsPanel,batch->settings.logarithmic);parameters->addWidget(mode);
        parameters->addWidget(new QLabel("强度",controlsPanel));auto strength=strengthControl(controlsPanel,batch->settings.strength);parameters->addWidget(strength);
        auto original=new QToolButton(controlsPanel);original->setText("原始点");original->setCheckable(true);original->setToolTip("显示原始点击位置");original->setStyleSheet("QToolButton{color:white;background:#263845;border:1px solid #738693;border-radius:4px;padding:5px;}QToolButton:checked{background:#345b50;}");parameters->addWidget(original);
        QObject::connect(original,&QToolButton::toggled,this,[this](bool value){heatmap->showOriginalPoints=value;heatmap->update();});rows->addLayout(parameters);
        auto edge=edgeControl(controlsPanel,batch->settings.edgeBlend);
        auto apply=[this,filter,cellSize,mode,strength,edge,radius]{
            HeatmapSettings value{filter->currentData().toInt(),cellSize->value(),strength->value(),mode->currentData().toBool(),edge->value(),batch->settings.grid,radius->value()};
            renderer->request(batch->sources,value,0,batch);
        };
        QObject::connect(filter,&QComboBox::currentIndexChanged,this,apply);QObject::connect(cellSize,&QSpinBox::valueChanged,this,apply);QObject::connect(edge,&QSpinBox::valueChanged,this,apply);
        QObject::connect(radius,&QSpinBox::valueChanged,this,apply);edge->setEnabled(batch->settings.grid);
        QObject::connect(mode,&QComboBox::currentIndexChanged,this,apply);QObject::connect(strength,&QSpinBox::valueChanged,this,apply);
        auto opacityRow=new QHBoxLayout;opacityRow->setSpacing(10);opacityRow->addWidget(new QLabel("衔接",controlsPanel));opacityRow->addWidget(edge);opacityRow->addWidget(new QLabel("透明度",controlsPanel));
        auto opacity=new QSlider(Qt::Horizontal,controlsPanel);opacity->setRange(0,100);opacity->setValue(0);opacity->setAccessibleName("全屏热力图透明度");opacityRow->addWidget(opacity,1);
        auto percentage=new QLabel("0%",controlsPanel);percentage->setMinimumWidth(38);percentage->setAlignment(Qt::AlignRight|Qt::AlignVCenter);opacityRow->addWidget(percentage);rows->addLayout(opacityRow);
        QObject::connect(opacity,&QSlider::valueChanged,this,[this,percentage](int value){heatmap->heatOpacity=1.0-value/100.0;percentage->setText(QString::number(value)+"%");heatmap->update();});
        exit=new QToolButton(this);exit->setText("X");exit->setToolTip("退出全屏");exit->setAccessibleName("退出全屏");exit->setStyleSheet("QToolButton{background:#263845;color:white;font-size:22px;border:1px solid #738693;border-radius:6px;}");
        QObject::connect(exit,&QToolButton::clicked,this,&QDialog::close);
    }
    void resizeEvent(QResizeEvent* event)override{
        QDialog::resizeEvent(event);heatmap->setGeometry(rect());const int controlWidth=std::min(720,width()-88);controlsPanel->setGeometry(width()-controlWidth-74,14,controlWidth,94);exit->setGeometry(width()-62,14,48,44);controlsPanel->raise();exit->raise();
    }
};
MouseStatistics::MouseStatistics(QWidget* parent):QWidget(parent){
    auto layout=new QHBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(28);
    auto mapPanel=new QFrame(this);mapPanel->setObjectName("panel");auto maps=new QVBoxLayout(mapPanel);maps->setContentsMargins(20,20,20,20);
    auto titleRow=new QHBoxLayout;auto title=new QLabel("鼠标点击热力图",mapPanel);title->setObjectName("sectionTitle");titleRow->addWidget(title);titleRow->addStretch();
    softwareFilter_=new QComboBox(mapPanel);softwareFilter_->addItem("全部软件","");softwareFilter_->setIconSize(QSize(22,22));softwareFilter_->setMinimumContentsLength(8);softwareFilter_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);softwareFilter_->setMaximumWidth(240);softwareFilter_->view()->setMinimumWidth(320);softwareFilter_->setAccessibleName("鼠标统计软件筛选");titleRow->addWidget(softwareFilter_);
    buttonFilter_=buttonChoices(mapPanel);titleRow->addWidget(buttonFilter_);maps->addLayout(titleRow);
    auto parameters=new QHBoxLayout;parameters->setSpacing(8);parameters->addWidget(new QLabel("网格",mapPanel));gridSize_=gridControl(mapPanel);parameters->addWidget(gridSize_);
    parameters->addWidget(new QLabel("衔接",mapPanel));edgeBlend_=edgeControl(mapPanel);parameters->addWidget(edgeBlend_);
    parameters->addWidget(new QLabel("平滑",mapPanel));radius_=radiusControl(mapPanel);radius_->setEnabled(false);parameters->addWidget(radius_);
    scaleMode_=scaleChoices(mapPanel);parameters->addWidget(scaleMode_);parameters->addWidget(new QLabel("强度",mapPanel));strength_=strengthControl(mapPanel);parameters->addWidget(strength_);parameters->addStretch();maps->addLayout(parameters);
    auto apply=[this]{requestRendering();};QObject::connect(buttonFilter_,&QComboBox::currentIndexChanged,this,apply);QObject::connect(gridSize_,&QSpinBox::valueChanged,this,apply);QObject::connect(edgeBlend_,&QSpinBox::valueChanged,this,apply);
    QObject::connect(scaleMode_,&QComboBox::currentIndexChanged,this,apply);QObject::connect(strength_,&QSpinBox::valueChanged,this,apply);
    QObject::connect(radius_,&QSpinBox::valueChanged,this,apply);
    screens_=new QGridLayout;maps->addLayout(screens_,1);empty_=new QLabel("暂无点击位置",mapPanel);empty_->setAlignment(Qt::AlignCenter);maps->addWidget(empty_,1);layout->addWidget(mapPanel,1);
    auto ratioPanel=new QFrame(this);ratioPanel->setObjectName("panel");ratioPanel->setFixedWidth(290);auto ratioLayout=new QVBoxLayout(ratioPanel);ratioLayout->setContentsMargins(14,20,14,20);
    auto heading=new QLabel("鼠标操作占比",ratioPanel);heading->setObjectName("sectionTitle");ratioLayout->addWidget(heading);
    ratios_=new QTableWidget(0,3,ratioPanel);ratios_->setHorizontalHeaderLabels({"操作","次数","占比"});ratios_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);ratios_->verticalHeader()->hide();ratios_->verticalHeader()->setDefaultSectionSize(36);ratios_->setShowGrid(false);ratios_->setEditTriggers(QAbstractItemView::NoEditTriggers);ratios_->setSelectionMode(QAbstractItemView::NoSelection);ratioLayout->addWidget(ratios_,1);layout->addWidget(ratioPanel);
    renderer_=new HeatmapRenderer(this);renderer_->ready=[this](std::shared_ptr<const HeatmapBatch> value){showBatch(std::move(value));};
}
void MouseStatistics::setGridMode(bool grid){gridMode_=grid;gridSize_->setEnabled(grid);edgeBlend_->setEnabled(grid);radius_->setEnabled(!grid);requestRendering();}
HeatmapSettings MouseStatistics::settings()const{return {buttonFilter_->currentData().toInt(),gridSize_->value(),strength_->value(),scaleMode_->currentData().toBool(),edgeBlend_->value(),gridMode_,radius_->value()};}
void MouseStatistics::requestRendering(){if(sources_)renderer_->request(sources_,settings(),scope_,batch_);}
void MouseStatistics::showBatch(std::shared_ptr<const HeatmapBatch> batch){
    batch_=std::move(batch);QSet<QString> keep;int index=0;
    for(const auto& frame:batch_->frames){
        const auto& screen=frame.density->screen;const auto id=screenKey(screen);keep.insert(id);auto tile=tiles_.value(id,nullptr);
        if(!tile){
            auto container=new QWidget(this);auto layout=new QVBoxLayout(container);layout->setContentsMargins(0,0,0,0);
            auto label=new QLabel(displayName(QString::fromStdString(screen.screenName))+QString(" · %1 × %2").arg(screen.width).arg(screen.height),container);label->setAlignment(Qt::AlignCenter);layout->addWidget(label);
            tile=new MouseHeatmapTile(container);tile->setObjectName("mouseHeatmap");tile->clicked=[this,tile]{
                if(!batch_||!tile->frame.density)return;
                wi::ViewResult::MouseScreen selected;selected.screen=tile->frame.density->screen;openFullscreen(selected);
            };layout->addWidget(tile,1);tiles_.insert(id,tile);
        }
        tile->setFrame(frame);screens_->addWidget(tile->parentWidget(),index/2,index%2);++index;
    }
    const auto ids=tiles_.keys();for(const auto& id:ids)if(!keep.contains(id)){auto tile=tiles_.take(id);screens_->removeWidget(tile->parentWidget());delete tile->parentWidget();}
    empty_->setVisible(tiles_.isEmpty());
}
void MouseStatistics::setStatistics(const wi::ViewResult& result){
    const bool changed=scope_!=result.request||!sources_||!sameSources(*sources_,result.mouseScreens);scope_=result.request;
    if(changed){sources_=std::make_shared<HeatmapSources>(result.mouseScreens);requestRendering();}
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
void MouseStatistics::clearRendering(){
    renderer_->clear();sources_.reset();batch_.reset();
    for(auto tile:tiles_)delete tile->parentWidget();tiles_.clear();empty_->show();
    ratios_->clearSpans();ratios_->setRowCount(0);
}
void MouseStatistics::openFullscreen(const wi::ViewResult::MouseScreen& screen){
    if(!batch_)return;auto target=QGuiApplication::primaryScreen();const auto name=displayName(QString::fromStdString(screen.screen.screenName));
    for(auto monitor:QGuiApplication::screens())if(displayName(monitor->name())==name){target=monitor;break;}
    if(!target)return;
    auto dialog=new FullscreenHeatmap(batch_,screenKey(screen.screen),window());dialog->winId();if(dialog->windowHandle())dialog->windowHandle()->setScreen(target);dialog->setGeometry(target->geometry());dialog->showFullScreen();
}
