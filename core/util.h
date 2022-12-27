#ifndef UTIL_H
#define UTIL_H

#include <QJsonDocument>
#include <QJsonObject>

#include <QElapsedTimer>

#define COM_RESET(x) { int remain = x.Reset(); if (remain != 0) qDebug() << __FUNCTION__ " reset " #x " ret" << remain; }

#define DX_DEBUG_LAYER true

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

#endif // UTIL_H
