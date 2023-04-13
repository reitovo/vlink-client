#ifndef UTIL_H
#define UTIL_H

#include <QJsonDocument>
#include <QJsonObject>

#include <QElapsedTimer>
#include "QComboBox"
#include "QThread"
#include "QMutex"

#define COM_RESET(x) { int remain = x.Reset(); if (remain != 0) {qDebug() << __FUNCTION__ " reset " #x " ret" << remain;} }
#define qDebugStd(x) { std::ostringstream oss; oss << x; qDebug(oss.str().c_str()); }

#define DX_DEBUG_LAYER true

namespace vts::info {
    extern QString BuildId;
}

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
    int lastAddSec = 0;

public:
    void add(long nsConsumed);
    QString stat();
    double fps();
    long ns();
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

enum DeviceAdapterType {
    ADAPTER_VENDOR_INVALID,
    ADAPTER_VENDOR_NVIDIA,
    ADAPTER_VENDOR_AMD,
    ADAPTER_VENDOR_INTEL
};

template <typename T>
inline void terminateQThread(const std::unique_ptr<T>& t, const char * func) {
    if (t != nullptr && !t->isFinished() && !t->wait(500)) {
        qWarning() << "The thread is not exited in 500ms, terminate it" << func;
        t->terminate();
        t->wait(500);
    }
}

class ScopedQMutex {
    QMutex* m;
public:
    explicit ScopedQMutex(QMutex* m) : m(m) {
        m->lock();
    }
    ~ScopedQMutex() {
        m->unlock();
    }
};

class ID3D11Device;
void printDxLiveObjects(ID3D11Device* dev, const char *);

inline DeviceAdapterType getGpuVendorTypeFromVendorId(uint32_t v) { 
    switch (v) {
    case 0x1002:
    case 0x1022:
        return ADAPTER_VENDOR_AMD;
    case 0x8086:
        return ADAPTER_VENDOR_INTEL;
    case 0x10DE:
        return ADAPTER_VENDOR_NVIDIA;
    default:
        return ADAPTER_VENDOR_INVALID;
    } 
}

inline QString getGpuVendorName(DeviceAdapterType t) {
    switch (t) {
    case ADAPTER_VENDOR_AMD:
        return "AMD";
    case ADAPTER_VENDOR_INTEL:
        return "Intel";
    case ADAPTER_VENDOR_NVIDIA:
        return "NVIDIA"; 
    default:
        return "UNKNOWN";
    }
}

struct ID3D11Device;
void printDxDebugInfo(ID3D11Device* dev);

struct ID3D11DeviceContext;
struct ID3D11Resource;
void showTexture(
        ID3D11DeviceContext* pContext,
        ID3D11Resource* pSource,
        QString fileName);

void setComboBoxIfChanged(const QStringList& strList, QComboBox* box);

#endif // UTIL_H
