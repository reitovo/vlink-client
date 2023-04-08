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
#include "vts_server.pb.h"

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
    void startClient(const vts::server::NotifyRtcSdp& serverSdp);
    void close();
     
    std::atomic_bool dcInited = false;
    std::atomic_bool dcThreadAlive;
    std::unique_ptr<QThread> dcThread;
    moodycamel::ConcurrentQueue<std::shared_ptr<vts::VtsMsg>> sendQueue;
    moodycamel::ConcurrentQueue<std::unique_ptr<vts::VtsMsg>> recvQueue;
    std::unique_ptr<smart_buf> smartBuf;
    void sendAsync(std::shared_ptr<vts::VtsMsg> payload);

    rtc::Description processLocalDescription(rtc::Description desc);

    QString dataStats();
    QDateTime timeVersion();
    void setClientRemoteSdp(QJsonObject sdp);
    void initSmartBuf();

    void decode(std::unique_ptr<vts::VtsMsg> m);
    void resetDecoder();

    void sendHeartbeat();
    long rtt();

    size_t txBytes();
    size_t rxBytes();
};

#endif // PEER_H
