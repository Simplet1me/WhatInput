#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <sqlite3.h>
#include <string>
#include <tuple>
#include <vector>

namespace wi {
std::string categoryId(const std::string &type);
std::string categoryName(const std::string &type);
using Count = std::int64_t;
struct MousePosition {
    std::string screenIdentity, screenName;
    int left = 0, top = 0, width = 0, height = 0, x = 0, y = 0;
    auto operator<=>(const MousePosition &) const = default;
};
struct Key {
    Count second{};
    std::string device, type, control, app;
    std::optional<MousePosition> mouse;
    auto operator<=>(const Key &) const = default;
};
struct Entry {
    Key key;
    std::string deviceName, controlName, appName;
    Count count{};
    bool online = true;
};
using Buffer = std::map<Key, Entry>;
struct State {
    std::set<std::string> down;
    int wheel[4]{};
    bool edge(const std::string &control, bool pressed);
    int scroll(int axis, int delta);
    void baseline(const std::set<std::string> &pressed) { down = pressed; }
};
bool trigger(bool previous, unsigned value);
struct Filter {
    Count start = 0, end = INT64_MAX;
    std::string type, device, app;
};
struct Row {
    std::string type, control, name, device;
    Count count;
};
class Database {
    sqlite3 *db_{};

  public:
    explicit Database(const std::string &path, bool readonly = false);
    ~Database();
    Database(const Database &) = delete;
    void exec(const char *sql);
    void commit(const std::string &batch, const Buffer &entries);
    void clear();
    sqlite3 *handle() { return db_; }
};
std::string uniqueBatch();
Count nowSecond();
} // namespace wi
