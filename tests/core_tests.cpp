#include "core.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
static int checks = 0;
void check(bool ok, const char *name) {
    ++checks;
    if (!ok)
        throw std::runtime_error(name);
}
wi::Count scalar(wi::Database &d, const char *sql) {
    sqlite3_stmt *s{};
    if (sqlite3_prepare_v2(d.handle(), sql, -1, &s, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(d.handle()));
    int rc = sqlite3_step(s);
    auto n = rc == SQLITE_ROW ? sqlite3_column_int64(s, 0) : -1;
    sqlite3_finalize(s);
    return n;
}
wi::Buffer sample(wi::Count second, int n, std::string device = "keyboard1",
                  std::string app = "editor") {
    wi::Key k{second, device, "keyboard", "kbd:0011:0", app};
    return {{k, {k, "Keyboard", "W", app, n}}};
}
int main() {
    try {
        wi::State s;
        int count = 0;
        for (int i = 0; i < 100; ++i) {
            count += s.edge("W", true);
            s.edge("W", false);
        }
        check(count == 100, "100 taps");
        count = 0;
        for (int i = 0; i < 100; ++i)
            count += s.edge("W", true);
        check(count == 1, "repeat suppressed");
        s.edge("W", false);
        for (auto c : {"kbd:001d:0", "kbd:001d:2", "kbd:0038:0", "kbd:0038:2",
                       "kbd:001c:0", "kbd:001c:2"})
            check(s.edge(c, true), "distinct physical keys");
        wi::State other;
        check(other.edge("W", true) && s.edge("W", true), "two keyboards");
        for (int i = 0; i < 3; ++i)
            check(s.scroll(0, 30) == 0, "partial wheel");
        check(s.scroll(0, 30) == 1, "wheel accumulated");
        check(s.scroll(0, -120) == 1, "opposite wheel");
        check(other.scroll(0, 30) == 0, "wheel devices independent");
        check(!wi::trigger(false, 159) && wi::trigger(false, 160),
              "trigger press");
        check(wi::trigger(true, 97) && !wi::trigger(true, 96),
              "trigger release");
        s.baseline({"A"});
        check(!s.edge("A", true), "baseline suppress");
        s.edge("A", false);
        check(s.edge("A", true), "baseline release then press");
        auto path = std::filesystem::current_path() / "test-data";
        std::filesystem::create_directories(path);
        auto file = path / (wi::uniqueBatch() + ".sqlite3");
        {
            wi::Database db(file.string());
            auto b = sample(100, 4);
            db.commit("batch1", b);
            db.commit("batch1", b);
            check(scalar(db, "SELECT SUM(press_count) FROM counts") == 4,
                  "idempotent retry");
        }
        {
            wi::Database db(file.string());
            db.commit("batch2", sample(100, 6));
            check(scalar(db, "SELECT SUM(press_count) FROM counts") == 10,
                  "restart same second");
            db.commit("minute1", sample(119, 1));
            db.commit("minute2", sample(120, 1));
            check(scalar(db,
                         "SELECT COUNT(DISTINCT second_utc/60) FROM counts") ==
                      2,
                  "cross minute");
            db.clear();
            db.commit("before", sample(20 * 3600 + 14 * 60 + 59, 7));
            db.commit("start", sample(20 * 3600 + 15 * 60, 2));
            db.commit("last", sample(21 * 3600 + 44 * 60 + 59, 3));
            db.commit("end", sample(21 * 3600 + 45 * 60, 11));
            check(scalar(db, "SELECT SUM(press_count) FROM counts WHERE "
                             "second_utc>=72900 AND second_utc<78300") == 5,
                  "exclusive endpoint");
            db.commit("midnight1", sample(86399, 1));
            db.commit("midnight2", sample(86400, 2));
            check(scalar(db, "SELECT SUM(press_count) FROM counts WHERE "
                             "second_utc>=86400") == 2,
                  "midnight");
            db.commit("app2", sample(86400, 9, "keyboard1", "game"));
            check(scalar(db, "SELECT SUM(press_count) FROM counts JOIN apps ON "
                             "apps.id=app_id WHERE apps.identity='game'") == 9,
                  "app partition");
            wi::Database lock(file.string());
            lock.exec("BEGIN IMMEDIATE");
            bool failed = false;
            try {
                db.commit("retry", sample(90000, 8));
            } catch (...) {
                failed = true;
            }
            check(failed, "busy failure");
            lock.exec("ROLLBACK");
            db.commit("retry", sample(90000, 8));
            db.commit("retry", sample(90000, 8));
            check(scalar(db, "SELECT SUM(press_count) FROM counts WHERE "
                             "second_utc=90000") == 8,
                  "retry after busy");
            db.clear();
            check(scalar(db, "SELECT COUNT(*) FROM counts") == 0, "clear");
            db.commit("new", sample(90001, 1));
            check(scalar(db, "SELECT SUM(press_count) FROM counts") == 1,
                  "after clear");
        }
        std::filesystem::remove(file);
        auto legacy = path / (wi::uniqueBatch() + ".sqlite3");
        {
            wi::Database db(legacy.string());
            db.exec("INSERT INTO devices(id,identity,type,name) VALUES(101,'old-k1','keyboard','K1'),(102,'old-k2','keyboard','K2'),(103,'old-m','mouse','M'),(104,'old-pad','gamepad','P');"
                    "INSERT INTO controls(id,identity,name) VALUES(1,'kbd:W','W'),(2,'mouse:left','Left'),(3,'pad:A','A');"
                    "INSERT INTO apps(id,identity,name) VALUES(1,'editor','Editor'),(2,'game','Game');"
                    "INSERT INTO counts VALUES(100,101,1,1,4),(100,102,1,1,6),(100,103,2,1,3),(100,104,3,1,2),(100,102,1,2,7);"
                    "PRAGMA user_version=1;");
        }
        {
            wi::Database db(legacy.string());
            check(scalar(db,"PRAGMA user_version")==2,"migration version");
            check(scalar(db,"SELECT SUM(press_count) FROM counts")==22,"migration preserves total");
            check(scalar(db,"SELECT COUNT(*) FROM devices")==3,"only three categories");
            check(scalar(db,"SELECT press_count FROM counts c JOIN devices d ON d.id=c.device_id WHERE d.type='keyboard' AND app_id=1")==10,"legacy keyboards merged");
            check(scalar(db,"SELECT COUNT(*) FROM counts")==4,"apps and types stay separate");
            auto first=sample(200,2,"new-k1");auto second=sample(200,5,"new-k2");first.insert(second.begin(),second.end());
            db.commit("merged-batch",first);db.commit("merged-batch",first);
            check(scalar(db,"SELECT SUM(press_count) FROM counts WHERE second_utc=200")==7,"two physical sources merge exactly once");
            check(scalar(db,"SELECT COUNT(*) FROM counts WHERE second_utc=200")==1,"one row per category second");
            check(scalar(db,"SELECT COUNT(*) FROM devices")==3,"no new hardware identities stored");
        }
        {
            wi::Database db(legacy.string());
            check(scalar(db,"SELECT SUM(press_count) FROM counts")==29,"migration not repeated on reopen");
        }
        check(std::filesystem::exists(legacy.string()+".pre-category-v1.bak"),"migration backup created");
        std::filesystem::remove(legacy);
        std::filesystem::remove(legacy.string()+".pre-category-v1.bak");
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
