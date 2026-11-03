#include "core.h"
#include <chrono>
#include <random>
#include <sstream>
#include <stdexcept>
#include <filesystem>

namespace wi {
std::string categoryId(const std::string& type) { return "category:" + type; }
std::string categoryName(const std::string& type) {
    if(type=="keyboard") return "键盘";
    if(type=="mouse") return "鼠标";
    if(type=="gamepad") return "手柄";
    return type;
}
bool State::edge(const std::string &c, bool p) {
    if (p)
        return down.insert(c).second;
    down.erase(c);
    return false;
}
int State::scroll(int axis, int delta) {
    auto &r = wheel[axis * 2 + (delta < 0)];
    r += std::abs(delta);
    int n = r / 120;
    r %= 120;
    return n;
}
bool trigger(bool p, unsigned v) { return p ? v > 96 : v >= 160; }
Count nowSecond() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
std::string uniqueBatch() {
    std::random_device r;
    std::ostringstream s;
    for (int i = 0; i < 8; ++i)
        s << std::hex << r();
    return s.str();
}
void Database::exec(const char *sql) {
    char *e = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &e) != SQLITE_OK) {
        std::string m = e ? e : "SQLite error";
        sqlite3_free(e);
        throw std::runtime_error(m);
    }
}
Database::Database(const std::string &path, bool ro) {
    int rc = sqlite3_open_v2(path.c_str(), &db_,
                             ro ? SQLITE_OPEN_READONLY
                                : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                             nullptr);
    if (rc != SQLITE_OK) {
        std::string e = db_ ? sqlite3_errmsg(db_) : "open failed";
        if (db_)
            sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(e);
    }
    try {
        sqlite3_busy_timeout(db_, 3000);
        if (ro) {
            exec("PRAGMA query_only=ON");
            return;
        }
        exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA "
             "foreign_keys=ON;");
        exec(
            "CREATE TABLE IF NOT EXISTS devices(id INTEGER PRIMARY "
            "KEY,identity TEXT UNIQUE NOT NULL,type TEXT NOT NULL,name TEXT "
            "NOT NULL,online INTEGER NOT NULL DEFAULT 0);"
            "CREATE TABLE IF NOT EXISTS controls(id INTEGER PRIMARY "
            "KEY,identity TEXT UNIQUE NOT NULL,name TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS apps(id INTEGER PRIMARY KEY,identity "
            "TEXT UNIQUE NOT NULL,name TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS counts(second_utc INTEGER NOT "
            "NULL,device_id INTEGER NOT NULL REFERENCES devices(id),control_id "
            "INTEGER NOT NULL REFERENCES controls(id),app_id INTEGER NOT NULL "
            "REFERENCES apps(id),press_count INTEGER NOT NULL "
            "CHECK(press_count>0),PRIMARY "
            "KEY(second_utc,device_id,control_id,app_id)) WITHOUT ROWID;"
            "CREATE TABLE IF NOT EXISTS batches(id TEXT PRIMARY KEY);");
        sqlite3_stmt* versionQuery{};
        sqlite3_prepare_v2(db_, "PRAGMA user_version", -1, &versionQuery, nullptr);
        int version=sqlite3_step(versionQuery)==SQLITE_ROW?sqlite3_column_int(versionQuery,0):-1;
        sqlite3_finalize(versionQuery);
        if(version!=0&&version!=3)throw std::runtime_error("Database format changed: close WhatInput and remove the old database before restarting.");
        exec("PRAGMA user_version=3;");
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}
Database::~Database() {
    if (db_)
        sqlite3_close(db_);
}
struct Stmt {
    sqlite3_stmt *s{};
    Stmt(sqlite3 *d, const std::string &q) {
        if (sqlite3_prepare_v2(d, q.c_str(), -1, &s, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(d));
    }
    ~Stmt() { sqlite3_finalize(s); }
    void text(int i, const std::string &t) {
        sqlite3_bind_text(s, i, t.c_str(), -1, SQLITE_TRANSIENT);
    }
    void done() {
        if (sqlite3_step(s) != SQLITE_DONE)
            throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(s)));
    }
};
void Database::commit(const std::string &batch, const Buffer &entries) {
    exec("BEGIN IMMEDIATE");
    try {
        Stmt b(db_, "INSERT OR IGNORE INTO batches VALUES(?)");
        b.text(1, batch);
        b.done();
        if (sqlite3_changes(db_))
            for (auto &[k, e] : entries) {
                if(e.count==0) continue;
                Stmt d(db_, "INSERT INTO devices(identity,type,name,online) "
                            "VALUES(?,?,?,?) ON CONFLICT(identity) DO UPDATE "
                            "SET name=excluded.name,online=CASE WHEN ?5 THEN "
                            "excluded.online ELSE devices.online END");
                d.text(1, categoryId(k.type));
                d.text(2, k.type);
                d.text(3, categoryName(k.type));
                sqlite3_bind_int(d.s, 4, e.online);
                sqlite3_bind_int(d.s, 5, e.count == 0);
                d.done();
                if (e.count == 0)
                    continue;
                Stmt c(db_,
                       "INSERT INTO controls(identity,name) VALUES(?,?) ON "
                       "CONFLICT(identity) DO UPDATE SET name=excluded.name");
                c.text(1, k.control);
                c.text(2, e.controlName);
                c.done();
                Stmt a(db_,
                       "INSERT INTO apps(identity,name) VALUES(?,?) ON "
                       "CONFLICT(identity) DO UPDATE SET name=excluded.name");
                a.text(1, k.app);
                a.text(2, e.appName);
                a.done();
                Stmt n(
                    db_,
                    "INSERT INTO counts VALUES(?,(SELECT id FROM devices WHERE "
                    "identity=?),(SELECT id FROM controls WHERE "
                    "identity=?),(SELECT id FROM apps WHERE identity=?),?) ON "
                    "CONFLICT(second_utc,device_id,control_id,app_id) DO "
                    "UPDATE SET press_count=press_count+excluded.press_count");
                sqlite3_bind_int64(n.s, 1, k.second);
                n.text(2, categoryId(k.type));
                n.text(3, k.control);
                n.text(4, k.app);
                sqlite3_bind_int64(n.s, 5, e.count);
                n.done();
            }
        exec("COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
void Database::clear() {
    exec("BEGIN IMMEDIATE; DELETE FROM counts; DELETE FROM batches; COMMIT;");
}
} // namespace wi
