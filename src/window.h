#pragma once
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
class RankModel;

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
    QLabel *status_, *updated_, *message_;
    QTableView *table_;
    QTableWidget *software_;
    RankModel *model_;
    QStackedWidget *pages_;
    QComboBox *appFilter_, *deviceFilter_, *softwareDeviceFilter_;
    QLabel *todayMouse_, *todayKeyboard_, *todayGamepad_, *todayTotal_;
    QTimer timer_;
    QHash<QString, QIcon> applicationIcons_;
    QHash<QString, qint64> iconRetryAt_;
    QIcon applicationIcon(const QString &id);
    quint64 request_ = 0;
    bool exiting_ = false;
    wi::ViewFilter filter() const;
    void query();
    void receive(std::shared_ptr<const wi::ViewResult> result);
    void updateStatus();
    void settings();
};
