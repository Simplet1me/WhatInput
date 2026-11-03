#include "window.h"
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QCryptographicHash>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QStyle>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableView>
#include <QTableWidget>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
QString number(wi::Count n){return QLocale(QLocale::Chinese).toString(qlonglong(n));}
QString category(const std::string& type){return QString::fromStdString(wi::categoryName(type));}
QString datetime(wi::Count t){return QDateTime::fromSecsSinceEpoch(t).toLocalTime().toString("yyyy-MM-dd HH:mm:ss t");}
QLabel* heading(const QString& text,QWidget* parent=nullptr){auto l=new QLabel(text,parent);l->setObjectName("sectionTitle");return l;}
}
class RankModel:public QAbstractTableModel {
public:
    std::vector<wi::RankedControl> rows;
    wi::Count total=0;
    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex& parent={})const override{return parent.isValid()?0:int(rows.size());}
    int columnCount(const QModelIndex& parent={})const override{return parent.isValid()?0:4;}
    QVariant headerData(int section,Qt::Orientation direction,int role)const override{
        if(role==Qt::DisplayRole&&direction==Qt::Horizontal)return QStringList{"按键","设备类型","次数","占比"}.value(section);
        return {};
    }
    QVariant data(const QModelIndex& index,int role)const override{
        if(!index.isValid()||index.row()>=int(rows.size()))return {};
        const auto& r=rows[index.row()];
        if(role==Qt::TextAlignmentRole)return int(index.column()>=2?Qt::AlignRight|Qt::AlignVCenter:Qt::AlignLeft|Qt::AlignVCenter);
        if(role==Qt::ForegroundRole&&index.column()==2)return QColor("#a9edc8");
        if(role==Qt::ToolTipRole&&index.column()==0)return QString::fromStdString(r.control);
        if(role!=Qt::DisplayRole)return {};
        switch(index.column()){
        case 0:return QString::fromStdString(r.name);
        case 1:return category(r.type);
        case 2:return number(r.count);
        case 3:return total?QString::number(double(static_cast<long double>(r.count)*100/total),'f',2)+"%":"0%";
        }return {};
    }
    void replace(const wi::ViewResult& result){beginResetModel();rows=result.controls;total=result.total;endResetModel();}
};

class ActivityChart:public QWidget {
public:
    std::vector<wi::HourCount> hours;
    std::function<void(const wi::HourCount&)> selected;
    int selectedIndex=-1;
    explicit ActivityChart(QWidget* parent=nullptr):QWidget(parent){setMouseTracking(true);setMinimumHeight(255);setAccessibleName("每小时输入次数，点击柱子查看该小时排行");}
    void setHours(std::vector<wi::HourCount> data){hours=std::move(data);selectedIndex=-1;setMinimumWidth(std::max(380,int(hours.size())*66+65));update();}
protected:
    void paintEvent(QPaintEvent* event)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#17232e"));
        if(hours.empty()){p.setPen(QColor("#92a6b5"));p.drawText(rect(),Qt::AlignCenter,"这个时段没有输入");return;}
        wi::Count max=1;for(auto& h:hours)max=std::max(max,h.count);
        int bottom=height()-50,top=30;const auto graphHeight=bottom-top;
        p.setPen(QColor("#334452"));p.drawLine(48,bottom,width(),bottom);
        p.setPen(QColor("#8198a8"));p.drawText(QRect(0,0,width(),24),Qt::AlignLeft,number(max)+" 次 / 最高小时");
        int first=std::max(0,(event->rect().left()-48)/66-1);
        int last=std::min(int(hours.size()),(event->rect().right()-48)/66+2);
        for(int i=first;i<last;++i){
            const auto& h=hours[i];qreal barHeight=std::max(2.0,double(h.count)/double(max)*graphHeight);
            QRectF bar(48+i*66,bottom-barHeight,42,barHeight);
            p.setPen(Qt::NoPen);p.setBrush(i==selectedIndex?QColor("#edc878"):QColor("#86cdae"));p.drawRoundedRect(bar,4,4);
            p.setPen(QColor("#adbfca"));const auto local=QDateTime::fromSecsSinceEpoch(h.start).toLocalTime();
            p.drawText(QRect(38+i*66,bottom+8,62,38),Qt::AlignHCenter,local.toString("MM-dd\nHH:mm"));
        }
    }
    void mousePressEvent(QMouseEvent* event)override{
        if(event->button()!=Qt::LeftButton)return;int index=(int(event->position().x())-48)/66;
        if(event->position().x()>=48&&index>=0&&index<int(hours.size())){selectedIndex=index;update();if(selected)selected(hours[index]);}
    }
    void mouseMoveEvent(QMouseEvent* event)override{
        int index=(int(event->position().x())-48)/66;
        if(event->position().x()>=48&&index>=0&&index<int(hours.size())){auto& h=hours[index];QToolTip::showText(event->globalPosition().toPoint(),datetime(h.start)+" → "+datetime(h.end)+"\n"+number(h.count)+" 次",this);}else QToolTip::hideText();
    }
};

