#include "ui/window.h"
#include "ui/keyboard/keyboard_heatmap.h"
#include "ui/mouse/mouse_statistics.h"
#include "ui/gamepad/gamepad_heatmap.h"
#include "ui/history/activity_chart.h"
#include <QToolButton>
#include <QPixmap>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTableView>
#include <QTableWidget>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
QString number(wi::Count n) {
    return QLocale(QLocale::Chinese).toString(qlonglong(n));
}
QString category(const std::string &type) {
    return QString::fromStdString(wi::categoryName(type));
}
QString datetime(wi::Count t) {
    return QDateTime::fromSecsSinceEpoch(t).toLocalTime().toString(
        "yyyy-MM-dd HH:mm:ss t");
}
QLabel *heading(const QString &text, QWidget *parent = nullptr) {
    auto l = new QLabel(text, parent);
    l->setObjectName("sectionTitle");
    return l;
}
} // namespace
class RankModel : public QAbstractTableModel {
  public:
    std::vector<wi::RankedControl> rows;
    wi::Count total = 0;
    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(rows.size());
    }
    int columnCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : 4;
    }
    QVariant headerData(int section, Qt::Orientation direction,
                        int role) const override {
        if (role == Qt::DisplayRole && direction == Qt::Horizontal)
            return QStringList{"按键", "设备类型", "次数", "占比"}.value(
                section);
        return {};
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() >= int(rows.size()))
            return {};
        const auto &r = rows[index.row()];
        if (role == Qt::TextAlignmentRole)
            return int(index.column() >= 2 ? Qt::AlignRight | Qt::AlignVCenter
                                           : Qt::AlignLeft | Qt::AlignVCenter);
        if (role == Qt::ForegroundRole && index.column() == 2)
            return QColor("#a9edc8");
        if (role == Qt::ToolTipRole && index.column() == 0)
            return QString::fromStdString(r.control);
        if (role != Qt::DisplayRole)
            return {};
        switch (index.column()) {
        case 0:
            return QString::fromStdString(r.name);
        case 1:
            return category(r.type);
        case 2:
            return number(r.count);
        case 3:
            return total ? QString::number(
                               double(static_cast<long double>(r.count) * 100 /
                                      total),
                               'f', 2) +
                               "%"
                         : "0%";
        }
        return {};
    }
    void replace(const wi::ViewResult &result) {
        beginResetModel();
        rows = result.controls;
        total = result.total;
        endResetModel();
    }
};

