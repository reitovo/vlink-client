#ifndef DEBUGCENTER_H
#define DEBUGCENTER_H

#include "qmutex.h"
#include "QList"

class IDebugCollectable;
class DebugCenter
{
public:
    static void registerCollectable(IDebugCollectable* c);
    static void unregisterCollectable(IDebugCollectable* c);

    static QString debugInfo();
};

class IDebugCollectable {
public:
    IDebugCollectable() {
        DebugCenter::registerCollectable(this);
    }
    ~IDebugCollectable() {
        DebugCenter::unregisterCollectable(this);
    }
    virtual QString debugInfo() = 0;
};

#endif // DEBUGCENTER_H
