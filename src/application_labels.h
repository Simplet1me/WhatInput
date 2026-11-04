#pragma once
#include <QHash>
#include <QString>

// Display labels only; executable identities and persisted counts are unchanged.
class ApplicationLabels {
public:
    explicit ApplicationLabels(QString root);
    QString label(const QString& identity,const QString& fallback);
private:
    QString filename_;
    QHash<QString,QString> cached_, live_;
    qint64 scannedAt_=0;
    void scan();
    void save()const;
};