StatsWindow::StatsWindow(wi::Engine& engine,QString root,QWidget* parent):QMainWindow(parent),engine_(engine),root_(std::move(root)){
    setWindowTitle("WhatInput · 本地输入统计");resize(1220,850);setMinimumSize(920,680);
    auto central=new QWidget(this);setCentralWidget(central);auto layout=new QVBoxLayout(central);layout->setContentsMargins(28,22,28,20);layout->setSpacing(16);
    auto header=new QHBoxLayout;auto brand=new QLabel("WhatInput",this);brand->setObjectName("brand");header->addWidget(brand);auto subtitle=new QLabel("本地输入统计",this);subtitle->setObjectName("muted");header->addWidget(subtitle);header->addStretch();
    auto settingButton=new QPushButton("设置",this);header->addWidget(settingButton);connect(settingButton,&QPushButton::clicked,this,&StatsWindow::settings);layout->addLayout(header);
    auto intro=new QHBoxLayout;auto title=new QLabel("你的输入，一目了然。",this);title->setObjectName("hero");intro->addWidget(title);intro->addStretch();auto metric=new QVBoxLayout;auto caption=new QLabel("当前范围总次数",this);caption->setAlignment(Qt::AlignRight);caption->setObjectName("muted");metric->addWidget(caption);total_=new QLabel("—",this);total_->setObjectName("total");total_->setAlignment(Qt::AlignRight);metric->addWidget(total_);intro->addLayout(metric);layout->addLayout(intro);
    auto filters=new QFrame(this);filters->setObjectName("panel");auto fLayout=new QVBoxLayout(filters);fLayout->setContentsMargins(16,14,16,14);auto quick=new QHBoxLayout;
    for(auto [label,days]:std::vector<std::pair<QString,int>>{{"今天",0},{"昨天",-1},{"最近 7 天",7},{"全部",-2}}){auto button=new QPushButton(label,this);quick->addWidget(button);connect(button,&QPushButton::clicked,this,[this,days]{preset(days);});}quick->addStretch();fLayout->addLayout(quick);
    auto fields=new QGridLayout;start_=new QDateTimeEdit(this);end_=new QDateTimeEdit(this);for(auto edit:{start_,end_}){edit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");edit->setCalendarPopup(true);edit->setMinimumWidth(190);}
    type_=new QComboBox(this);type_->addItem("全部类型","");type_->addItem("键盘","keyboard");type_->addItem("鼠标","mouse");type_->addItem("手柄","gamepad");
    app_=new QComboBox(this);app_->addItem("全部软件","");app_->setMinimumWidth(240);app_->setIconSize(QSize(24,24));app_->view()->setMinimumWidth(340);app_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);app_->setMinimumContentsLength(22);
    auto apply=new QPushButton("查询",this);apply->setObjectName("primary");
    fields->addWidget(new QLabel("起始时间",this),0,0);fields->addWidget(start_,1,0);fields->addWidget(new QLabel("结束时间（不包含）",this),0,1);fields->addWidget(end_,1,1);fields->addWidget(apply,1,2);fields->addWidget(new QLabel("软件",this),0,3);fields->addWidget(app_,1,3);fields->addWidget(new QLabel("设备类型",this),0,4);fields->addWidget(type_,1,4);fields->setColumnStretch(3,1);fLayout->addLayout(fields);layout->addWidget(filters);
    auto rangeRow=new QHBoxLayout;range_=new QLabel(this);range_->setObjectName("muted");rangeRow->addWidget(range_);back_=new QPushButton("← 返回原时段",this);back_->hide();rangeRow->addWidget(back_);rangeRow->addStretch();layout->addLayout(rangeRow);
    message_=new QLabel(this);message_->setObjectName("message");message_->setWordWrap(true);message_->hide();layout->addWidget(message_);
    auto splitter=new QSplitter(Qt::Horizontal,this);auto left=new QFrame(this);left->setObjectName("panel");auto leftLayout=new QVBoxLayout(left);leftLayout->setContentsMargins(18,16,18,16);leftLayout->addWidget(heading("活动分布 · 点击小时查看排行",this));
    chart_=new ActivityChart(this);auto scroll=new QScrollArea(this);scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);scroll->setWidget(chart_);scroll->setMinimumHeight(285);leftLayout->addWidget(scroll,2);
    leftLayout->addWidget(heading("软件使用排行",this));software_=new QTableWidget(0,2,this);software_->setHorizontalHeaderLabels({"软件","次数"});software_->verticalHeader()->hide();software_->verticalHeader()->setDefaultSectionSize(44);software_->setIconSize(QSize(28,28));software_->setMinimumWidth(400);software_->setWordWrap(false);software_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);software_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);software_->setEditTriggers(QAbstractItemView::NoEditTriggers);software_->setSelectionBehavior(QAbstractItemView::SelectRows);software_->setShowGrid(false);leftLayout->addWidget(software_,1);
    auto right=new QFrame(this);right->setObjectName("panel");auto rightLayout=new QVBoxLayout(right);rightLayout->setContentsMargins(18,16,18,16);rightLayout->addWidget(heading("按键排行",this));table_=new QTableView(this);model_=new RankModel(this);table_->setModel(model_);table_->verticalHeader()->hide();table_->setShowGrid(false);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);for(int i=1;i<4;++i)table_->horizontalHeader()->setSectionResizeMode(i,QHeaderView::ResizeToContents);rightLayout->addWidget(table_);splitter->addWidget(left);splitter->addWidget(right);splitter->setSizes({620,510});layout->addWidget(splitter,1);
    auto footer=new QHBoxLayout;auto stateLayout=new QVBoxLayout;status_=new QLabel(this);status_->setObjectName("muted");status_->setWordWrap(true);stateLayout->addWidget(status_);updated_=new QLabel(this);updated_->setObjectName("muted");stateLayout->addWidget(updated_);footer->addLayout(stateLayout,1);auto save=new QPushButton("立即保存",this);auto exportButton=new QPushButton("导出 CSV",this);auto clearButton=new QPushButton("清空历史",this);clearButton->setObjectName("danger");footer->addWidget(save);footer->addWidget(exportButton);footer->addWidget(clearButton);layout->addLayout(footer);
    setStyleSheet(R"(
QMainWindow,QDialog{background:#101922;color:#dce7ee;font-family:'Microsoft YaHei UI';font-size:13px;}
QWidget{color:#dce7ee;font-family:'Microsoft YaHei UI';font-size:13px;}
QLabel#brand{font-size:24px;font-weight:700;color:#a9edc8;} QLabel#hero{font-size:26px;font-weight:600;} QLabel#total{font-size:38px;color:#a9edc8;} QLabel#muted{color:#91a7b7;font-size:12px;} QLabel#sectionTitle{font-size:15px;font-weight:600;margin-bottom:10px;}
QFrame#panel{background:#17232e;border:1px solid #2e3e4b;border-radius:10px;}
QPushButton,QComboBox,QDateTimeEdit,QSpinBox{background:#1b2a36;border:1px solid #3b505f;border-radius:5px;padding:7px 10px;min-height:20px;}
QPushButton:hover{border-color:#a9edc8;background:#263b48;} QPushButton#primary{background:#a9edc8;color:#18382a;border-color:#a9edc8;} QPushButton#danger{color:#ecabab;}
QComboBox QAbstractItemView{background:#1b2a36;color:#dce7ee;selection-background-color:#345b50;}
QTableView{background:#17232e;alternate-background-color:#1b2a36;border:0;selection-background-color:#345b50;color:#dce7ee;}QHeaderView::section{background:#17232e;color:#94acbc;border:0;border-bottom:1px solid #344754;padding:10px;}QTableView::item{padding:7px;border-bottom:1px solid #253744;}
QScrollArea{background:#17232e;border:0;}QScrollBar{background:#17232e;}QLabel#message{background:#3e3423;color:#f0d59c;padding:10px;border-radius:6px;}
QToolTip{background:#233747;color:#e4edf3;border:1px solid #536978;}
)");
    connect(apply,&QPushButton::clicked,this,[this]{allTime_=false;original_.reset();back_->hide();query();});
    connect(type_,&QComboBox::currentIndexChanged,this,[this]{query();});connect(app_,&QComboBox::currentIndexChanged,this,[this]{query();});
    connect(back_,&QPushButton::clicked,this,[this]{if(!original_)return;start_->setDateTime(original_->start);end_->setDateTime(original_->end);allTime_=original_->all;original_.reset();back_->hide();query();});
    chart_->selected=[this](const wi::HourCount& hour){if(!original_)original_=OriginalRange{start_->dateTime(),end_->dateTime(),allTime_};allTime_=false;start_->setDateTime(QDateTime::fromSecsSinceEpoch(hour.start).toLocalTime());end_->setDateTime(QDateTime::fromSecsSinceEpoch(hour.end).toLocalTime());back_->show();query();};
    connect(save,&QPushButton::clicked,&engine_,&wi::Engine::flush);
    connect(exportButton,&QPushButton::clicked,this,[this]{if(filter().start>=filter().end){message_->setText("请选择有效时间区间。");message_->show();return;}engine_.exportCsv(filter());});
    connect(clearButton,&QPushButton::clicked,this,[this]{if(QMessageBox::warning(this,"清空历史","将永久清空所有历史及尚未保存的计数，继续吗？",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)engine_.clear();});
    connect(&engine_,&wi::Engine::viewReady,this,&StatsWindow::receive,Qt::QueuedConnection);
    connect(&engine_,&wi::Engine::notice,this,[this](const QString& message){message_->setText(message);message_->show();},Qt::QueuedConnection);
    timer_.setInterval(100);connect(&timer_,&QTimer::timeout,this,[this]{if(isVisible()&&!isMinimized()){engine_.refresh();updateStatus();}});
    preset(0);
}
wi::ViewFilter StatsWindow::filter()const{return {allTime_?0:start_->dateTime().toSecsSinceEpoch(),allTime_?INT64_MAX:end_->dateTime().toSecsSinceEpoch(),type_->currentData().toString().toStdString(),app_->currentData().toString().toStdString()};}
void StatsWindow::query(){
    auto f=filter();if(f.start>=f.end){message_->setText("起始时间必须早于结束时间。");message_->show();return;}
    message_->hide();range_->setText(allTime_?"全部记录（包含未保存的内存计数）":datetime(f.start)+" → "+datetime(f.end)+"（不含终点）");
    if(isVisible()&&!isMinimized())engine_.watch(f,++request_);
}
void StatsWindow::preset(int days){auto date=QDate::currentDate();auto start=date.startOfDay();auto end=date.addDays(1).startOfDay();allTime_=days==-2;if(days==-1){end=start;start=date.addDays(-1).startOfDay();}if(days==7)start=date.addDays(-6).startOfDay();if(allTime_)start=QDateTime::fromSecsSinceEpoch(0).toLocalTime();start_->setDateTime(start);end_->setDateTime(end);original_.reset();back_->hide();query();}
void StatsWindow::receive(std::shared_ptr<const wi::ViewResult> result){
    if(result->request!=request_||!isVisible()||isMinimized()||exiting_)return;
    if(!result->error.isEmpty()){message_->setText("数据读取失败："+result->error+"。当前结果可能未更新。");message_->show();return;}
    total_->setText(number(result->total));
    int rowScroll=table_->verticalScrollBar()->value();model_->replace(*result);table_->verticalScrollBar()->setValue(rowScroll);
    if(!original_)chart_->setHours(result->hours);
    const auto oldApp=app_->currentData().toString();QSignalBlocker block(app_);
    bool changed=app_->count()!=int(result->applications.size())+1;
    if(!changed){int i=1;for(auto& [id,name]:result->applications){if(app_->itemData(i).toString()!=QString::fromStdString(id)||app_->itemText(i)!=QString::fromStdString(name)){changed=true;break;}++i;}}
    if(changed){app_->clear();app_->addItem("全部软件","");for(auto& [id,name]:result->applications)app_->addItem(applicationIcon(QString::fromStdString(id)),QString::fromStdString(name),QString::fromStdString(id));int selected=app_->findData(oldApp);if(selected<0&&!oldApp.isEmpty()){app_->addItem(oldApp,oldApp);selected=app_->count()-1;}app_->setCurrentIndex(std::max(selected,0));}
    int softwareScroll=software_->verticalScrollBar()->value();software_->setRowCount(int(result->software.size()));int i=0;for(auto& entry:result->software){auto id=QString::fromStdString(entry.id);auto item=new QTableWidgetItem(applicationIcon(id),QString::fromStdString(entry.name));item->setToolTip(QString::fromStdString(entry.name)+"\n"+id);software_->setItem(i,0,item);auto value=new QTableWidgetItem(number(entry.count));value->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);software_->setItem(i,1,value);++i;}software_->verticalScrollBar()->setValue(softwareScroll);
    if(!result->total){message_->setText("这个时段没有输入");message_->show();}else if(message_->text()=="这个时段没有输入"||message_->text().startsWith("数据读取失败"))message_->hide();
    updated_->setText("界面更新："+result->updated.toString("HH:mm:ss.zzz")+" · 按秒聚合，包含未保存数据");updateStatus();
}
QIcon StatsWindow::applicationIcon(const QString& id){
    if(auto it=applicationIcons_.constFind(id);it!=applicationIcons_.cend())return it.value();
    const auto fallback=style()->standardIcon(id=="system"?QStyle::SP_ComputerIcon:QStyle::SP_FileIcon);
    if(id=="system"||id=="unknown"||id.isEmpty()){
        applicationIcons_.insert(id,fallback);return fallback;
    }
    const auto now=QDateTime::currentMSecsSinceEpoch();
    if(iconRetryAt_.value(id)>now)return fallback;
    const auto directory=root_+"/cache/app-icons";
    const auto key=QCryptographicHash::hash(QDir::fromNativeSeparators(id).toCaseFolded().toUtf8(),QCryptographicHash::Sha256).toHex();
    const auto filename=directory+"/"+QString::fromLatin1(key)+".png";
    QPixmap pixmap;
    if(!pixmap.load(filename)){
        // Query the executable's shell icon only when the disk cache is missing
        // or unreadable. This does not open the application's process.
        if(QFileInfo::exists(id)){
            QFileIconProvider provider;
            pixmap=provider.icon(QFileInfo(id)).pixmap(QSize(64,64));
        }
        if(pixmap.isNull()){
            iconRetryAt_.insert(id,now+60000);return fallback;
        }
        QSaveFile file(filename);
        if(!QDir().mkpath(directory)||!file.open(QIODevice::WriteOnly)||!pixmap.save(&file,"PNG")||!file.commit()){
            // Keep the icon usable in memory if the runtime directory cannot
            // be written; do not redirect the cache to AppData.
            qWarning("Unable to save application icon cache: %s",qPrintable(filename));
        }
    }
    QIcon icon(pixmap);applicationIcons_.insert(id,icon);iconRetryAt_.remove(id);return icon;
}
void StatsWindow::updateStatus(){auto state=engine_.status();QString text=exiting_?"正在保存并退出":state.saving?"正在保存":"正在统计";text+=" · 每 "+QString::number(state.interval)+" 秒保存 · 待保存 "+number(state.unsaved)+" 次";if(state.lastCommit)text+=" · 最近落盘 "+QDateTime::fromSecsSinceEpoch(state.lastCommit).toLocalTime().toString("HH:mm:ss");if(!state.storageError.isEmpty())text="保存失败，增量仍保留在内存："+state.storageError;if(!state.inputError.isEmpty())text="采集异常："+state.inputError;status_->setText(text);}
void StatsWindow::settings(){
    QDialog dialog(this);dialog.setWindowTitle("设置");auto layout=new QVBoxLayout(&dialog);auto form=new QFormLayout;auto seconds=new QSpinBox(&dialog);seconds->setRange(1,3600);seconds->setSuffix(" 秒");seconds->setValue(engine_.status().interval);form->addRow("自动保存间隔",seconds);layout->addLayout(form);auto hint=new QLabel("默认 60 秒。修改后立即开始新的计时。\n界面约每 100 毫秒刷新，正常退出时立即保存。\n异常退出可能丢失尚未保存的数据。",&dialog);hint->setWordWrap(true);layout->addWidget(hint);auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Save)->setText("保存");buttons->button(QDialogButtonBox::Cancel)->setText("取消");layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;QDir().mkpath(root_+"/config");QSaveFile file(root_+"/config/settings.json");if(!file.open(QIODevice::WriteOnly)){QMessageBox::warning(this,"设置保存失败",file.errorString());return;}auto data=QJsonDocument(QJsonObject{{"saveIntervalSeconds",seconds->value()}}).toJson();if(file.write(data)!=data.size()||!file.commit()){QMessageBox::warning(this,"设置保存失败",file.errorString());return;}engine_.setInterval(seconds->value());updateStatus();
}
void StatsWindow::open(){if(exiting_)return;showNormal();raise();activateWindow();timer_.start();query();}
void StatsWindow::prepareExit(){exiting_=true;timer_.stop();engine_.unwatch();setEnabled(false);updateStatus();}
void StatsWindow::closeEvent(QCloseEvent* event){event->ignore();hide();timer_.stop();engine_.unwatch();}
void StatsWindow::showEvent(QShowEvent* event){QMainWindow::showEvent(event);if(!exiting_){timer_.start();query();}}
void StatsWindow::changeEvent(QEvent* event){QMainWindow::changeEvent(event);if(event->type()==QEvent::WindowStateChange&&!exiting_){if(isMinimized()){timer_.stop();engine_.unwatch();}else if(isVisible()){timer_.start();query();}}}
