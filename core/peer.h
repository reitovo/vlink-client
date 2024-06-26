#ifndef PEER_H
#define PEER_H

#include "NatTypeProbe/p2p_api.h"
#include <QString>
#include "av_to_d3d.h"
#include "core/smart_buf.h"
#include "qthread.h"
#include "rtc/rtc.hpp"
#include "proto/vts.pb.h"
#include <QDateTime>
#include <QJsonObject>
#include "concurrentqueue/concurrentqueue.h"
#include "proto/vts_server.pb.h"
#include "ui/widgets/peeritemwidget.h"
#include "speed.h"

class CollabRoom;

// This class is holding either server or client peer connection using libdatachannel
class Peer {
    CollabRoom* room;
    std::unique_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> dc;
    bool forceRelay;

    std::unique_ptr<AvToDx> dec;

    QMutex pcLock;
    SpeedStat txSpeedCount;
    SpeedStat rxSpeedCount;

    void printSelectedCandidate();

public:
    QString remotePeerId;
    std::string nick;

    Peer(CollabRoom* room, QString remoteId);
    ~Peer();

    bool connected();
    bool usingTurn();
    bool failed();

    void startServer();
    void startClient();
    void close();

    QMutex decoderMutex;
    void startDecoder();
    void stopDecoder();

    inline void setNick(const std::string& name) {
        nick = name;
        dec->setNick(nick);
    }

    std::atomic_bool dcInited = false;
    std::atomic_bool dcThreadAlive;
    std::unique_ptr<QThread> dcThread;
    moodycamel::ConcurrentQueue<std::shared_ptr<vts::VtsMsg>> sendQueue;
    moodycamel::ConcurrentQueue<std::unique_ptr<vts::VtsMsg>> recvQueue;
    std::unique_ptr<smart_buf> smartBuf;
    void sendAsync(std::shared_ptr<vts::VtsMsg> payload);

    QString dataStats();
    void addRemoteCandidate(const vts::server::Candidate& candidate);
    void setRemoteSdp(const vts::server::Sdp& sdp);
    void initSmartBuf();

    void decode(std::unique_ptr<vts::VtsMsg> m);
    void resetDecoder();

    void sendHeartbeat();
    long rtt();

    size_t txBytes();
    size_t rxBytes();

    size_t txSpeed();
    size_t rxSpeed();
};

#endif // PEER_H
