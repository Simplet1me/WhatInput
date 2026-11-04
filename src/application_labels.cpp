#include <windows.h>
#include "application_labels.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <utility>

namespace {
QString identity(DWORD pid){
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return {};
    wchar_t path[32768];DWORD size=32768;
    const bool ok=QueryFullProcessImageNameW(process,0,path,&size)!=FALSE;
    CloseHandle(process);
    return ok?QDir::fromNativeSeparators(QString::fromWCharArray(path,int(size))).toCaseFolded():QString{};
}
QString key(QString value){return QDir::fromNativeSeparators(value).toCaseFolded();}
}
ApplicationLabels::ApplicationLabels(QString root):filename_(std::move(root)+"/cache/app-names.json"){
    QFile file(filename_);
    if(file.open(QIODevice::ReadOnly)){
        const auto names=QJsonDocument::fromJson(file.readAll()).object();
        for(auto it=names.begin();it!=names.end();++it)
            if(it.value().isString()&&!it.value().toString().trimmed().isEmpty())cached_[key(it.key())]=it.value().toString();
    }
}
void ApplicationLabels::scan(){
    live_.clear();scannedAt_=QDateTime::currentMSecsSinceEpoch();
    EnumWindows([](HWND window,LPARAM data)->BOOL{
        auto& names=*reinterpret_cast<QHash<QString,QString>*>(data);
        if(!IsWindowVisible(window)||GetWindow(window,GW_OWNER)||
            (GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW))return TRUE;
        wchar_t buffer[1024]{};
        if(!GetWindowTextW(window,buffer,1024))return TRUE;
        const auto title=QString::fromWCharArray(buffer).trimmed();
        if(title.isEmpty())return TRUE;
        DWORD pid=0;GetWindowThreadProcessId(window,&pid);
        auto path=identity(pid);if(path.isEmpty())return TRUE;
        if(!names.contains(path))names.insert(path,title);
        const auto executable="exe-name:"+QFileInfo(path).fileName();
        if(!names.contains(executable))names.insert(executable,title);
        // UWP apps can host their visible content in a different process.
        struct Children {QHash<QString,QString>* names;QString title;};
        Children children{&names,title};
        const auto name=QFileInfo(path).fileName();
        if(name=="applicationframehost.exe"||name=="runtimebroker.exe")
            EnumChildWindows(window,[](HWND child,LPARAM context)->BOOL{
                if(!IsWindowVisible(child))return TRUE;
                auto& state=*reinterpret_cast<Children*>(context);
                DWORD owner=0;GetWindowThreadProcessId(child,&owner);
                const auto childPath=identity(owner);
                if(!childPath.isEmpty()&&!state.names->contains(childPath))state.names->insert(childPath,state.title);
                return TRUE;
            },reinterpret_cast<LPARAM>(&children));
        return TRUE;
    },reinterpret_cast<LPARAM>(&live_));
}
QString ApplicationLabels::label(const QString& value,const QString& fallback){
    if(value=="system"||value=="unknown")return fallback;
    const auto id=key(value);
    if(auto it=cached_.constFind(id);it!=cached_.cend())return it.value();
    if(!scannedAt_||QDateTime::currentMSecsSinceEpoch()-scannedAt_>=5000)scan();
    const auto title=live_.value(id);
    if(title.isEmpty())return fallback;
    cached_.insert(id,title);save();return title;
}
void ApplicationLabels::save()const{
    if(!QDir().mkpath(QFileInfo(filename_).absolutePath()))return;
    QJsonObject names;for(auto it=cached_.cbegin();it!=cached_.cend();++it)names.insert(it.key(),it.value());
    QSaveFile file(filename_);const auto bytes=QJsonDocument(names).toJson();
    if(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size())file.commit();
}