StatsWindow::StatsWindow(wi::Engine &engine, QString root, QWidget *parent)
    : QMainWindow(parent), engine_(engine), root_(std::move(root)) {
    setWindowTitle("WhatInput · 本地输入统计");
    resize(1440, 820);
    setMinimumSize(1180, 620);
    auto central = new QWidget(this);
    central->setObjectName("central");
    central->setAttribute(Qt::WA_StyledBackground);
    setCentralWidget(central);
    auto shell = new QHBoxLayout(central);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);
    auto sidebar = new QFrame(this);
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(210);
    auto navigation = new QVBoxLayout(sidebar);
    navigation->setContentsMargins(18, 26, 18, 22);
    navigation->setSpacing(18);
    auto brand = new QLabel("WhatInput", sidebar);
    brand->setObjectName("brand");
    navigation->addWidget(brand);
    auto tree = new QTreeWidget(sidebar);
    tree->setHeaderHidden(true);
    tree->setIndentation(16);
    tree->setRootIsDecorated(false);
    tree->setObjectName("navigation");
    auto homeItem = new QTreeWidgetItem(tree, QStringList{"首页"});
    homeItem->setData(0, Qt::UserRole, 0);
    auto history = new QTreeWidgetItem(tree, QStringList{"历史统计"});
    history->setData(0, Qt::UserRole, 1);
    int pageIndex = 2;
    for (auto name : QStringList{"键盘统计", "鼠标统计", "手柄统计", "鼠标移动统计", "组合按键统计"}) {
        auto item = new QTreeWidgetItem(tree, QStringList{name});
        item->setData(0, Qt::UserRole, pageIndex++);
    }
    tree->expandAll();
    navigation->addWidget(tree, 1);
    auto settingButton = new QPushButton("设置", sidebar);
    navigation->addWidget(settingButton);
    connect(settingButton, &QPushButton::clicked, this, &StatsWindow::settings);
    auto save = new QPushButton("立即保存", sidebar);
    navigation->addWidget(save);
    connect(save, &QPushButton::clicked, &engine_, &wi::Engine::flush);
    auto exportButton = new QPushButton("导出全部 CSV", sidebar);
    navigation->addWidget(exportButton);
    connect(exportButton, &QPushButton::clicked, this,
            [this] { engine_.exportCsv({0, INT64_MAX, "", ""}); });
    auto clearButton = new QPushButton("清空历史", sidebar);
    clearButton->setObjectName("danger");
    navigation->addWidget(clearButton);
    connect(clearButton, &QPushButton::clicked, this, [this] {
        if (QMessageBox::warning(this, "清空历史",
                                 "将永久清空所有历史及尚未保存的计数，继续吗？",
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) == QMessageBox::Yes)
            engine_.clear();
    });
    shell->addWidget(sidebar);
    auto content = new QVBoxLayout;
    content->setContentsMargins(30, 28, 30, 22);
    content->setSpacing(16);
    shell->addLayout(content, 1);
    pages_ = new QStackedWidget(this);
    pages_->setObjectName("pageStack");
    content->addWidget(pages_, 1);
    auto home = new QWidget(pages_);
    home->setObjectName("dashboard");
    home->setAttribute(Qt::WA_StyledBackground);
    home->setAutoFillBackground(false);
    dashboard_ = home;
    auto homeLayout = new QVBoxLayout(home);
    homeLayout->setContentsMargins(0, 0, 0, 16);
    homeLayout->setSpacing(20);
    // Propagate the visible cards' combined minimum height to QScrollArea.
    homeLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    auto title = heading("首页", home);
    pageTitle_ = title;
    title->setObjectName("hero");
    auto homeHeader = new QHBoxLayout;
    homeHeader->addWidget(title);
    homeHeader->addStretch(1);
    formula_ = new QWidget(home);
    auto equation = new QGridLayout(formula_);
    equation->setContentsMargins(0, 0, 0, 0);
    equation->setHorizontalSpacing(14);
    equation->setVerticalSpacing(3);
    auto term = [&](int column, const QString &caption, bool total) {
        auto value = new QLabel("—", home);
        value->setObjectName(total ? "dailyTotal" : "dailyNumber");
        value->setAlignment(Qt::AlignCenter);
        equation->addWidget(value, 0, column);
        auto label = new QLabel(caption, home);
        label->setObjectName("muted");
        label->setAlignment(Qt::AlignCenter);
        equation->addWidget(label, 1, column);
        return value;
    };
    todayMouse_ = term(0, "鼠标输入", false);
    todayKeyboard_ = term(2, "键盘输入", false);
    todayGamepad_ = term(4, "手柄输入", false);
    todayTotal_ = term(6, "今日输入次数", true);
    for (int column : {1, 3, 5}) {
        auto sign = new QLabel(column == 5 ? "=" : "+", home);
        sign->setObjectName("dailyOperator");
        sign->setAlignment(Qt::AlignCenter);
        equation->addWidget(sign, 0, column);
    }
    homeHeader->addWidget(formula_);
    homeLayout->addLayout(homeHeader);
    timePanel_ = new QFrame(home);
    timePanel_->setObjectName("panel");
    auto timeLayout = new QVBoxLayout(timePanel_);
    timeLayout->setContentsMargins(18, 14, 18, 14);
    auto timeFields = new QHBoxLayout;
    period_ = new QComboBox(timePanel_);
    period_->addItems(
        {"今天", "昨天", "最近一周", "一月", "全部时间", "自定义范围"});
    period_->setCurrentIndex(0);
    period_->setToolTip("一月为最近一个月，所有时间范围均含起点、不含终点");
    rangeStart_ = new QDateTimeEdit(timePanel_);
    rangeEnd_ = new QDateTimeEdit(timePanel_);
    for (auto edit : {rangeStart_, rangeEnd_}) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
        edit->setMinimumWidth(190);
    }
    auto applyRange = new QPushButton("查询", timePanel_);
    timeFields->addWidget(period_);
    timeFields->addWidget(new QLabel("从", timePanel_));
    timeFields->addWidget(rangeStart_);
    timeFields->addWidget(new QLabel("至（不含）", timePanel_));
    timeFields->addWidget(rangeEnd_);
    timeFields->addWidget(applyRange);
    timeFields->addStretch();
    mouseHeatmapMode_=new QComboBox(timePanel_);
    mouseHeatmapMode_->addItems({"网格热力图","普通热力图"});
    mouseHeatmapMode_->setAccessibleName("鼠标热力图模式");
    mouseHeatmapMode_->hide();
    timeFields->addWidget(mouseHeatmapMode_);
    timeLayout->addLayout(timeFields);
    timePanel_->hide();
    homeLayout->addWidget(timePanel_);
    connect(period_, &QComboBox::currentIndexChanged, this,
            [this] { selectPeriod(); });
    connect(applyRange, &QPushButton::clicked, this, [this] { query(); });
    cardsPanel_ = new QWidget(home);
    auto cards = new QHBoxLayout(cardsPanel_);
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(36);
    auto keys = new QFrame(home);
    keys->setMinimumHeight(320);
    keys->setObjectName("panel");
    auto keyLayout = new QVBoxLayout(keys);
    keyLayout->setContentsMargins(20, 20, 20, 20);
    auto keyHeader = new QHBoxLayout;
    keyHeader->setSpacing(8);
    keyHeader->addWidget(heading("按键排行", keys));
    keyHeader->addStretch(1);
    appFilter_ = new QComboBox(keys);
    appFilter_->addItem("全部软件", "");
    appFilter_->setIconSize(QSize(22, 22));
    appFilter_->setMinimumContentsLength(8);
    appFilter_->setMinimumWidth(140);
    appFilter_->setMaximumWidth(190);
    appFilter_->setAccessibleName("按键排行软件筛选");
    appFilter_->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    appFilter_->view()->setMinimumWidth(320);
    deviceFilter_ = new QComboBox(keys);
    deviceFilter_->addItem("全部设备", "");
    deviceFilter_->addItem("键盘", "keyboard");
    deviceFilter_->addItem("鼠标", "mouse");
    deviceFilter_->addItem("手柄", "gamepad");
    deviceFilter_->setAccessibleName("按键排行设备筛选");
    keyHeader->addWidget(appFilter_);
    keyHeader->addWidget(deviceFilter_);
    keyLayout->addLayout(keyHeader);
    connect(appFilter_, &QComboBox::currentIndexChanged, this,
            [this] { query(); });
    connect(deviceFilter_, &QComboBox::currentIndexChanged, this,
            [this] { query(); });
    table_ = new QTableView(keys);
    model_ = new RankModel(this);
    table_->setModel(model_);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(44);
    table_->setShowGrid(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 4; ++i)
        table_->horizontalHeader()->setSectionResizeMode(
            i, QHeaderView::ResizeToContents);
    keyLayout->addWidget(table_, 1);
    auto apps = new QFrame(home);
    apps->setMinimumHeight(320);
    apps->setObjectName("panel");
    auto appLayout = new QVBoxLayout(apps);
    appLayout->setContentsMargins(20, 20, 20, 20);
    auto softwareHeader = new QHBoxLayout;
    softwareHeader->addWidget(heading("软件点击次数排行", apps));
    softwareHeader->addStretch(1);
    softwareDeviceFilter_ = new QComboBox(apps);
    softwareDeviceFilter_->addItem("全部设备", "");
    softwareDeviceFilter_->addItem("键盘", "keyboard");
    softwareDeviceFilter_->addItem("鼠标", "mouse");
    softwareDeviceFilter_->addItem("手柄", "gamepad");
    softwareDeviceFilter_->setAccessibleName("软件排行设备筛选");
    softwareHeader->addWidget(softwareDeviceFilter_);
    appLayout->addLayout(softwareHeader);
    connect(softwareDeviceFilter_, &QComboBox::currentIndexChanged, this,
            [this] { query(); });
    software_ = new QTableWidget(0, 2, apps);
    software_->setHorizontalHeaderLabels({"软件", "点击次数"});
    software_->verticalHeader()->hide();
    software_->verticalHeader()->setDefaultSectionSize(44);
    software_->setIconSize(QSize(28, 28));
    software_->setWordWrap(false);
    software_->horizontalHeader()->setSectionResizeMode(0,
                                                        QHeaderView::Stretch);
    software_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    software_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    software_->setSelectionBehavior(QAbstractItemView::SelectRows);
    software_->setShowGrid(false);
    appLayout->addWidget(software_, 1);
    cards->addWidget(keys, 1);
    cards->addWidget(apps, 1);
    homeLayout->addWidget(cardsPanel_, 1);
    keyboardPanel_ = new QFrame(home);
    keyboardPanel_->setObjectName("panel");
    auto keyboardLayout = new QVBoxLayout(keyboardPanel_);
    keyboardLayout->setContentsMargins(20, 20, 20, 20);
    auto keyboardHeader = new QHBoxLayout;
    keyboardHeader->addWidget(heading("键盘热力图", keyboardPanel_));
    keyboardHeader->addStretch();
    keyboardAppFilter_ = new QComboBox(keyboardPanel_);
    keyboardAppFilter_->addItem("全部软件", "");
    keyboardAppFilter_->setIconSize(QSize(22, 22));
    keyboardAppFilter_->setMinimumContentsLength(12);
    keyboardAppFilter_->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    keyboardAppFilter_->setMaximumWidth(360);
    keyboardAppFilter_->view()->setMinimumWidth(360);
    keyboardAppFilter_->setAccessibleName("键盘统计软件筛选");
    keyboardHeader->addWidget(keyboardAppFilter_);
    keyboardLayout->addLayout(keyboardHeader);
    connect(keyboardAppFilter_, &QComboBox::currentIndexChanged, this,
            [this] { query(); });
    keyboardHeatmap_ = new KeyboardHeatmap(keyboardPanel_);
    auto keyboardScroll = new QScrollArea(keyboardPanel_);
    keyboardScroll->setObjectName("keyboardScroll");
    keyboardScroll->viewport()->setObjectName("keyboardViewport");
    keyboardScroll->setWidgetResizable(true);
    keyboardScroll->setFrameShape(QFrame::NoFrame);
    keyboardScroll->setWidget(keyboardHeatmap_);
    keyboardLayout->addWidget(keyboardScroll, 1);
    keyboardPanel_->hide();
    homeLayout->addWidget(keyboardPanel_, 1);
    mouseStatistics_=new MouseStatistics(home);mouseStatistics_->hide();homeLayout->addWidget(mouseStatistics_,1);
    movementPanel_=new QFrame(home);movementPanel_->setObjectName("panel");
    auto movementLayout=new QVBoxLayout(movementPanel_);movementLayout->setContentsMargins(20,20,20,20);
    auto distanceHeader=new QHBoxLayout;
    movementTotal_=new QLabel("总移动距离：0 m",movementPanel_);movementTotal_->setObjectName("sectionTitle");distanceHeader->addWidget(movementTotal_);distanceHeader->addStretch();
    distanceHeader->addWidget(new QLabel("鼠标 DPI",movementPanel_));auto dpi=new QSpinBox(movementPanel_);dpi->setRange(50,100000);dpi->setValue(engine_.mouseDpi());dpi->setAccessibleName("鼠标 DPI");distanceHeader->addWidget(dpi);
    auto applyDpi=new QPushButton("应用 DPI",movementPanel_);distanceHeader->addWidget(applyDpi);movementLayout->addLayout(distanceHeader);
    connect(applyDpi,&QPushButton::clicked,this,[this,dpi]{QDir().mkpath(root_+"/config");QSaveFile file(root_+"/config/mouse-distance.json");const auto data=QJsonDocument(QJsonObject{{"dpi",dpi->value()}}).toJson();if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit()){QMessageBox::warning(this,"DPI 保存失败",file.errorString());return;}engine_.setMouseDpi(dpi->value());});
    movementTable_=new QTableWidget(0,3,movementPanel_);movementTable_->setHorizontalHeaderLabels({"软件","移动距离（m）","占比"});
    movementTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);movementTable_->verticalHeader()->hide();movementTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);movementTable_->setShowGrid(false);movementLayout->addWidget(movementTable_,1);
    movementPanel_->hide();homeLayout->addWidget(movementPanel_,1);
    gamepadPanel_=new QFrame(home);gamepadPanel_->setObjectName("panel");auto gamepadLayout=new QVBoxLayout(gamepadPanel_);gamepadLayout->setContentsMargins(20,20,20,20);
    auto gamepadHeader=new QHBoxLayout;auto padTitle=new QLabel("手柄热力图",gamepadPanel_);padTitle->setObjectName("sectionTitle");gamepadHeader->addWidget(padTitle);gamepadHeader->addStretch();
    gamepadAppFilter_=new QComboBox(gamepadPanel_);gamepadAppFilter_->addItem("全部软件","");gamepadAppFilter_->setIconSize(QSize(22,22));gamepadAppFilter_->setMinimumWidth(240);gamepadAppFilter_->setAccessibleName("手柄统计软件筛选");gamepadHeader->addWidget(gamepadAppFilter_);gamepadLayout->addLayout(gamepadHeader);
    gamepadHeatmap_=new GamepadHeatmap(gamepadPanel_);gamepadLayout->addWidget(gamepadHeatmap_,1);homeLayout->addWidget(gamepadPanel_,1);gamepadPanel_->hide();
    connect(gamepadAppFilter_,&QComboBox::currentIndexChanged,this,[this]{if(gamepadMode_)query();});
    combinationPanel_=new QFrame(home);combinationPanel_->setObjectName("panel");auto comboLayout=new QVBoxLayout(combinationPanel_);comboLayout->setContentsMargins(20,20,20,20);
    auto comboHeader=new QHBoxLayout;auto comboTitle=new QLabel("组合按键排行",combinationPanel_);comboTitle->setObjectName("sectionTitle");comboHeader->addWidget(comboTitle);comboHeader->addStretch();
    combinationAppFilter_=new QComboBox(combinationPanel_);combinationAppFilter_->addItem("全部软件","");combinationAppFilter_->setIconSize(QSize(22,22));combinationAppFilter_->setMinimumWidth(240);combinationAppFilter_->setAccessibleName("组合按键软件筛选");comboHeader->addWidget(combinationAppFilter_);comboLayout->addLayout(comboHeader);
    combinationTable_=new QTableWidget(0,3,combinationPanel_);combinationTable_->setHorizontalHeaderLabels({"组合按键","次数","占比"});combinationTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);combinationTable_->verticalHeader()->hide();combinationTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);combinationTable_->setShowGrid(false);comboLayout->addWidget(combinationTable_,1);homeLayout->addWidget(combinationPanel_,1);combinationPanel_->hide();
    connect(combinationAppFilter_,&QComboBox::currentIndexChanged,this,[this]{if(combinationMode_)query();});
    connect(mouseHeatmapMode_,&QComboBox::currentIndexChanged,this,[this](int index){mouseStatistics_->setGridMode(index==0);});
    connect(mouseStatistics_->softwareFilter(),&QComboBox::currentIndexChanged,this,[this]{if(mouseMode_)query();});
    activityPanel_ = new QFrame(home);
    activityPanel_->setObjectName("panel");
    auto activityLayout = new QVBoxLayout(activityPanel_);
    activityLayout->setContentsMargins(20, 16, 20, 16);
    auto activityHeader = new QHBoxLayout;
    activityHeader->addWidget(heading("时间活跃度", activityPanel_));
    activityAppFilter_ = new QComboBox(activityPanel_);
    activityAppFilter_->addItem("全部软件", "");
    activityAppFilter_->setIconSize(QSize(22, 22));
    activityAppFilter_->setMinimumContentsLength(10);
    activityAppFilter_->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    activityAppFilter_->setMaximumWidth(240);
    activityAppFilter_->view()->setMinimumWidth(320);
    activityHeader->addWidget(activityAppFilter_);
    activityLayout->addLayout(activityHeader);
    connect(activityAppFilter_, &QComboBox::currentIndexChanged, this,
            [this] { query(); });
    activityGrouping_=new QComboBox(activityPanel_);
    activityGrouping_->addItems({"按小时","按天","按周","按月","按年"});
    activityGrouping_->setAccessibleName("时间活跃度分组");activityHeader->addWidget(activityGrouping_);
    activityHeader->addStretch();
    connect(activityGrouping_,&QComboBox::currentIndexChanged,this,[this](int grouping){if(historyMode_)engine_.setActivityGrouping(grouping);});
    chart_ = new ActivityChart(activityPanel_);
    activityPanel_->setFixedHeight(560);
    activityLayout->addWidget(chart_,1);
    auto legend=new QHBoxLayout;legend->addStretch();
    for(int series=0;series<4;++series){
        auto button=new QToolButton(activityPanel_);button->setText(ActivityChart::seriesName(series));button->setCheckable(true);button->setChecked(true);button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        QPixmap swatch(14,14);swatch.fill(ActivityChart::seriesColor(series));button->setIcon(QIcon(swatch));button->setAccessibleName(ActivityChart::seriesName(series)+"柱显示开关");
        button->setStyleSheet("QToolButton{color:#60717f;border:1px solid transparent;padding:6px 12px;border-radius:4px;}QToolButton:checked{color:#e4edf3;}QToolButton:hover{border-color:#637585;}");
        connect(button,&QToolButton::toggled,this,[this,series](bool visible){chart_->setSeriesVisible(series,visible);});legend->addWidget(button);
    }
    legend->addStretch();activityLayout->addLayout(legend);
    activityPanel_->hide();
    homeLayout->insertWidget(homeLayout->indexOf(cardsPanel_),activityPanel_);
    auto dashboardScroll = new QScrollArea(pages_);
    dashboardScroll_=dashboardScroll;
    dashboardScroll->viewport()->setObjectName("dashboardViewport");
    dashboardScroll->setFrameShape(QFrame::NoFrame);
    dashboardScroll->setWidgetResizable(true);
    dashboardScroll->setWidget(home);
    dashboardScroll->viewport()->installEventFilter(this);
    home->setAutoFillBackground(false);
    pages_->addWidget(dashboardScroll);
    for (auto name : QStringList{"手柄统计"}) {
        auto page = new QWidget(pages_);
        auto layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        auto headingLabel = heading(name, page);
        headingLabel->setObjectName("hero");
        layout->addWidget(headingLabel);
        auto placeholder = new QFrame(page);
        placeholder->setObjectName("panel");
        auto empty = new QVBoxLayout(placeholder);
        empty->addStretch();
        auto label =
            new QLabel(name == "输入历史" ? "从左侧选择输入类型" : "页面准备中",
                       placeholder);
        label->setAlignment(Qt::AlignCenter);
        label->setObjectName("sectionTitle");
        empty->addWidget(label);
        empty->addStretch();
        layout->addWidget(placeholder, 1);
        pages_->addWidget(page);
    }
    message_ = new QLabel(this);
    message_->setObjectName("message");
    message_->setWordWrap(true);
    message_->hide();
    content->addWidget(message_);
    status_ = new QLabel(this);
    status_->setObjectName("muted");
    status_->setWordWrap(true);
    content->addWidget(status_);
    setStyleSheet(R"(
QMainWindow,QDialog{background:#101922;color:#dce7ee;}
QWidget#central,QWidget#dashboard,QWidget#dashboardViewport,QStackedWidget#pageStack{background:#101922;}
QWidget#activityViewport,QScrollArea#activityScroll,QWidget#keyboardViewport,QScrollArea#keyboardScroll{background:#17232e;}
QWidget{color:#dce7ee;font-family:'Microsoft YaHei UI';font-size:13px;}
QFrame#sidebar{background:#14212c;border-right:1px solid #2e3e4b;}
QLabel#brand{font-size:24px;font-weight:700;color:#a9edc8;}QLabel#hero{font-size:26px;font-weight:600;}QLabel#muted{color:#91a7b7;font-size:12px;}QLabel#sectionTitle{font-size:17px;font-weight:600;margin-bottom:8px;}
QLabel#dailyNumber{font-size:28px;font-weight:600;}QLabel#dailyTotal{font-size:40px;font-weight:700;color:#a9edc8;}QLabel#dailyOperator{font-size:26px;color:#91a7b7;}
QFrame#panel{background:#17232e;border:1px solid #2e3e4b;border-radius:10px;}
QTreeWidget#navigation{background:transparent;border:0;outline:0;}QTreeWidget#navigation::item{height:42px;border-radius:6px;padding-left:4px;}QTreeWidget#navigation::item:selected{background:#345b50;color:#c8f5dc;}QTreeWidget#navigation::item:hover{background:#263b48;}
QPushButton,QSpinBox,QComboBox,QDateTimeEdit{background:#1b2a36;border:1px solid #3b505f;border-radius:5px;padding:7px 10px;min-height:20px;}QPushButton:hover{border-color:#a9edc8;background:#263b48;}QPushButton#danger{color:#ecabab;}
QComboBox QAbstractItemView{background:#1b2a36;color:#dce7ee;selection-background-color:#345b50;}
QSlider::groove:horizontal{height:6px;background:#344754;border-radius:3px;}QSlider::handle:horizontal{width:16px;margin:-6px 0;background:#a9edc8;border-radius:8px;}
QTableView{background:#17232e;border:0;selection-background-color:#345b50;color:#dce7ee;}QHeaderView::section{background:#17232e;color:#94acbc;border:0;border-bottom:1px solid #344754;padding:10px;}QTableView::item{padding:7px;border-bottom:1px solid #253744;}
QScrollBar{background:#17232e;}QLabel#message{background:#3e3423;color:#f0d59c;padding:10px;border-radius:6px;}QToolTip{background:#233747;color:#e4edf3;border:1px solid #536978;}
QScrollArea{background:#101922;border:0;}
)");
    connect(tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                if (!item || !item->data(0, Qt::UserRole).isValid())
                    return;
                selectPage(item->data(0, Qt::UserRole).toInt());
            });
    connect(&engine_, &wi::Engine::viewReady, this, &StatsWindow::receive,
            Qt::QueuedConnection);
    connect(
        &engine_, &wi::Engine::notice, this,
        [this](const QString &message) {
            message_->setText(message);
            message_->show();
        },
        Qt::QueuedConnection);
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (isVisible() && !isMinimized()) {
            if (++nameRefreshTicks_ >= 10) {
                nameRefreshTicks_ = 0;
                refreshApplicationNames();
            }
            if (pages_->currentIndex() == 0) {
                if (queryDate_ != QDate::currentDate()) {
                    if ((historyMode_ || keyboardMode_ || mouseMode_ || movementMode_ || gamepadMode_ || combinationMode_) &&
                        period_->currentIndex() != 5)
                        selectPeriod();
                    else
                        query();
                } else
                    engine_.refresh();
            }
            updateStatus();
        }
    });
    selectPeriod();
    tree->setCurrentItem(homeItem);
}
wi::ViewFilter StatsWindow::filter() const {
    const auto date = QDate::currentDate();
    const bool ranged = historyMode_ || keyboardMode_ || mouseMode_ || movementMode_ || gamepadMode_ || combinationMode_;
    const bool all = ranged && period_->currentIndex() == 4;
    wi::ViewFilter selected{all      ? 0
            : ranged ? rangeStart_->dateTime().toSecsSinceEpoch()
                     : date.startOfDay().toSecsSinceEpoch(),
            all      ? INT64_MAX
            : ranged ? rangeEnd_->dateTime().toSecsSinceEpoch()
                     : date.addDays(1).startOfDay().toSecsSinceEpoch(),
            gamepadMode_?std::string("gamepad"):mouseMode_?std::string("mouse"):keyboardMode_
                ? std::string("keyboard")
                : deviceFilter_->currentData().toString().toStdString(),
            (combinationMode_?combinationAppFilter_:(gamepadMode_?gamepadAppFilter_:(mouseMode_?mouseStatistics_->softwareFilter():(keyboardMode_ ? keyboardAppFilter_ : appFilter_))))
                ->currentData()
                .toString()
                .toStdString(),
            softwareDeviceFilter_->currentData().toString().toStdString(),
            std::string{},
            activityAppFilter_->currentData().toString().toStdString(),
            activityGrouping_->currentIndex()};
    selected.mouseDetails=mouseMode_;
    if(movementMode_){
        selected.start-=selected.start%60;
        if(selected.end!=INT64_MAX)selected.end-=selected.end%60;
    }
    return selected;
}
void StatsWindow::selectPage(int index) {
    if(mouseMode_&&index!=3)mouseStatistics_->clearRendering();
    if(historyMode_||keyboardMode_||mouseMode_||movementMode_||gamepadMode_||combinationMode_)timeSelections_[currentPage_]={period_->currentIndex(),rangeStart_->dateTime(),rangeEnd_->dateTime()};
    currentPage_=index;
    historyMode_ = index == 1;
    keyboardMode_ = index == 2;
    mouseMode_=index==3;
    movementMode_=index==5;
    gamepadMode_=index==4;
    combinationMode_=index==6;
    rangeStart_->setDisplayFormat(movementMode_?"yyyy-MM-dd HH:mm":"yyyy-MM-dd HH:mm:ss");
    rangeEnd_->setDisplayFormat(movementMode_?"yyyy-MM-dd HH:mm":"yyyy-MM-dd HH:mm:ss");
    pages_->setCurrentIndex(0);
    pageTitle_->setText(historyMode_    ? "历史统计"
                        : keyboardMode_ ? "键盘统计"
                        : mouseMode_?"鼠标统计":movementMode_?"鼠标移动统计":gamepadMode_?"手柄统计":combinationMode_?"组合按键统计":"首页");
    formula_->setVisible(index == 0);
    cardsPanel_->setVisible(!keyboardMode_&&!mouseMode_&&!movementMode_&&!gamepadMode_&&!combinationMode_);
    cardsPanel_->setMinimumHeight(historyMode_ ? 560 : 0);
    combinationPanel_->setVisible(combinationMode_);
    gamepadPanel_->setVisible(gamepadMode_);
    movementPanel_->setVisible(movementMode_);
    keyboardPanel_->setVisible(keyboardMode_);
    mouseStatistics_->setVisible(mouseMode_);
    mouseHeatmapMode_->setVisible(mouseMode_);
    timePanel_->setVisible(historyMode_ || keyboardMode_ || mouseMode_ || movementMode_ || gamepadMode_ || combinationMode_);
    activityPanel_->setVisible(historyMode_);
    updateDashboardLayout();
    message_->hide();
    if(historyMode_||keyboardMode_||mouseMode_||movementMode_||gamepadMode_||combinationMode_){
        const auto selection=timeSelections_[index];QSignalBlocker blocker(period_);period_->setCurrentIndex(selection.period);
        if(selection.start.isValid())rangeStart_->setDateTime(selection.start);
        if(selection.end.isValid())rangeEnd_->setDateTime(selection.end);
        rangeStart_->setEnabled(selection.period==5);rangeEnd_->setEnabled(selection.period==5);
    }
    if ((historyMode_ || keyboardMode_ || mouseMode_ || movementMode_ || gamepadMode_ || combinationMode_) && period_->currentIndex() != 5)
        selectPeriod();
    else
        query();
}
void StatsWindow::selectPeriod() {
    const bool custom = period_->currentIndex() == 5;
    rangeStart_->setEnabled(custom);
    rangeEnd_->setEnabled(custom);
    if (custom)
        return;
    if (period_->currentIndex() == 4) {
        query();
        return;
    }
    const auto date = QDate::currentDate();
    auto start = date;
    auto end = date.addDays(1);
    switch (period_->currentIndex()) {
    case 1:
        start = date.addDays(-1);
        end = date;
        break;
    case 2:
        start = date.addDays(-6);
        break;
    case 3:
        start = end.addMonths(-1);
        break;
    }
    rangeStart_->setDateTime(start.startOfDay());
    rangeEnd_->setDateTime(end.startOfDay());
    query();
}
void StatsWindow::query() {
    queryDate_ = QDate::currentDate();
    if (pages_->currentIndex() != 0) {
        engine_.unwatch();
        return;
    }
    auto selected = filter();
    if (selected.start >= selected.end) {
        message_->setText("起始时间必须早于结束时间，区间含起点、不含终点。");
        message_->show();
        return;
    }
    message_->hide();
    if (isVisible() && !isMinimized()) {
        refreshApplicationNames();
        if(mouseMode_)mouseStatistics_->clearRendering();
        appliedFilter_ = selected;
        model_->replace(wi::ViewResult{});
        engine_.watch(selected, ++request_);
    }
}
void StatsWindow::receive(std::shared_ptr<const wi::ViewResult> result) {
    if (result->request != request_ || !isVisible() || isMinimized() ||
        exiting_ || pages_->currentIndex() != 0)
        return;
    if (!result->error.isEmpty()) {
        message_->setText("数据读取失败：" + result->error +
                          "。当前结果可能未更新。");
        message_->show();
        return;
    }
    for (auto combo : {appFilter_, activityAppFilter_, keyboardAppFilter_,mouseStatistics_->softwareFilter(),gamepadAppFilter_,combinationAppFilter_}) {
        const auto selected = combo->currentData().toString();
        bool changed = combo->count() != int(result->applications.size()) + 1;
        if (!changed) {
            int i = 1;
            for (const auto &[id, name] : result->applications) {
                if (combo->itemData(i).toString() !=
                        QString::fromStdString(id) ||
                    combo->itemText(i) != applicationLabels_.label(
                                              QString::fromStdString(id),
                                              QString::fromStdString(name))) {
                    changed = true;
                    break;
                }
                ++i;
            }
        }
        if (changed) {
            QSignalBlocker blocker(combo);
            const auto oldName = combo->currentText();
            combo->clear();
            combo->addItem("全部软件", "");
            for (const auto &[id, name] : result->applications) {
                auto key = QString::fromStdString(id);
                combo->addItem(
                    applicationIcon(key),
                    applicationLabels_.label(key, QString::fromStdString(name)),
                    key);
                combo->setItemData(combo->count() - 1, key, Qt::ToolTipRole);
                combo->setItemData(combo->count() - 1,
                                   QString::fromStdString(name),
                                   Qt::UserRole + 1);
            }
            int index = combo->findData(selected);
            if (index < 0 && !selected.isEmpty()) {
                combo->addItem(oldName, selected);
                index = combo->count() - 1;
            }
            combo->setCurrentIndex(std::max(0, index));
        }
    }
    if ((historyMode_ || keyboardMode_ || mouseMode_ || movementMode_ || gamepadMode_ || combinationMode_) && period_->currentIndex() == 4) {
        rangeStart_->setDateTime(
            QDateTime::fromSecsSinceEpoch(result->rangeStart).toLocalTime());
        rangeEnd_->setDateTime(
            QDateTime::fromSecsSinceEpoch(result->rangeEnd).toLocalTime());
    }
    if(mouseMode_){mouseStatistics_->setStatistics(*result);updateStatus();return;}
    if(gamepadMode_){gamepadHeatmap_->setStatistics(*result);updateStatus();return;}
    if(combinationMode_){
        wi::Count total=0;for(const auto& row:result->combinations)total+=row.count;
        combinationTable_->setRowCount(std::max(1,int(result->combinations.size())));combinationTable_->clearContents();int index=0;
        for(const auto& row:result->combinations){combinationTable_->setItem(index,0,new QTableWidgetItem(QString::fromStdString(row.name)));combinationTable_->setItem(index,1,new QTableWidgetItem(number(row.count)));combinationTable_->setItem(index,2,new QTableWidgetItem(QString::number(total?100.0*row.count/total:0,'f',2)+"%"));++index;}
        if(result->combinations.empty())combinationTable_->setItem(0,0,new QTableWidgetItem("这个时段没有组合按键"));updateStatus();return;
    }
    if(movementMode_){
        wi::Count total=0;for(const auto& row:result->movement)total+=row.count;
        movementTotal_->setText("总移动距离："+QLocale().toString(double(total)/1000000000,'f',4)+" m");
        movementTable_->setRowCount(std::max(1,int(result->movement.size())));
        movementTable_->clearContents();int index=0;
        for(const auto& row:result->movement){
            const auto id=QString::fromStdString(row.id),fallback=QString::fromStdString(row.name);
            auto name=new QTableWidgetItem(applicationIcon(id),applicationLabels_.label(id,fallback));name->setData(Qt::UserRole,id);name->setData(Qt::UserRole+1,fallback);movementTable_->setItem(index,0,name);
            movementTable_->setItem(index,1,new QTableWidgetItem(QLocale().toString(double(row.count)/1000000000,'f',4)));
            movementTable_->setItem(index,2,new QTableWidgetItem(QString::number(total?100.0*row.count/total:0,'f',2)+"%"));++index;
        }
        if(result->movement.empty())movementTable_->setItem(0,0,new QTableWidgetItem("这个时段没有鼠标移动"));
        updateStatus();return;
    }
    if (keyboardMode_) {
        keyboardHeatmap_->setStatistics(*result);
        updateStatus();
        return;
    }
    todayMouse_->setText(number(result->todayMouse));
    todayKeyboard_->setText(number(result->todayKeyboard));
    todayGamepad_->setText(number(result->todayGamepad));
    todayTotal_->setText(number(result->todayMouse + result->todayKeyboard +
                                result->todayGamepad));
    int rowScroll = table_->verticalScrollBar()->value();
    model_->replace(*result);
    table_->verticalScrollBar()->setValue(rowScroll);
    int softwareScroll = software_->verticalScrollBar()->value();
    software_->setRowCount(int(result->software.size()));
    int i = 0;
    for (auto &entry : result->software) {
        auto id = QString::fromStdString(entry.id);
        const auto label =
            applicationLabels_.label(id, QString::fromStdString(entry.name));
        auto item = new QTableWidgetItem(applicationIcon(id), label);
        item->setToolTip(label + "\n" + id);
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, QString::fromStdString(entry.name));
        software_->setItem(i, 0, item);
        auto value = new QTableWidgetItem(number(entry.count));
        value->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        software_->setItem(i, 1, value);
        ++i;
    }
    if (result->software.empty()) {
        software_->setRowCount(1);
        software_->setItem(0, 0, new QTableWidgetItem("当前设备暂无点击记录"));
    }
    software_->verticalScrollBar()->setValue(softwareScroll);
    if (message_->text().startsWith("数据读取失败"))
        message_->hide();
    if (historyMode_) {
        chart_->setData(*result);
        if (period_->currentIndex() == 4) {
            rangeStart_->setDateTime(
                QDateTime::fromSecsSinceEpoch(result->rangeStart)
                    .toLocalTime());
            rangeEnd_->setDateTime(
                QDateTime::fromSecsSinceEpoch(result->rangeEnd).toLocalTime());
        }
    }
    updateStatus();
}
void StatsWindow::refreshApplicationNames() {
    applicationLabels_.refresh();
    for(int i=0;i<movementTable_->rowCount();++i){auto item=movementTable_->item(i,0);if(item&&!item->data(Qt::UserRole).toString().isEmpty())item->setText(applicationLabels_.label(item->data(Qt::UserRole).toString(),item->data(Qt::UserRole+1).toString()));}
    for (auto combo : {appFilter_, activityAppFilter_, keyboardAppFilter_,mouseStatistics_->softwareFilter(),gamepadAppFilter_,combinationAppFilter_}) {
        QSignalBlocker blocker(combo);
        for (int i = 1; i < combo->count(); ++i) {
            const auto title = applicationLabels_.label(
                combo->itemData(i).toString(),
                combo->itemData(i, Qt::UserRole + 1).toString());
            if (combo->itemText(i) != title)
                combo->setItemText(i, title);
        }
    }
    for (int i = 0; i < software_->rowCount(); ++i) {
        auto item = software_->item(i, 0);
        if (!item || item->data(Qt::UserRole).toString().isEmpty())
            continue;
        const auto id = item->data(Qt::UserRole).toString();
        const auto title = applicationLabels_.label(
            id, item->data(Qt::UserRole + 1).toString());
        if (item->text() != title) {
            item->setText(title);
            item->setToolTip(title + "\n" + id);
        }
    }
}
QIcon StatsWindow::applicationIcon(const QString &id) {
    if (auto it = applicationIcons_.constFind(id);
        it != applicationIcons_.cend())
        return it.value();
    const auto fallback = style()->standardIcon(
        id == "system" ? QStyle::SP_ComputerIcon : QStyle::SP_FileIcon);
    if (id == "system" || id == "unknown" || id.isEmpty()) {
        applicationIcons_.insert(id, fallback);
        return fallback;
    }
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (iconRetryAt_.value(id) > now)
        return fallback;
    const auto directory = root_ + "/cache/app-icons";
    const auto key = QCryptographicHash::hash(
                         QDir::fromNativeSeparators(id).toCaseFolded().toUtf8(),
                         QCryptographicHash::Sha256)
                         .toHex();
    const auto filename = directory + "/" + QString::fromLatin1(key) + ".png";
    QPixmap pixmap;
    if (!pixmap.load(filename)) {
        // Query the executable's shell icon only when the disk cache is missing
        // or unreadable. This does not open the application's process.
        if (QFileInfo::exists(id)) {
            QFileIconProvider provider;
            pixmap = provider.icon(QFileInfo(id)).pixmap(QSize(64, 64));
        }
        if (pixmap.isNull()) {
            iconRetryAt_.insert(id, now + 60000);
            return fallback;
        }
        QSaveFile file(filename);
        if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) ||
            !pixmap.save(&file, "PNG") || !file.commit()) {
            // Keep the icon usable in memory if the runtime directory cannot
            // be written; do not redirect the cache to AppData.
            qWarning("Unable to save application icon cache: %s",
                     qPrintable(filename));
        }
    }
    QIcon icon(pixmap);
    applicationIcons_.insert(id, icon);
    iconRetryAt_.remove(id);
    return icon;
}
void StatsWindow::updateStatus() {
    auto state = engine_.status();
    status_->setText("最近一次保存时间为 " +
                     (state.lastCommit
                          ? QDateTime::fromSecsSinceEpoch(state.lastCommit)
                                .toLocalTime()
                                .toString("yyyy-MM-dd HH:mm:ss")
                          : "尚未保存"));
    if (!state.storageError.isEmpty()) {
        message_->setText("保存失败：" + state.storageError);
        message_->show();
    } else if (!state.inputError.isEmpty()) {
        message_->setText("采集异常：" + state.inputError);
        message_->show();
    } else if (message_->text().startsWith("保存失败：") ||
               message_->text().startsWith("采集异常："))
        message_->hide();
}
void StatsWindow::settings() {
    QDialog dialog(this);
    dialog.setWindowTitle("设置");
    auto layout = new QVBoxLayout(&dialog);
    auto form = new QFormLayout;
    auto seconds = new QSpinBox(&dialog);
    seconds->setRange(1, 3600);
    seconds->setSuffix(" 秒");
    seconds->setValue(engine_.status().interval);
    form->addRow("自动保存间隔", seconds);
    layout->addLayout(form);
    auto hint = new QLabel(
        "默认 60 秒。修改后立即开始新的计时。\n界面约每 100 "
        "毫秒刷新，正常退出时立即保存。\n异常退出可能丢失尚未保存的数据。",
        &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText("保存");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QDir().mkpath(root_ + "/config");
    QSaveFile file(root_ + "/config/settings.json");
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "设置保存失败", file.errorString());
        return;
    }
    auto data =
        QJsonDocument(QJsonObject{{"saveIntervalSeconds", seconds->value()}})
            .toJson();
    if (file.write(data) != data.size() || !file.commit()) {
        QMessageBox::warning(this, "设置保存失败", file.errorString());
        return;
    }
    engine_.setInterval(seconds->value());
    updateStatus();
}
void StatsWindow::open() {
    if (exiting_)
        return;
    showNormal();
    raise();
    activateWindow();
    timer_.start();
    query();
}
void StatsWindow::updateDashboardLayout(){
    if(!dashboardScroll_)return;
    const int cardHeight=std::clamp(dashboardScroll_->viewport()->height()*3/4,420,720);
    if(activityPanel_->height()!=cardHeight)activityPanel_->setFixedHeight(cardHeight);
    dashboard_->layout()->invalidate();
    dashboard_->layout()->activate();
    dashboard_->updateGeometry();
}
bool StatsWindow::eventFilter(QObject* watched,QEvent* event){
    if(dashboardScroll_&&watched==dashboardScroll_->viewport()&&event->type()==QEvent::Resize)
        updateDashboardLayout();
    return QMainWindow::eventFilter(watched,event);
}
void StatsWindow::prepareExit() {
    exiting_ = true;
    timer_.stop();
    engine_.unwatch();
    mouseStatistics_->clearRendering();
    setEnabled(false);
    updateStatus();
}
void StatsWindow::closeEvent(QCloseEvent *event) {
    event->ignore();
    hide();
    timer_.stop();
    engine_.unwatch();
    mouseStatistics_->clearRendering();
}
void StatsWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    if (!exiting_) {
        timer_.start();
        query();
    }
}
void StatsWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && !exiting_) {
        if (isMinimized()) {
            timer_.stop();
            engine_.unwatch();
            mouseStatistics_->clearRendering();
        } else if (isVisible()) {
            timer_.start();
            query();
        }
    }
}
