// clang-format off
#include <windows.h>
#include <objbase.h>
// clang-format on
#include "capture/capture.h"
#include "ui/window.h"
#include "platform/autostart.h"
#include <QApplication>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSessionManager>
#include <QSystemTrayIcon>
#include <QTimer>
#include <functional>

namespace {
struct MainComApartment {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~MainComApartment() { if (SUCCEEDED(hr)) CoUninitialize(); }
};
QIcon applicationIcon() { return QIcon(":/assets/WhatInput.ico"); }
} // namespace
int main(int argc, char **argv) {
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    QString privilegeError;
    if (!wi::administratorSession(&privilegeError)) {
        MessageBoxW(nullptr, privilegeError.toStdWString().c_str(), L"WhatInput", MB_OK | MB_ICONERROR);
        return 1;
    }
    MainComApartment apartment;
    if ((FAILED(apartment.hr) && apartment.hr != RPC_E_CHANGED_MODE) || !wi::initializeTaskSecurity(&privilegeError)) {
        if (privilegeError.isEmpty()) privilegeError = "无法初始化 Windows COM 线程。";
        MessageBoxW(nullptr, privilegeError.toStdWString().c_str(), L"WhatInput", MB_OK | MB_ICONERROR); return 1;
    }
    const auto root =
        QFileInfo(QString::fromWCharArray(executable)).absolutePath();
    QDir().mkpath(root + "/temp");
    qputenv("TEMP", QDir::toNativeSeparators(root + "/temp").toLocal8Bit());
    qputenv("TMP", QDir::toNativeSeparators(root + "/temp").toLocal8Bit());
    QApplication app(argc, argv);
    app.setApplicationName("WhatInput");
    app.setApplicationVersion(WHATINPUT_VERSION);
    app.setQuitOnLastWindowClosed(false);
    app.setWindowIcon(applicationIcon());
    if (app.arguments().contains("--enable-autostart") || app.arguments().contains("--disable-autostart")) {
        QString error;
        if (wi::setAutoStartEnabled(app.arguments().contains("--enable-autostart"), &error)) return 0;
        QDir().mkpath(root + "/logs"); QFile file(root + "/logs/autostart-setup.log");
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) file.write(error.toUtf8());
        return 1;
    }
    const bool hiddenStartup = app.arguments().contains("--startup-hidden") || app.arguments().contains("--background");
    const bool loginStartup = app.arguments().contains("--autostart");
    QDir().mkpath(root + "/logs");
    QFile startupLog(root + "/logs/startup.log");
    if (startupLog.size() > 1024 * 1024) {
        QFile::remove(root + "/logs/startup.previous.log");
        startupLog.rename(root + "/logs/startup.previous.log");
        startupLog.setFileName(root + "/logs/startup.log");
    }
    if (startupLog.open(QIODevice::Append | QIODevice::Text)) {
        startupLog.write((QDateTime::currentDateTime().toString(Qt::ISODate) +
            (loginStartup ? " login startup" : " manual startup") + " elevated=true user=" + wi::currentUserSid() + "\n").toUtf8());
        startupLog.close();
    }
    if (app.arguments().contains("--exit")) {
        if (auto h = FindWindowW(L"WhatInputCollector", nullptr))
            PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
    }
    const auto mutexName = ("Local\\WhatInput.Collector.v2." + wi::currentUserSid()).toStdWString();
    HANDLE singleton = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!singleton) {
        QMessageBox::critical(nullptr, "WhatInput", "无法建立单实例锁。");
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        for (int i = 0; i < 50; ++i) {
            if (auto h = FindWindowW(L"WhatInputCollector", nullptr)) {
                DWORD pid;
                GetWindowThreadProcessId(h, &pid);
                AllowSetForegroundWindow(pid);
                if (!hiddenStartup)
                    PostMessageW(h, WM_APP + 2, 0, 0);
                break;
            }
            Sleep(20);
        }
        CloseHandle(singleton);
        return 0;
    }
    if (!QDir().mkpath(root + "/data") || !QDir().mkpath(root + "/config") ||
        !QDir().mkpath(root + "/logs")) {
        QMessageBox::critical(nullptr, "WhatInput",
                              "程序目录不可写，请移动到可写目录后运行。");
        CloseHandle(singleton);
        return 1;
    }
    QSaveFile writable(root + "/config/.writable");
    if (!writable.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(nullptr, "WhatInput",
                              "程序目录不可写，请移动到可写目录后运行。");
        CloseHandle(singleton);
        return 1;
    }
    writable.cancelWriting();
    int interval = 60;
    QString settingError;
    QFile settings(root + "/config/settings.json");
    if (settings.exists()) {
        if (settings.open(QIODevice::ReadOnly)) {
            QJsonParseError error;
            const auto json =
                QJsonDocument::fromJson(settings.readAll(), &error);
            // Release the read handle before QSaveFile later replaces this file on Windows.
            settings.close();
            const auto value = json.object().value("saveIntervalSeconds");
            if (error.error == QJsonParseError::NoError && value.isDouble() &&
                value.toInt() >= 1 && value.toInt() <= 3600)
                interval = value.toInt();
            else
                settingError =
                    "保存间隔设置无效，暂用默认 60 秒，可在设置中重新保存。";
        } else
            settingError = "无法读取设置，暂用默认 60 秒。";
    }
    wi::Engine engine(root, interval);
    StatsWindow window(engine, root);
    QSystemTrayIcon tray(app.windowIcon());
    tray.setToolTip("WhatInput · 正在统计");
    QMenu menu;
    auto showAction = menu.addAction("打开统计");
    auto saveAction = menu.addAction("立即保存");
    menu.addSeparator();
    auto quitAction = menu.addAction("退出");
    tray.setContextMenu(&menu);
    bool quitting = false;
    std::function<void()> quit = [&] {
        if (quitting)
            return;
        quitting = true;
        window.prepareExit();
        tray.setToolTip("WhatInput · 正在保存并退出");
        showAction->setEnabled(false);
        saveAction->setEnabled(false);
        quitAction->setEnabled(false);
        wi::stopCapture();
        engine.finish();
    };
    QObject::connect(showAction, &QAction::triggered, &window,
                     &StatsWindow::open);
    QObject::connect(saveAction, &QAction::triggered, &engine,
                     &wi::Engine::flush);
    QObject::connect(quitAction, &QAction::triggered, &app, quit);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &window,
                     [&](QSystemTrayIcon::ActivationReason reason) {
                         if (reason == QSystemTrayIcon::DoubleClick)
                             window.open();
                     });
    QObject::connect(
        &engine, &wi::Engine::finished, &app,
        [&](bool saved, const QString &error) {
            if (saved) {
                tray.hide();
                app.quit();
                return;
            }
            QMessageBox dialog(QMessageBox::Warning, "退出前保存失败",
                               "采集已停止，尚未保存的数据仍在内存。\n" + error,
                               QMessageBox::NoButton, &window);
            auto retry = dialog.addButton("重试保存", QMessageBox::AcceptRole);
            auto discard = dialog.addButton("丢弃未保存数据并退出",
                                            QMessageBox::DestructiveRole);
            dialog.setDefaultButton(retry);
            dialog.exec();
            if (dialog.clickedButton() == discard)
                engine.discardAndFinish();
            else
                engine.finish();
        },
        Qt::QueuedConnection);
    engine.start();
    const bool capturing = wi::startCapture(
        engine,
        [&] {
            QMetaObject::invokeMethod(
                &window, [&] { window.open(); }, Qt::QueuedConnection);
        },
        [&] { QMetaObject::invokeMethod(&app, quit, Qt::QueuedConnection); });
    if (!capturing)
        QMessageBox::warning(&window, "输入采集未启动",
                             engine.status().inputError);
    QObject::connect(
        &app, &QGuiApplication::commitDataRequest, &app,
        [&](QSessionManager &) { engine.flush(); }, Qt::DirectConnection);
    tray.show();
    if (!hiddenStartup ||
        !QSystemTrayIcon::isSystemTrayAvailable())
        window.open();
    if (!settingError.isEmpty())
        QTimer::singleShot(0, &window, [&] {
            QMessageBox::information(&window, "设置", settingError);
        });
    int result = app.exec();
    wi::stopCapture();
    CloseHandle(singleton);
    return result;
}
