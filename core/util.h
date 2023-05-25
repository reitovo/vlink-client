#ifndef UTIL_H
#define UTIL_H

#include <QJsonDocument>
#include <QJsonObject>

#include <QElapsedTimer>
#include "QComboBox"
#include "QThread"
#include "QMutex"
#include "grpcpp/support/status.h"

#include <functional>
#include <iostream>

#define FORCE_COM_RESET(x) { int remain = x.Reset(); while (remain != 0) { remain = x.Reset();} }
#define COM_RESET(x) { int remain = x.Reset(); if (remain != 0) {qDebug() << __FUNCTION__ << "reset " #x " ret" << remain;} }
#define COM_RESET_TO_SINGLE(x) { x->AddRef(); while(true) { int remain = x->Release(); if (remain == 1) break; } }
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

inline void runDetached(const std::function<bool()>& run, QObject* receiver, const std::function<void()>& onFinished = nullptr) {
    bool* runFinish = new bool;
    QThread* t = QThread::create([=]() {
        *runFinish = run();
    });
    QObject::connect(t, &QThread::finished, t, &QThread::deleteLater);
    QObject::connect(t, &QThread::finished, receiver, [=]() {
        if (*runFinish && onFinished != nullptr) {
            onFinished();
        }
        delete runFinish;
    });
    t->start();
}

template <class RspType>
inline void runDetachedThenFinishOnUI(const std::function<void(RspType*, grpc::Status*)>& run,
                                      QObject* receiver,
                                      const std::function<void(RspType*, grpc::Status*)>& onFinished = nullptr) {
    auto* rsp = new RspType();
    auto* status = new grpc::Status();
    QThread* t = QThread::create([=]() {
        run(rsp, status);
    });
    QObject::connect(t, &QThread::finished, t, &QThread::deleteLater);
    QObject::connect(t, &QThread::finished, receiver, [=]() {
        if (onFinished != nullptr) {
            onFinished(rsp, status);
        }
        delete rsp;
        delete status;
    });
    t->start();
}

struct FrameQualityDesc {
    float frameRate = 60;
    int frameWidth = 1920;
    int frameHeight = 1080;
    int frameQuality = 0;

    friend inline std::ostream& operator<<(std::ostream& out, const FrameQualityDesc& quality) {
        out << "W =" << quality.frameWidth << "H ="  << quality.frameHeight << "F ="  << quality.frameRate  << "Q =" << quality.frameQuality;
        return out;
    }

    friend inline QDebug& operator<<(QDebug& out, const FrameQualityDesc& quality) {
        out  << "W =" << quality.frameWidth << "H ="  << quality.frameHeight << "F ="  << quality.frameRate  << "Q =" << quality.frameQuality;
        return out;
    }
};

inline QString getFrameFormatDesc(FrameQualityDesc q) {
    QString qua;
    switch (q.frameQuality) {
        case 0: qua = "一般"; break;
        case 1: qua = "良好"; break;
        case 2: qua = "优秀"; break;
        case 3: qua = "极致"; break;
    }
    return QString("%1×%2 %3FPS %4").arg(q.frameWidth).arg(q.frameHeight).arg(q.frameRate).arg(qua);
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

inline QString humanizeBps(uint64_t bytes) {
    bytes *= 8;
    if (bytes > 1024 * 1024 * 1024) {
        return QString("%1 Gbps").arg(1.0 * bytes / (1024 * 1024 * 1024), 0, 'f', 2);
    } else if (bytes > 1024 * 1024) {
        return QString("%1 Mbps").arg(1.0 * bytes / (1024 * 1024), 0, 'f', 2);
    } else {
        return QString("%1 Kbps").arg(1.0 * bytes / (1024), 0, 'f', 2);
    }
}

class ID3D11DeviceChild;
void setDxDebugName(ID3D11DeviceChild* child, const std::string& name);

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

class IUnknown;
void printDxLiveObjects(IUnknown* dev, const char *);

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

bool isElevated();

std::string getPrimaryGpu();

#endif // UTIL_H
