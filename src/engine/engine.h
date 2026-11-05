#pragma once
#include "core/core.h"
#include <QDateTime>
#include <QObject>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace wi {
struct ViewFilter {
    Count start = 0, end = INT64_MAX;
    std::string type, app;
    std::string softwareType;
    std::string activityType, activityApp;
    int activityZoom = 0;
    bool mouseDetails=false;
};
struct RankedControl {
    std::string type, control, name;
    Count count = 0;
};
struct HourCount {
    Count start = 0, end = 0, count = 0;
};
struct ActivePeriod {
    Count start = 0, end = 0, count = 0;
};
struct RankedApplication {
    std::string id, name;
    Count count = 0;
};
struct ViewResult {
    quint64 request = 0;
    Count total = 0;
    Count todayMouse = 0, todayKeyboard = 0, todayGamepad = 0;
    Count rangeStart = 0, rangeEnd = 0, activityBucketSeconds = 3600;
    std::vector<RankedControl> controls;
    std::vector<HourCount> hours;
    Count activityTotal = 0, activeMinutes = 0;
    std::vector<ActivePeriod> activePeriods;
    std::vector<RankedApplication> software;
    std::map<std::string, std::string> applications;
    QString error;
    QDateTime updated;
    struct MousePoint {int x=0,y=0;Count count=0;int button=0;bool operator==(const MousePoint&)const=default;};
    struct MouseScreen {
        MousePosition screen;
        std::vector<MousePoint> points;
        Count total=0;
    };
    std::vector<MouseScreen> mouseScreens;
    std::vector<RankedApplication> movement;
    std::vector<RankedControl> combinations;
};
struct EngineStatus {
    Count unsaved = 0, lastCommit = 0;
    int interval = 60;
    bool saving = false, storageReady = false;
    QString storageError, inputError;
};

// One storage worker owns SQLite and the current view accumulator. Collectors
// only touch short-lived in-memory buffers under mutex_.
class Engine : public QObject {
    Q_OBJECT
  public:
    Engine(QString root, int interval, QObject *parent = nullptr);
    ~Engine();
    void start();
    void record(const std::string &type, const std::string &control,
                const std::string &name, const std::string &app,
                const std::string &appName, Count count,
                std::optional<MousePosition> position=std::nullopt);
    void inputError(const QString &message);
    void watch(ViewFilter filter, quint64 request);
    void unwatch();
    void refresh();
    void setActivityZoom(int zoom);
    void setInterval(int seconds);
    void flush();
    void clear();
    void exportCsv(ViewFilter filter);
    void finish();
    void discardAndFinish();
    EngineStatus status() const;
    int mouseDpi()const{return mouseDpi_.load();}
    void setMouseDpi(int value){mouseDpi_=value;}
  signals:
    void viewReady(std::shared_ptr<const wi::ViewResult> result);
    void notice(QString message);
    void finished(bool saved, QString error);

  private:
    QString root_;
    std::atomic<int> mouseDpi_=2000;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Buffer active_, displayChanges_;
    EngineStatus status_;
    bool watching_ = false, viewRequested_ = false, refreshRequested_ = false;
    bool flushRequested_ = false, clearRequested_ = false,
         intervalChanged_ = false;
    bool finishRequested_ = false, exportRequested_ = false;
    bool discardRequested_ = false;
    bool finalizing_ = false;
    ViewFilter filter_, exportFilter_;
    quint64 request_ = 0;
    std::thread worker_;
    void run();
    void log(const QString &message);
    void rememberSave();
};
} // namespace wi
Q_DECLARE_METATYPE(std::shared_ptr<const wi::ViewResult>)
