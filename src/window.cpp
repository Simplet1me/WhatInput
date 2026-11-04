#include "window.h"
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
    auto history = new QTreeWidgetItem(tree, QStringList{"输入历史"});
    history->setData(0, Qt::UserRole, 1);
    for (auto name : QStringList{"键盘输入", "鼠标输入", "手柄输入"}) {
        auto item = new QTreeWidgetItem(history, QStringList{name});
        item->setData(0, Qt::UserRole, history->childCount() + 1);
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
    content->addWidget(pages_, 1);
    auto home = new QWidget(pages_);
    auto homeLayout = new QVBoxLayout(home);
    homeLayout->setContentsMargins(0, 0, 0, 0);
    homeLayout->setSpacing(20);
    auto title = heading("首页", home);
    title->setObjectName("hero");
    auto homeHeader = new QHBoxLayout;
    homeHeader->addWidget(title);
    homeHeader->addStretch(1);
    auto equation = new QGridLayout;
    equation->setHorizontalSpacing(14);
    equation->setVerticalSpacing(3);
    auto term = [&](int column, const QString& caption, bool total) {
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
    homeHeader->addLayout(equation);
    homeLayout->addLayout(homeHeader);
    auto cards = new QHBoxLayout;
    cards->setSpacing(36);
    auto keys = new QFrame(home);
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
    homeLayout->addLayout(cards, 1);
    pages_->addWidget(home);
    for (auto name :
         QStringList{"输入历史", "键盘输入", "鼠标输入", "手柄输入"}) {
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
        auto hint =
            new QLabel("历史明细与筛选功能将在后续版本加入。", placeholder);
        hint->setAlignment(Qt::AlignCenter);
        hint->setObjectName("muted");
        empty->addWidget(hint);
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
    updated_ = new QLabel(this);
    updated_->setObjectName("muted");
    content->addWidget(updated_);
    setStyleSheet(R"(
QMainWindow,QDialog{background:#101922;color:#dce7ee;}
QWidget{color:#dce7ee;font-family:'Microsoft YaHei UI';font-size:13px;}
QFrame#sidebar{background:#14212c;border-right:1px solid #2e3e4b;}
QLabel#brand{font-size:24px;font-weight:700;color:#a9edc8;}QLabel#hero{font-size:26px;font-weight:600;}QLabel#muted{color:#91a7b7;font-size:12px;}QLabel#sectionTitle{font-size:17px;font-weight:600;margin-bottom:8px;}
QLabel#dailyNumber{font-size:28px;font-weight:600;}QLabel#dailyTotal{font-size:40px;font-weight:700;color:#a9edc8;}QLabel#dailyOperator{font-size:26px;color:#91a7b7;}
QFrame#panel{background:#17232e;border:1px solid #2e3e4b;border-radius:10px;}
QTreeWidget#navigation{background:transparent;border:0;outline:0;}QTreeWidget#navigation::item{height:42px;border-radius:6px;padding-left:4px;}QTreeWidget#navigation::item:selected{background:#345b50;color:#c8f5dc;}QTreeWidget#navigation::item:hover{background:#263b48;}
QPushButton,QSpinBox,QComboBox{background:#1b2a36;border:1px solid #3b505f;border-radius:5px;padding:7px 10px;min-height:20px;}QPushButton:hover{border-color:#a9edc8;background:#263b48;}QPushButton#danger{color:#ecabab;}
QComboBox QAbstractItemView{background:#1b2a36;color:#dce7ee;selection-background-color:#345b50;}
QTableView{background:#17232e;border:0;selection-background-color:#345b50;color:#dce7ee;}QHeaderView::section{background:#17232e;color:#94acbc;border:0;border-bottom:1px solid #344754;padding:10px;}QTableView::item{padding:7px;border-bottom:1px solid #253744;}
QScrollBar{background:#17232e;}QLabel#message{background:#3e3423;color:#f0d59c;padding:10px;border-radius:6px;}QToolTip{background:#233747;color:#e4edf3;border:1px solid #536978;}
)");
    connect(tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                if (!item || !item->data(0, Qt::UserRole).isValid())
                    return;
                pages_->setCurrentIndex(item->data(0, Qt::UserRole).toInt());
                message_->hide();
                query();
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
            if (pages_->currentIndex() == 0)
                engine_.refresh();
            updateStatus();
        }
    });
    tree->setCurrentItem(homeItem);
}
wi::ViewFilter StatsWindow::filter() const {
    return {0, INT64_MAX, deviceFilter_->currentData().toString().toStdString(),
            appFilter_->currentData().toString().toStdString(),
            softwareDeviceFilter_->currentData().toString().toStdString()};
}
void StatsWindow::query() {
    if (pages_->currentIndex() != 0) {
        engine_.unwatch();
        updated_->setText("历史页面准备中 · 后台采集继续");
        return;
    }
    if (isVisible() && !isMinimized()) {
        model_->replace(wi::ViewResult{});
        engine_.watch(filter(), ++request_);
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
    const auto selected = appFilter_->currentData().toString();
    bool changed = appFilter_->count() != int(result->applications.size()) + 1;
    if (!changed) {
        int index = 1;
        for (const auto &[id, name] : result->applications) {
            if (appFilter_->itemData(index).toString() !=
                    QString::fromStdString(id) ||
                appFilter_->itemText(index) != QString::fromStdString(name)) {
                changed = true;
                break;
            }
            ++index;
        }
    }
    if (changed) {
        QSignalBlocker blocker(appFilter_);
        const auto oldName = appFilter_->currentText();
        appFilter_->clear();
        appFilter_->addItem("全部软件", "");
        for (const auto &[id, name] : result->applications) {
            auto key = QString::fromStdString(id);
            appFilter_->addItem(applicationIcon(key),
                                QString::fromStdString(name), key);
            appFilter_->setItemData(appFilter_->count() - 1, key,
                                    Qt::ToolTipRole);
        }
        int index = appFilter_->findData(selected);
        if (index < 0 && !selected.isEmpty()) {
            appFilter_->addItem(oldName, selected);
            index = appFilter_->count() - 1;
        }
        appFilter_->setCurrentIndex(std::max(0, index));
    }
    todayMouse_->setText(number(result->todayMouse));
    todayKeyboard_->setText(number(result->todayKeyboard));
    todayGamepad_->setText(number(result->todayGamepad));
    todayTotal_->setText(number(result->todayMouse + result->todayKeyboard + result->todayGamepad));
    int rowScroll = table_->verticalScrollBar()->value();
    model_->replace(*result);
    table_->verticalScrollBar()->setValue(rowScroll);
    int softwareScroll = software_->verticalScrollBar()->value();
    software_->setRowCount(int(result->software.size()));
    int i = 0;
    for (auto &entry : result->software) {
        auto id = QString::fromStdString(entry.id);
        auto item = new QTableWidgetItem(applicationIcon(id),
                                         QString::fromStdString(entry.name));
        item->setToolTip(QString::fromStdString(entry.name) + "\n" + id);
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
    updated_->setText("全部时间 · 更新于 " +
                      result->updated.toString("HH:mm:ss") +
                      " · 包含尚未保存的计数");
    updateStatus();
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
    QString text = exiting_       ? "正在保存并退出"
                   : state.saving ? "正在保存"
                                  : "正在统计";
    text += " · 每 " + QString::number(state.interval) + " 秒保存 · 待保存 " +
            number(state.unsaved) + " 次";
    if (state.lastCommit)
        text += " · 最近落盘 " + QDateTime::fromSecsSinceEpoch(state.lastCommit)
                                     .toLocalTime()
                                     .toString("HH:mm:ss");
    if (!state.storageError.isEmpty())
        text = "保存失败，增量仍保留在内存：" + state.storageError;
    if (!state.inputError.isEmpty())
        text = "采集异常：" + state.inputError;
    status_->setText(text);
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
void StatsWindow::prepareExit() {
    exiting_ = true;
    timer_.stop();
    engine_.unwatch();
    setEnabled(false);
    updateStatus();
}
void StatsWindow::closeEvent(QCloseEvent *event) {
    event->ignore();
    hide();
    timer_.stop();
    engine_.unwatch();
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
        } else if (isVisible()) {
            timer_.start();
            query();
        }
    }
}
