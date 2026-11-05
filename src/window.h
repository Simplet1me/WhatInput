#pragma once
#include "application_labels.h"
#include "engine.h"
#include <QAbstractTableModel>
#include <QHash>
#include <QIcon>
#include <QMainWindow>
#include <QTimer>
#include <optional>
class QDateTimeEdit;
class QComboBox;
class QLabel;
class QPushButton;
class QTableView;
class QTableWidget;
class QCloseEvent;
class QShowEvent;
class QStackedWidget;
class QSlider;
class RankModel;
class ActivityChart;
class KeyboardHeatmap;
class MouseStatistics;
class GamepadHeatmap;

class StatsWindow : public QMainWindow {
    Q_OBJECT
  public:
    StatsWindow(wi::Engine &engine, QString root, QWidget *parent = nullptr);
    void open();
    void prepareExit();

  protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    wi::Engine &engine_;
    QString root_;
    ApplicationLabels applicationLabels_;
    QLabel *status_, *message_, *pageTitle_;
    QTableView *table_;
    QTableWidget *software_;
    RankModel *model_;
    QStackedWidget *pages_;
    QComboBox *appFilter_, *deviceFilter_, *softwareDeviceFilter_;
    QComboBox *activityAppFilter_, *activityDeviceFilter_, *period_;
    QComboBox *keyboardAppFilter_;
    QComboBox* mouseHeatmapMode_;
    QWidget* movementPanel_;
    QTableWidget* movementTable_;
    QLabel* movementTotal_;
    bool movementMode_=false;
    bool gamepadMode_=false;
    QWidget* gamepadPanel_;
    GamepadHeatmap* gamepadHeatmap_;
    QComboBox* gamepadAppFilter_;
    bool combinationMode_=false;
    QWidget* combinationPanel_;
    QComboBox* combinationAppFilter_;
    QTableWidget* combinationTable_;
    QDateTimeEdit *rangeStart_, *rangeEnd_;
    QWidget *timePanel_, *activityPanel_, *formula_, *dashboard_;
    QWidget *cardsPanel_, *keyboardPanel_;
    KeyboardHeatmap *keyboardHeatmap_;
    MouseStatistics* mouseStatistics_;
    ActivityChart *chart_;
    QSlider *activityZoom_;
    QLabel *todayMouse_, *todayKeyboard_, *todayGamepad_, *todayTotal_;
    QTimer timer_;
    int nameRefreshTicks_ = 0;
    QHash<QString, QIcon> applicationIcons_;
    QHash<QString, qint64> iconRetryAt_;
    QIcon applicationIcon(const QString &id);
    quint64 request_ = 0;
    bool exiting_ = false;
    bool historyMode_ = false;
    bool keyboardMode_ = false;
    bool mouseMode_=false;
    int currentPage_=0;
    struct TimeSelection {int period=0;QDateTime start,end;};
    std::map<int,TimeSelection> timeSelections_;
    QDate queryDate_;
    wi::ViewFilter appliedFilter_;
    void selectPeriod();
    void selectPage(int index);
    wi::ViewFilter filter() const;
    void query();
    void receive(std::shared_ptr<const wi::ViewResult> result);
    void updateStatus();
    void settings();
    void refreshApplicationNames();
};
