#ifndef PEER_H
#define PEER_H

#include "NatTypeProbe/p2p_api.h"
#include <QString>
#include "av_to_d3d.h"
#include "core/smartbuf.h"
#include "qthread.h"
#include "rtc/rtc.hpp"
#include "vts.pb.h"
#include <QDateTime>
#include <QJsonObject>
#include "concurrentqueue/concurrentqueue.h"

class PeerUi {
public:
    QString peerId;
    QString nick;
    NatType nat;
    long rtt;
    bool isServer;
};

class CollabRoom;

// This class is holding either server or client peer connection using libdatachannel
class Peer : public PeerUi {
    CollabRoom* room;
    std::unique_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> dc;
    QDateTime sdpTime;

    std::unique_ptr<AvToDx> dec;

    QMutex dcLock;

public:
    Peer(CollabRoom* room, QString remoteId, QDateTime timeVersion);
    ~Peer();

    bool connected();
    bool usingTurn();

    void startServer();
    void startClient(QJsonObject serverSdp);
    void close();

    std::atomic_bool dcThreadAlive;
    std::unique_ptr<QThread> dcThread;
    moodycamel::ConcurrentQueue<std::shared_ptr<VtsMsg>> sendQueue;
    moodycamel::ConcurrentQueue<std::unique_ptr<VtsMsg>> recvQueue;
    std::unique_ptr<smartbuf> smartBuf;
    void sendAsync(std::shared_ptr<VtsMsg> payload);

    QString dataStats();
    QDateTime timeVersion();
    void setRemoteSdp(QJsonObject sdp);
    void initSmartBuf();

    void decode(const VtsAvFrame& frame);

    void sendHeartbeat();
    long rtt();
};

#endif // PEER_H
