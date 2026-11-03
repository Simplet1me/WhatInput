#include "core.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMainWindow>
#include <QMessageBox>
#include <QSaveFile>
#include <QTimer>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#include <QtConcurrent>
#include <windows.h>

static QString root;
static QString txt(sqlite3_stmt *s, int i) {
    auto p = sqlite3_column_text(s, i);
    return p ? QString::fromUtf8(reinterpret_cast<const char *>(p)) : QString();
}
class Statement {
  public:
    sqlite3_stmt *s{};
    Statement(sqlite3 *db, const QString &sql) {
        if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &s, nullptr) !=
            SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(s); }
    bool next() {
        int r = sqlite3_step(s);
        if (r == SQLITE_ROW)
            return true;
        if (r != SQLITE_DONE)
            throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(s)));
        return false;
    }
    void bind(const QJsonObject &f) {
        sqlite3_bind_int64(s, 1, f["start"].toVariant().toLongLong());
        sqlite3_bind_int64(s, 2, f["end"].toVariant().toLongLong());
        for (int i = 0; i < 2; ++i) {
            auto v =
                f[QStringList{"type", "app"}[i]].toString().toUtf8();
            sqlite3_bind_text(s, i + 3, v.constData(), v.size(),
                              SQLITE_TRANSIENT);
        }
    }
};
static const QString joins =
    " FROM counts c JOIN devices d ON d.id=c.device_id JOIN controls k ON "
    "k.id=c.control_id JOIN apps a ON a.id=c.app_id ";
static const QString where =
    " WHERE second_utc>=?1 AND second_utc<?2 AND (?3='' OR d.type=?3) AND "
    "(?4='' OR a.identity=?4) ";
