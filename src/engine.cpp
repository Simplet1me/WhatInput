#include "engine.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTimeZone>
#include <algorithm>
#include <chrono>
#include <functional>

namespace wi {
namespace {
struct Query {
    sqlite3_stmt *s = nullptr;
    Query(sqlite3 *db, const char *sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Query() { sqlite3_finalize(s); }
    void text(int index, const std::string &text) {
        sqlite3_bind_text(s, index, text.c_str(), -1, SQLITE_TRANSIENT);
    }
    std::string text(int index) const {
        const auto p = sqlite3_column_text(s, index);
        return p ? reinterpret_cast<const char *>(p) : "";
    }
    bool next() {
        int rc = sqlite3_step(s);
        if (rc == SQLITE_ROW)
            return true;
        if (rc != SQLITE_DONE)
            throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(s)));
        return false;
    }
    void filter(const ViewFilter &f) {
        sqlite3_bind_int64(s, 1, f.start);
        sqlite3_bind_int64(s, 2, f.end);
        text(3, f.type);
        text(4, f.app);
    }
};
const char *selectedSql =
    "SELECT "
    "c.second_utc,d.type,k.identity,k.name,a.identity,a.name,SUM(c.press_count)"
    " "
    "FROM counts c JOIN devices d ON d.id=c.device_id JOIN controls k ON "
    "k.id=c.control_id JOIN apps a ON a.id=c.app_id "
    "WHERE c.second_utc>=?1 AND c.second_utc<?2 AND (?3='' OR d.type=?3) AND "
    "(?4='' OR a.identity=?4) "
    "GROUP BY c.second_utc,d.type,k.identity,a.identity ORDER BY c.second_utc";
bool matches(const Key &k, const ViewFilter &f) {
    return k.second >= f.start && k.second < f.end &&
           (f.type.empty() || f.type == k.type) &&
           (f.app.empty() || f.app == k.app);
}
void eachStored(Database &db, const ViewFilter &f,
                const std::function<void(const Entry &)> &fn) {
    Query q(db.handle(), selectedSql);
    q.filter(f);
    while (q.next()) {
        Key k{sqlite3_column_int64(q.s, 0), categoryId(q.text(1)), q.text(1),
              q.text(2), q.text(4)};
        fn(Entry{k, categoryName(k.type), q.text(3), q.text(5),
                 sqlite3_column_int64(q.s, 6)});
    }
}
struct ViewCache {
    ViewFilter filter;
    std::map<std::pair<std::string, std::string>, RankedControl> controls;
    std::map<Count, Count> hours;
    std::map<std::string, Count> software;
    std::map<std::string, std::string> apps;
    Count total = 0;
    QDate today = QDate::currentDate();
    Count todayStart = today.startOfDay().toSecsSinceEpoch();
    Count todayEnd = today.addDays(1).startOfDay().toSecsSinceEpoch();
    Count todayMouse = 0, todayKeyboard = 0, todayGamepad = 0;
    bool valid = false;
    void add(const Entry &e) {
        apps[e.key.app] = e.appName;
        // Daily totals are independent of both ranking filters. Use local
        // calendar boundaries (not a fixed 24 hours) and include memory deltas.
        if (e.key.second >= todayStart && e.key.second < todayEnd) {
            if (e.key.type == "mouse") todayMouse += e.count;
            else if (e.key.type == "keyboard") todayKeyboard += e.count;
            else if (e.key.type == "gamepad") todayGamepad += e.count;
        }
        if (e.key.second >= filter.start && e.key.second < filter.end &&
            (filter.softwareType.empty() || e.key.type == filter.softwareType) &&
            (e.key.type != "mouse" || e.key.control.starts_with("mouse:button:")))
            software[e.key.app] += e.count;
        if (!matches(e.key, filter))
            return;
        auto id = std::make_pair(e.key.type, e.key.control);
        auto [it, inserted] = controls.try_emplace(
            id, RankedControl{e.key.type, e.key.control, e.controlName, 0});
        it->second.count += e.count;
        total += e.count;
        // Preserve offset through repeated DST hours and non-whole-hour zones.
        const auto local =
            QDateTime::fromSecsSinceEpoch(e.key.second).toLocalTime();
        Count hour =
            e.key.second - local.time().minute() * 60 - local.time().second();
        hours[hour] += e.count;
    }
    std::shared_ptr<const ViewResult> result(quint64 request) const {
        auto out = std::make_shared<ViewResult>();
        out->request = request;
        out->total = total;
        out->todayMouse = todayMouse;
        out->todayKeyboard = todayKeyboard;
        out->todayGamepad = todayGamepad;
        out->applications = apps;
        out->updated = QDateTime::currentDateTime();
        for (auto &[key, row] : controls)
            out->controls.push_back(row);
        std::sort(out->controls.begin(), out->controls.end(),
                  [](const auto &a, const auto &b) {
                      return a.count != b.count
                                 ? a.count > b.count
                                 : std::tie(a.type, a.control) <
                                       std::tie(b.type, b.control);
                  });
        for (auto &[hour, count] : hours)
            out->hours.push_back({std::max(hour, filter.start),
                                  std::min(hour + 3600, filter.end), count});
        for (auto &[id, count] : software) {
            auto it = apps.find(id);
            out->software.push_back(
                {id, it == apps.end() ? id : it->second, count});
        }
        std::sort(
            out->software.begin(), out->software.end(),
            [](const auto &a, const auto &b) { return a.count > b.count; });
        return out;
    }
};
QString csvCell(QString s) {
    if (!s.isEmpty() && QString("=+-@\t\r").contains(s.front()))
        s.prepend('\'');
    s.replace('"', "\"\"");
    return '"' + s + '"';
}
void merge(Buffer &destination, const Buffer &source) {
    for (const auto &[k, e] : source) {
        auto [it, added] = destination.try_emplace(k, e);
        if (!added)
            it->second.count += e.count;
    }
}
} // namespace
Engine::Engine(QString root, int interval, QObject *parent)
    : QObject(parent), root_(std::move(root)) {
    status_.interval = std::clamp(interval, 1, 3600);
    qRegisterMetaType<std::shared_ptr<const wi::ViewResult>>();
}
Engine::~Engine() {
    {
        std::lock_guard lock(mutex_);
        finalizing_ = true;
        finishRequested_ = true;
        wake_.notify_one();
    }
    if (worker_.joinable())
        worker_.join();
}
void Engine::start() { worker_ = std::thread(&Engine::run, this); }
void Engine::record(const std::string &type, const std::string &control,
                    const std::string &name, const std::string &app,
                    const std::string &appName, int count) {
    if (count <= 0)
        return;
    Key k{nowSecond(), categoryId(type), type, control, app};
    Entry entry{k, categoryName(type), name, appName, count};
    std::lock_guard lock(mutex_);
    if (finishRequested_)
        return;
    auto [it, added] = active_.try_emplace(k, entry);
    if (!added)
        it->second.count += count;
    if (watching_) {
        auto [change, newChange] = displayChanges_.try_emplace(k, entry);
        if (!newChange)
            change->second.count += count;
    }
    status_.unsaved += count;
}
void Engine::inputError(const QString &message) {
    std::lock_guard lock(mutex_);
    status_.inputError = message;
}
void Engine::watch(ViewFilter filter, quint64 request) {
    std::lock_guard lock(mutex_);
    filter_ = std::move(filter);
    request_ = request;
    watching_ = true;
    viewRequested_ = true;
    wake_.notify_one();
}
void Engine::unwatch() {
    std::lock_guard lock(mutex_);
    watching_ = false;
    displayChanges_.clear();
    wake_.notify_one();
}
void Engine::refresh() {
    std::lock_guard lock(mutex_);
    if (watching_) {
        refreshRequested_ = true;
        wake_.notify_one();
    }
}
void Engine::setInterval(int seconds) {
    std::lock_guard lock(mutex_);
    status_.interval = std::clamp(seconds, 1, 3600);
    intervalChanged_ = true;
    wake_.notify_one();
}
void Engine::flush() {
    std::lock_guard lock(mutex_);
    flushRequested_ = true;
    wake_.notify_one();
}
void Engine::clear() {
    std::lock_guard lock(mutex_);
    clearRequested_ = true;
    wake_.notify_one();
}
void Engine::exportCsv(ViewFilter filter) {
    std::lock_guard lock(mutex_);
    exportFilter_ = std::move(filter);
    exportRequested_ = true;
    wake_.notify_one();
}
void Engine::finish() {
    std::lock_guard lock(mutex_);
    finishRequested_ = true;
    wake_.notify_one();
}
void Engine::discardAndFinish() {
    std::lock_guard lock(mutex_);
    discardRequested_ = true;
    finishRequested_ = true;
    wake_.notify_one();
}
EngineStatus Engine::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}
void Engine::log(const QString &message) {
    QDir().mkpath(root_ + "/logs");
    QFile file(root_ + "/logs/whatinput.log");
    if (file.size() > 1024 * 1024) {
        QFile::remove(root_ + "/logs/whatinput.old.log");
        file.rename(root_ + "/logs/whatinput.old.log");
        file.setFileName(root_ + "/logs/whatinput.log");
    }
    if (file.open(QIODevice::Append))
        file.write((QDateTime::currentDateTime().toString(Qt::ISODate) + " " +
                    message + "\n")
                       .toUtf8());
}
void Engine::run() {
    using Clock = std::chrono::steady_clock;
    std::unique_ptr<Database> db;
    Buffer pending;
    std::string batch;
    ViewCache cache;
    quint64 cacheRequest = 0;
    auto deadline = Clock::now() + std::chrono::seconds(status().interval);
    auto open = [&] {
        if (!db)
            db = std::make_unique<Database>(
                (root_ + "/data/whatinput.sqlite3").toStdString());
        // If a failed COMMIT actually succeeded, its marker resolves ambiguity.
        if (!pending.empty()) {
            Query q(db->handle(), "SELECT 1 FROM batches WHERE id=?");
            q.text(1, batch);
            if (q.next()) {
                Count saved = 0;
                for (auto &[k, e] : pending)
                    saved += e.count;
                std::lock_guard lock(mutex_);
                status_.unsaved -= saved;
                status_.lastCommit = nowSecond();
                pending.clear();
                batch.clear();
            }
        }
        std::lock_guard lock(mutex_);
        status_.storageReady = true;
    };
    auto failure = [&](const std::exception &e) {
        auto error = QString::fromUtf8(e.what());
        {
            std::lock_guard lock(mutex_);
            status_.storageError = error;
            status_.saving = false;
        }
        log(error);
        db.reset();
    };
    try {
        open();
    } catch (const std::exception &e) {
        failure(e);
    }
    while (true) {
        bool flushNow = false, clearNow = false, exitNow = false,
             exportNow = false;
        ViewFilter exportFilter;
        {
            std::unique_lock lock(mutex_);
            wake_.wait_until(lock, deadline, [&] {
                return finishRequested_ || flushRequested_ || clearRequested_ ||
                       viewRequested_ || refreshRequested_ ||
                       intervalChanged_ || exportRequested_;
            });
            if (discardRequested_) {
                lock.unlock();
                emit finished(true, {});
                return;
            }
            if (intervalChanged_) {
                deadline =
                    Clock::now() + std::chrono::seconds(status_.interval);
                intervalChanged_ = false;
            }
            exitNow = finishRequested_;
            clearNow = clearRequested_;
            clearRequested_ = false;
            flushNow = flushRequested_ || Clock::now() >= deadline || exitNow;
            flushRequested_ = false;
            exportNow = exportRequested_;
            exportRequested_ = false;
            exportFilter = exportFilter_;
            refreshRequested_ = false;
        }
        if (clearNow) {
            try {
                open();
                // Isolate pre-clear input without blocking capture on SQLite
                // I/O.
                Buffer before;
                {
                    std::lock_guard lock(mutex_);
                    before.swap(active_);
                }
                try {
                    db->clear();
                } catch (...) {
                    std::lock_guard lock(mutex_);
                    merge(active_, before);
                    throw;
                }
                Count removed = 0;
                for (auto &[k, e] : before)
                    removed += e.count;
                for (auto &[k, e] : pending)
                    removed += e.count;
                pending.clear();
                batch.clear();
                cache.valid = false;
                {
                    std::lock_guard lock(mutex_);
                    status_.unsaved -= removed;
                    status_.lastCommit = nowSecond();
                    status_.storageError.clear();
                    viewRequested_ = watching_;
                }
                emit notice("历史记录已清空，正在继续统计新输入。");
            } catch (const std::exception &e) {
                failure(e);
                emit notice("清空失败：" + QString::fromUtf8(e.what()));
            }
        }
        if (flushNow) {
            bool success = true;
            do {
                {
                    std::lock_guard lock(mutex_);
                    if (pending.empty()) {
                        pending.swap(active_);
                        batch = uniqueBatch();
                    }
                    status_.saving = !pending.empty();
                }
                try {
                    open();
                    if (!pending.empty()) {
                        db->commit(batch, pending);
                        Count saved = 0;
                        for (auto &[k, e] : pending)
                            saved += e.count;
                        pending.clear();
                        batch.clear();
                        std::lock_guard lock(mutex_);
                        status_.unsaved -= saved;
                        status_.lastCommit = nowSecond();
                    }
                    std::lock_guard lock(mutex_);
                    status_.storageError.clear();
                    status_.saving = false;
                } catch (const std::exception &e) {
                    failure(e);
                    success = false;
                }
                if (!exitNow || !success)
                    break;
                std::lock_guard lock(mutex_);
                if (active_.empty())
                    break;
            } while (true);
            auto state = status();
            deadline = Clock::now() +
                       std::chrono::seconds(success ? state.interval : 5);
            if (exitNow) {
                if (success) {
                    emit finished(true, {});
                    return;
                }
                {
                    std::lock_guard lock(mutex_);
                    if (finalizing_)
                        return;
                    finishRequested_ = false;
                }
                emit finished(false, state.storageError);
            }
        }
        bool watching = false, rebuild = false;
        ViewFilter viewFilter;
        quint64 viewRequest = 0;
        Buffer snapshot;
        {
            std::lock_guard lock(mutex_);
            watching = watching_;
            viewRequest = request_;
            viewFilter = filter_;
            rebuild = watching_ && (viewRequested_ || !cache.valid ||
                cache.todayStart != QDate::currentDate().startOfDay().toSecsSinceEpoch());
            viewRequested_ = false;
            if (rebuild) {
                snapshot = active_;
                displayChanges_.clear();
            } else if (watching)
                snapshot.swap(displayChanges_);
            else {
                displayChanges_.clear();
                cache = ViewCache{};
            }
        }
        if (watching) {
            if (rebuild) {
                try {
                    open();
                    cache = ViewCache{};
                    cache.filter = viewFilter;
                    cacheRequest = viewRequest;
                    Query apps(db->handle(), "SELECT identity,name FROM apps");
                    while (apps.next())
                        cache.apps[apps.text(0)] = apps.text(1);
                    eachStored(*db, {viewFilter.start, viewFilter.end, "", ""},
                               [&](const Entry &e) { cache.add(e); });
                    for (auto &[k, e] : pending)
                        cache.add(e);
                    for (auto &[k, e] : snapshot)
                        cache.add(e);
                    cache.valid = true;
                    emit viewReady(cache.result(cacheRequest));
                } catch (const std::exception &e) {
                    failure(e);
                    cache.valid = false;
                    auto result = std::make_shared<ViewResult>();
                    result->request = viewRequest;
                    result->error = QString::fromUtf8(e.what());
                    emit viewReady(result);
                }
            } else if (!snapshot.empty()) {
                for (auto &[k, e] : snapshot)
                    cache.add(e);
                emit viewReady(cache.result(cacheRequest));
            }
        }
        if (exportNow) {
            try {
                open();
                Buffer records;
                {
                    std::lock_guard lock(mutex_);
                    for (auto &[k, e] : active_)
                        if (matches(k, exportFilter))
                            records.emplace(k, e);
                }
                for (auto &[k, e] : pending)
                    if (matches(k, exportFilter)) {
                        auto [it, added] = records.try_emplace(k, e);
                        if (!added)
                            it->second.count += e.count;
                    }
                eachStored(*db, exportFilter, [&](const Entry &e) {
                    auto [it, added] = records.try_emplace(e.key, e);
                    if (!added)
                        it->second.count += e.count;
                });
                QDir().mkpath(root_ + "/exports");
                QString path = root_ + "/exports/WhatInput-" +
                               QDateTime::currentDateTime().toString(
                                   "yyyyMMdd-HHmmss-zzz") +
                               ".csv";
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly))
                    throw std::runtime_error("无法创建导出文件");
                file.write("\xEF\xBB\xBF");
                file.write(QString("UTC秒,本地时间,软件,设备类型,按键,次数\r\n")
                               .toUtf8());
                for (auto &[k, e] : records) {
                    QStringList row{
                        QString::number(k.second),
                        csvCell(QDateTime::fromSecsSinceEpoch(k.second)
                                    .toLocalTime()
                                    .toString(Qt::ISODate)),
                        csvCell(QString::fromStdString(e.appName)),
                        csvCell(QString::fromStdString(categoryName(k.type))),
                        csvCell(QString::fromStdString(e.controlName)),
                        QString::number(e.count)};
                    if (file.write((row.join(',') + "\r\n").toUtf8()) < 0)
                        throw std::runtime_error("写入导出文件失败");
                }
                if (!file.commit())
                    throw std::runtime_error("保存导出文件失败");
                emit notice("已导出（包含尚未落盘的数据）：" + path);
            } catch (const std::exception &e) {
                emit notice("导出失败：" + QString::fromUtf8(e.what()));
            }
        }
    }
}
} // namespace wi
