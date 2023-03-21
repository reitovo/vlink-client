#ifndef PEER_H
#define PEER_H

#include "NatTypeProbe/p2p_api.h"
#include <QString>
#include "av_to_d3d.h"
#include "core/smart_buf.h"
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

    QMutex pcLock;

    void printSelectedCandidate();

public:
    Peer(CollabRoom* room, QString remoteId, QDateTime timeVersion);
    ~Peer();

    bool connected();
    bool usingTurn();
    bool failed();

    void startServer();
    void startClient(QJsonObject serverSdp);
    void close();
     
    std::atomic_bool dcInited = false;
    std::atomic_bool dcThreadAlive;
    std::unique_ptr<QThread> dcThread;
    moodycamel::ConcurrentQueue<std::shared_ptr<VtsMsg>> sendQueue;
    moodycamel::ConcurrentQueue<std::unique_ptr<VtsMsg>> recvQueue;
    std::unique_ptr<smart_buf> smartBuf;
    void sendAsync(std::shared_ptr<VtsMsg> payload);

    rtc::Description processLocalDescription(rtc::Description desc);

    QString dataStats();
    QDateTime timeVersion();
    void setClientRemoteSdp(QJsonObject sdp);
    void initSmartBuf();

    void decode(std::unique_ptr<VtsMsg> m);
    void resetDecoder();

    void sendHeartbeat();
    long rtt();

    void bytesProcessed(size_t& sent, size_t& received);
};

#endif // PEER_H
