#include "debug_center.h"

static QMutex listLock;
static QList<IDebugCollectable*> collectable;

void DebugCenter::registerCollectable(IDebugCollectable *c) {
    listLock.lock();
    collectable.append(c);
    listLock.unlock();
}

void DebugCenter::unregisterCollectable(IDebugCollectable *c) {
    listLock.lock();
    collectable.removeAll(c);
    listLock.unlock();
}

QString DebugCenter::debugInfo()
{
    QStringList items;
    listLock.lock();

    for(auto& a : collectable) {
        items.append(a->debugInfo());
    }

    listLock.unlock();

    return items.join("\n");
}
