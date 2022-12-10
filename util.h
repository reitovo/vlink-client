#ifndef UTIL_H
#define UTIL_H

#include <QJsonDocument>
#include <QJsonObject>

bool requestOk(const QJsonObject& json) {
    return json["ok"].toBool(false);
}

QString requestErrorMessage(const QJsonObject& json) {
    return json["msg"].toString();
}

#endif // UTIL_H
