#include <windows.h>
#include "application_labels.h"
#include <QDir>
#include <QFileInfo>

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
void ApplicationLabels::refresh(){
    live_.clear();
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
QString ApplicationLabels::label(const QString& value,const QString& fallback) const{
    if(value=="system"||value=="unknown")return fallback;
    const auto id=key(value);
    const auto title=live_.value(id);
    if(!title.isEmpty())return title;
    if(id.startsWith("exe-name:"))return id.mid(9);
    const auto name=QFileInfo(id).fileName();
    return name.isEmpty()?fallback:name;
}
