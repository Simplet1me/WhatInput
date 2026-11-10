#pragma once
#include <QString>

namespace wi {
enum class AutostartState { Disabled, Enabled, NeedsRepair, Unknown };
struct AutostartStatus {
    AutostartState state = AutostartState::Unknown;
    QString detail;
};
QString currentUserSid();
bool administratorSession(QString* error);
bool initializeTaskSecurity(QString* error);
AutostartStatus autoStartStatus();
bool setAutoStartEnabled(bool enabled, QString* error);
}