static QJsonObject query(QJsonObject f) {
    QJsonObject out{{"request", f["request"]}};
    try {
        if (f["start"].toVariant().toLongLong() >=
            f["end"].toVariant().toLongLong())
            throw std::runtime_error("起始时间必须早于结束时间");
        wi::Database db((root + "/data/whatinput.sqlite3").toStdString(), true);
        db.exec("BEGIN");
        QJsonArray apps, rows, bars, software;
        Statement as(db.handle(),
                     "SELECT identity,name FROM apps ORDER BY name");
        while (as.next())
            apps.append(
                QJsonObject{{"id", txt(as.s, 0)}, {"name", txt(as.s, 1)}});
        Statement rank(db.handle(),
                       "SELECT d.type,k.identity,k.name,SUM(c.press_count)" +
                           joins + where +
                           " GROUP BY d.type,k.identity ORDER BY "
                           "SUM(c.press_count) DESC,d.type,k.identity");
        rank.bind(f);
        qint64 total = 0;
        while (rank.next()) {
            auto n = sqlite3_column_int64(rank.s, 3);
            total += n;
            rows.append(QJsonObject{{"type", txt(rank.s, 0)},
                                    {"control", txt(rank.s, 1)},
                                    {"name", txt(rank.s, 2)},
                                    {"count", QString::number(n)}});
        }
        // Group by UTC hours, then label in local time; keep exact clipped
        // interval for drill-down.
        Statement hist(db.handle(),
                       "SELECT (second_utc/3600)*3600,SUM(press_count)" +
                           joins + where + " GROUP BY 1 ORDER BY 1");
        hist.bind(f);
        while (hist.next()) {
            qint64 t = sqlite3_column_int64(hist.s, 0);
            bars.append(QJsonObject{
                {"start", double(qMax(t, f["start"].toVariant().toLongLong()))},
                {"end",
                 double(qMin(t + 3600, f["end"].toVariant().toLongLong()))},
                {"label",
                 QDateTime::fromSecsSinceEpoch(t).toLocalTime().toString(
                     "MM-dd HH:mm t")},
                {"count", QString::number(sqlite3_column_int64(hist.s, 1))}});
        }
        Statement ar(db.handle(), "SELECT a.name,SUM(press_count)" + joins +
                                      where + " GROUP BY a.id ORDER BY 2 DESC");
        ar.bind(f);
        while (ar.next())
            software.append(QJsonObject{
                {"name", txt(ar.s, 0)},
                {"count", QString::number(sqlite3_column_int64(ar.s, 1))}});
        db.exec("COMMIT");
        out["apps"] = apps;
        out["rows"] = rows;
        out["bars"] = bars;
        out["software"] = software;
        out["total"] = QString::number(total);
        out["updated"] =
            QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    } catch (const std::exception &e) {
        out["error"] = QString::fromUtf8(e.what());
    }
    auto h = FindWindowW(L"WhatInputCollector", nullptr);
    DWORD_PTR status = 0, committed = 0;
    if (h) {
        SendMessageTimeoutW(h, WM_APP + 4, 0, 0, SMTO_ABORTIFHUNG, 200,
                            &status);
        SendMessageTimeoutW(h, WM_APP + 5, 0, 0, SMTO_ABORTIFHUNG, 200,
                            &committed);
    }
    out["committed"] = committed
                           ? QDateTime::fromSecsSinceEpoch(qint64(committed))
                                 .toLocalTime()
                                 .toString("HH:mm:ss")
                           : "—";
    out["status"] = !h            ? "采集器未运行"
                    : status == 1 ? "正在统计 · 最多约 5 秒写入延迟"
                                  : "采集故障，请查看 logs/collector.log";
    return out;
}
class Bridge : public QObject {
    Q_OBJECT
  public:
    explicit Bridge(QObject *p = nullptr) : QObject(p) {}
  public slots:
    void queryStats(const QString &json) {
        auto f = QJsonDocument::fromJson(json.toUtf8()).object();
        auto watcher = new QFutureWatcher<QJsonObject>(this);
        connect(watcher, &QFutureWatcher<QJsonObject>::finished, this,
                [this, watcher] {
                    emit result(
                        QString::fromUtf8(QJsonDocument(watcher->result())
                                              .toJson(QJsonDocument::Compact)));
                    watcher->deleteLater();
                });
        watcher->setFuture(QtConcurrent::run(query, f));
    }
    void exportCsv(const QString &json) {
        auto f = QJsonDocument::fromJson(json.toUtf8()).object();
        auto watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this,
                [this, watcher] {
                    emit notice(watcher->result());
                    watcher->deleteLater();
                });
        watcher->setFuture(QtConcurrent::run([f] {
            try {
                wi::Database db(
                    (root + "/data/whatinput.sqlite3").toStdString(), true);
                Statement s(
                    db.handle(),
                    "SELECT "
                    "second_utc,a.name,d.type,k.name,SUM(press_count)" +
                        joins + where + " GROUP BY second_utc,a.id,d.type,k.id ORDER BY second_utc,a.id,d.type,k.id");
                s.bind(f);
                QDir().mkpath(root + "/exports");
                QString path = root + "/exports/WhatInput-" +
                               QDateTime::currentDateTime().toString(
                                   "yyyyMMdd-HHmmss-zzz") +
                               ".csv";
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly))
                    throw std::runtime_error("无法创建导出文件");
                file.write("\xEF\xBB\xBF");
                file.write(
                    QStringLiteral(
                        "UTC秒,本地时间,软件,设备类型,按键,次数\r\n")
                        .toUtf8());
                auto cell = [](QString v) {
                    if (!v.isEmpty() && QString("=+-@\t\r").contains(v.front()))
                        v.prepend('\'');
                    v.replace('"', "\"\"");
                    return '"' + v + '"';
                };
                while (s.next()) {
                    auto t = sqlite3_column_int64(s.s, 0);
                    QStringList row{QString::number(t),
                                    cell(QDateTime::fromSecsSinceEpoch(t)
                                             .toLocalTime()
                                             .toString(Qt::ISODate))};
                    for (int i = 1; i <= 3; ++i)
                        row << cell(txt(s.s, i));
                    row << QString::number(sqlite3_column_int64(s.s, 4));
                    if (file.write((row.join(',') + "\r\n").toUtf8()) < 0)
                        throw std::runtime_error("导出写入失败");
                }
                if (!file.commit())
                    throw std::runtime_error("导出保存失败");
                return QString("已导出：") + path;
            } catch (const std::exception &e) {
                return QString("导出失败：") + QString::fromUtf8(e.what());
            }
        }));
    }
    void clearHistory() {
        if (QMessageBox::warning(nullptr, "清空全部历史",
                                 "此操作将永久删除所有软件和设备的统计次数，无"
                                 "法撤销。是否继续？",
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) != QMessageBox::Yes)
            return;
        auto h = FindWindowW(L"WhatInputCollector", nullptr);
        if (!h) {
            emit notice("请先启动 WhatInput.exe，再清空历史。");
            return;
        }
        DWORD_PTR value{};
        if (SendMessageTimeoutW(h, WM_APP + 3, 0, 0, SMTO_ABORTIFHUNG, 1000,
                                &value))
            emit notice(
                "已请求清空，下一次刷新显示结果；若失败请查看采集状态。");
        else
            emit notice("清空请求失败");
    }
  signals:
    void result(const QString &json);
    void notice(const QString &message);
};
class LocalOnly : public QWebEngineUrlRequestInterceptor {
  public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo &i) override {
        auto s = i.requestUrl().scheme();
        if (s != "qrc" && s != "data" && s != "about")
            i.block(true);
    }
};
int main(int argc, char **argv) {
    wchar_t exe[32768];
    GetModuleFileNameW(nullptr, exe, 32768);
    root = QFileInfo(QString::fromWCharArray(exe)).absolutePath();
    QDir().mkpath(root + "/temp");
    QDir().mkpath(root + "/cache/webengine");
    QDir().mkpath(root + "/data/webengine");
    qputenv("TEMP", (root + "/temp").toLocal8Bit());
    qputenv("TMP", (root + "/temp").toLocal8Bit());
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
            ("--disable-breakpad --disable-crash-reporter --disk-cache-dir=\"" +
             root + "/cache/webengine\"")
                .toUtf8());
    QApplication app(argc, argv);
    app.setApplicationName("WhatInputStats");
    QLocalSocket existing;
    existing.connectToServer("WhatInput.Stats.v1");
    if (existing.waitForConnected(300)) {
        existing.write(app.arguments().contains("--close") ? "close"
                                                           : "activate");
        existing.waitForBytesWritten();
        return 0;
    }
    if (app.arguments().contains("--close"))
        return 0;
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen("WhatInput.Stats.v1"))
        return 1;
    QMainWindow win;
    win.setWindowTitle("WhatInput · 本地输入统计");
    win.resize(1220, 820);
    QWebEngineProfile profile("WhatInput", &app);
    profile.setCachePath(root + "/cache/webengine");
    profile.setPersistentStoragePath(root + "/data/webengine");
    profile.setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    profile.setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    LocalOnly interceptor;
    profile.setUrlRequestInterceptor(&interceptor);
    auto view = new QWebEngineView(&win);
    auto page = new QWebEnginePage(&profile, view);
    view->setPage(page);
    page->settings()->setAttribute(
        QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    page->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows,
                                   false);
    auto channel = new QWebChannel(page);
    auto bridge = new Bridge(channel);
    channel->registerObject("backend", bridge);
    page->setWebChannel(channel);
    win.setCentralWidget(view);
    view->load(QUrl("qrc:/web/index.html"));
    QObject::connect(&server, &QLocalServer::newConnection, &win, [&] {
        auto socket = server.nextPendingConnection();
        QObject::connect(socket, &QLocalSocket::readyRead, &win, [&, socket] {
            auto command = socket->readAll();
            if (command == "close")
                win.close();
            else {
                win.showNormal();
                win.raise();
                win.activateWindow();
            }
            socket->disconnectFromServer();
            socket->deleteLater();
        });
    });
    if (app.arguments().contains("--self-test")) {
        QTimer::singleShot(7000, &win, [&, page] {
            page->runJavaScript(
                "JSON.stringify({title:document.title,bridge:typeof "
                "backend!=='undefined'&&!!backend,rows:document."
                "querySelectorAll('#rows "
                "tr').length,status:document.getElementById('status')."
                "textContent,error:!document.getElementById('message').hidden,"
                "css:getComputedStyle(document.body).backgroundColor,resources:"
                "performance.getEntriesByType('resource').map(x=>x.name)})",
                [&](const QVariant &result) {
                    QFile report(root + "/logs/ui-smoke.json");
                    QDir().mkpath(root + "/logs");
                    report.open(QIODevice::WriteOnly);
                    report.write(result.toString().toUtf8());
                    auto data =
                        QJsonDocument::fromJson(result.toString().toUtf8())
                            .object();
                    view->grab().save(root + "/logs/ui-smoke.png");
                    app.exit(data["bridge"].toBool() &&
                                     !data["error"].toBool() &&
                                     data["rows"].toInt() > 0
                                 ? 0
                                 : 2);
                });
        });
    }
    win.show();
    int rc = app.exec();
    win.setCentralWidget(nullptr);
    delete view;
    QThreadPool::globalInstance()->waitForDone();
    return rc;
}
#include "stats.moc"
