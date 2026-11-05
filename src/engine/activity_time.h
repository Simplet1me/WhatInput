#pragma once
#include <QDateTime>
#include <QTimeZone>
namespace wi {
inline QDateTime activityFloor(const QDateTime& local,int grouping){
    if(grouping==0)return local.addSecs(-local.time().minute()*60-local.time().second());
    auto date=local.date();
    if(grouping==2)date=date.addDays(1-date.dayOfWeek());
    if(grouping==3)date=QDate(date.year(),date.month(),1);
    if(grouping==4)date=QDate(date.year(),1,1);
    return date.startOfDay(local.timeZone());
}
inline QDateTime activityNext(const QDateTime& boundary,int grouping){
    if(grouping==0)return boundary.addSecs(3600);
    if(grouping==1)return boundary.addDays(1);
    if(grouping==2)return boundary.addDays(7);
    if(grouping==3)return boundary.addMonths(1);
    return boundary.addYears(1);
}
}
