#ifndef UTIL_H
#define UTIL_H

#include <QJsonDocument>
#include <QJsonObject>

#include <QElapsedTimer>

#define COM_RESET(x) { int remain = x.Reset(); if (remain != 0) qDebug() << __FUNCTION__ " reset " #x " ret" << remain; }

#define DX_DEBUG_LAYER false

class Elapsed {
    bool ended = false;
    QString name;
    QElapsedTimer timer;
public:
    Elapsed(const QString& name);
    ~Elapsed();

    void end();
};

class FpsCounter {
    int currentSec = 0;
    int lastCount = 0;
    int count = 0;
    long nsAverage = 0;

public:
    void add(long nsConsumed);
    QString stat();
};

inline bool requestOk(const QJsonObject& json) {
    return json["ok"].toBool(false);
}

inline QString requestErrorMessage(const QJsonObject& json) {
    return json["msg"].toString();
}

inline QString humanizeBytes(uint64_t bytes) {
    if (bytes > 1024 * 1024 * 1024) {
        return QString("%1 GB").arg(1.0 * bytes / (1024 * 1024 * 1024), 0, 'f', 2);
    } else if (bytes > 1024 * 1024) {
        return QString("%1 MB").arg(1.0 * bytes / (1024 * 1024), 0, 'f', 2);
    } else if (bytes > 1024) {
        return QString("%1 KB").arg(1.0 * bytes / (1024), 0, 'f', 2);
    } else {
        return QString("%1 B").arg(bytes);
    }
}

struct ID3D11Device;
void printDxDebugInfo(ID3D11Device* dev);

#endif // UTIL_H
