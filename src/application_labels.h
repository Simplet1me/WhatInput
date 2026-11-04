#pragma once
#include <QHash>
#include <QString>

// Display labels only; executable identities and persisted counts are unchanged.
class ApplicationLabels {
public:
    void refresh();
    QString label(const QString& identity,const QString& fallback) const;
private:
    // One live UI refresh snapshot, discarded on the next refresh. Never saved.
    QHash<QString,QString> live_;
};
