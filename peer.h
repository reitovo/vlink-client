#ifndef PEER_H
#define PEER_H

#include "NatTypeProbe/p2p_api.h"
#include <QString>
#include "rtc/rtc.hpp"
#include <QDateTime>
#include <QJsonObject>

class PeerUi {
public:
    QString id;
    QString nick;
    NatType nat;
    long rtt;
};

class CollabRoom;
class Peer : public PeerUi {
public:
    CollabRoom* room;
    std::unique_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> dc;
    QDateTime sdpTime;

    Peer(CollabRoom* room, QString id) {
        this->room = room;
        this->id = id;
    }
    ~Peer() {
        close();
        qDebug() << "Peer destroyed";
    }

    void startServer();
    void startClient(QJsonObject serverSdp);
    void close();
};

#endif // PEER_H
