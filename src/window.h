#pragma once
#include "engine.h"
#include <QMainWindow>
#include <QAbstractTableModel>
#include <QTimer>
#include <QHash>
#include <QIcon>
#include <optional>
class QDateTimeEdit;
class QComboBox;
class QLabel;
class QPushButton;
class QTableView;
class QTableWidget;
class QCloseEvent;
class QShowEvent;
class ActivityChart;
class RankModel;

class StatsWindow : public QMainWindow {
    Q_OBJECT
public:
    StatsWindow(wi::Engine& engine,QString root,QWidget* parent=nullptr);
    void open();
    void prepareExit();
protected:
    void closeEvent(QCloseEvent* event)override;
    void showEvent(QShowEvent* event)override;
    void changeEvent(QEvent* event)override;
private:
    wi::Engine& engine_;
    QString root_;
    QDateTimeEdit *start_,*end_;
    QComboBox *type_,*app_;
    QLabel *total_,*status_,*range_,*updated_,*message_;
    QPushButton *back_;
    QTableView* table_;
    QTableWidget* software_;
    RankModel* model_;
    ActivityChart* chart_;
    QTimer timer_;
    QHash<QString,QIcon> applicationIcons_;
    QHash<QString,qint64> iconRetryAt_;
    QIcon applicationIcon(const QString& id);
    quint64 request_=0;
    bool exiting_=false,allTime_=false;
    struct OriginalRange { QDateTime start,end;bool all; };
    std::optional<OriginalRange> original_;
    wi::ViewFilter filter()const;
    void query();
    void preset(int days);
    void receive(std::shared_ptr<const wi::ViewResult> result);
    void updateStatus();
    void settings();
};
