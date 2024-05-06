#include "peer.h"
#include <QTimer>
#include <utility>
#include "util.h"
#include <QSettings>

Peer::Peer(CollabRoom *room, QString id) {
    this->room = room;
    this->remotePeerId = id;

    QSettings s;
    forceRelay = s.value("forceRelay").toBool();

    dcThreadAlive = true;
    dcThread = std::unique_ptr<QThread>(QThread::create([=, this]() {
        while (dcThreadAlive) {
            QThread::usleep(100);

            if (!dcInited) {
                continue;
            }

            std::shared_ptr<vts::VtsMsg> msg;
            if (sendQueue.try_dequeue(msg)) {
                auto data = msg->SerializeAsString();
                smartBuf->send(data);
            }

            std::unique_ptr<vts::VtsMsg> m;
            if (recvQueue.try_dequeue(m)) {
                // receive av from remote peer
                switch (m->type()) {
                    case vts::VTS_MSG_AVFRAME: {
                        decode(std::move(m));
                        break;
                    }
                    case vts::VTS_MSG_AVSTOP: {
                        resetDecoder();
                        break;
                    }
                    case vts::VTS_MSG_HEARTBEAT: {
                        // server first, then client reply
                        qDebug() << "receive dc heartbeat from" << remotePeerId;
                        if (!room->isServer) {
                            sendHeartbeat();
                        }
                        break;
                    }
                    default: {
                        break;
                    }
                }
            }
        }
    }));
    dcThread->start();

    startDecoder();
}

Peer::~Peer() {
    dcThreadAlive = false;
    terminateQThread(dcThread, __FUNCTION__);
    stopDecoder();
    close();

    qDebug() << "Peer destroyed";
}

bool Peer::connected() {
    pcLock.lock();
    auto ret = dc != nullptr && dc->isOpen();
    pcLock.unlock();
    return ret;
}

bool Peer::usingTurn() {
    if (pc == nullptr)
        return false;

    rtc::Candidate local, remote;
    if (!pc->getSelectedCandidatePair(&local, &remote)) {
        return false;
    }

    return remote.type() == rtc::Candidate::Type::Relayed || local.type() == rtc::Candidate::Type::Relayed;
}

void Peer::startServer() {
    rtc::Configuration config;

    config.mtu = 1400;
    config.maxMessageSize = 512 * 1024;
    config.iceServers.emplace_back("stun:stun.miwifi.com:3478");
    if (!room->turnServer.isEmpty()) {
        if (forceRelay) {
            config.iceTransportPolicy = rtc::TransportPolicy::Relay;
        }
        config.iceServers.emplace_back("turn:" + room->turnServer.toStdString());
    }

    pc = std::make_unique<rtc::PeerConnection>(config);

    pc->onStateChange([=, this](rtc::PeerConnection::State state) {
        qDebugStd("Server rtcState: " << state);
        if (state == rtc::PeerConnection::State::Failed) {
            emit room->onRtcFailed(this);
            startServer();
        }
    });

    pc->onGatheringStateChange([=, this](rtc::PeerConnection::GatheringState state) {
        qDebugStd("Server Gathering state: " << state);
    });

    pc->onLocalDescription([=, this](const rtc::Description &description) {
        vts::server::Sdp sdp;
        sdp.set_type(description.typeString());
        sdp.set_sdp(std::string(description));
        sdp.set_frompeerid(room->localPeerId.toStdString());
        sdp.set_topeerid(remotePeerId.toStdString());

        qDebug() << "Send server sdp to client";
        room->roomServer->setSdp(sdp);
    });

    pc->onLocalCandidate([=, this](const rtc::Candidate &candidate) {
        vts::server::Candidate c;
        c.set_mid(candidate.mid());
        c.set_candidate(candidate.candidate());
        c.set_frompeerid(room->localPeerId.toStdString());
        c.set_topeerid(remotePeerId.toStdString());

        qDebug() << "Send server candidate to client";
        qDebugStd(candidate);
        room->roomServer->setCandidate(c);
    });

    pcLock.lock();
    dc = pc->createDataChannel("vts");
    pcLock.unlock();

    dc->onOpen([this]() {
        qDebug() << "Server datachannel open";
        initSmartBuf();
        dcInited = true;
        printSelectedCandidate();
    });

    dc->onMessage([=, this](std::variant<rtc::binary, rtc::string> message) {
        if (std::holds_alternative<rtc::string>(message)) {
            auto &raw = get<rtc::string>(message);
            smartBuf->onReceive(raw);
        }
    });

    dc->onClosed([this]() {
        dcInited = false;
        qDebug() << "Server datachannel closed";
        smartBuf.reset();
    });
}

void Peer::startClient() {
    rtc::Configuration config;

    config.mtu = 1400;
    config.maxMessageSize = 512 * 1024;
    config.iceServers.emplace_back("stun:stun.miwifi.com:3478");
    const auto &turnServer = room->turnServer.toStdString();
    if (!turnServer.empty()) {
        if (forceRelay) {
            config.iceTransportPolicy = rtc::TransportPolicy::Relay;
        }
        config.iceServers.emplace_back("turn:" + turnServer);
    }

    pc = std::make_unique<rtc::PeerConnection>(config);

    pc->onStateChange(
            [this](rtc::PeerConnection::State state) {
                qDebugStd("Client RtcState: " << state);
                if (state == rtc::PeerConnection::State::Failed) {
                    emit room->onRtcFailed(this);
                }
            });

    pc->onGatheringStateChange([=, this](rtc::PeerConnection::GatheringState state) {
        qDebugStd("Client Gathering state: " << state);
    });

    pc->onLocalDescription([=, this](const rtc::Description &description) {
        vts::server::Sdp sdp;
        sdp.set_type(description.typeString());
        sdp.set_sdp(std::string(description));
        sdp.set_frompeerid(room->localPeerId.toStdString());
        sdp.set_topeerid(remotePeerId.toStdString());

        qDebug() << "Send client sdp to server";
        room->roomServer->setSdp(sdp);
    });

    pc->onLocalCandidate([=, this](const rtc::Candidate &candidate) {
        vts::server::Candidate c;
        c.set_mid(candidate.mid());
        c.set_candidate(candidate.candidate());
        c.set_frompeerid(room->localPeerId.toStdString());
        c.set_topeerid(remotePeerId.toStdString());

        qDebug() << "Send client candidate to server";
        room->roomServer->setCandidate(c);
    });

    pc->onDataChannel([=, this](std::shared_ptr<rtc::DataChannel> incoming) {
        pcLock.lock();
        dc = std::move(incoming);
        pcLock.unlock();

        qDebug() << "Client incoming server data channel " << QString::fromStdString(pc->remoteAddress().value());

        dc->onOpen([this]() {
            qDebug() << "Client datachannel open";
            initSmartBuf();
            dcInited = true;
            printSelectedCandidate();
        });

        dc->onMessage([=, this](std::variant<rtc::binary, rtc::string> message) {
            if (std::holds_alternative<rtc::string>(message)) {
                auto &raw = get<rtc::string>(message);
                smartBuf->onReceive(raw);
            }
        });

        dc->onClosed([this]() {
            dcInited = false;
            qDebug() << "Client datachannel closed";
            smartBuf.reset();
        });
    });
}

void Peer::close() {
    dcInited = false;
    if (dc != nullptr) {
        pcLock.lock();
        dc->resetCallbacks();
        dc->close();
        dc = nullptr;
        pcLock.unlock();
    }
    if (pc != nullptr) {
        pc->resetCallbacks();
        pc->close();
        pc = nullptr;
    }
    smartBuf.reset();
}

void Peer::sendAsync(std::shared_ptr<vts::VtsMsg> payload) {
    if (!connected()) {
        return;
    }

    if (sendQueue.size_approx() > 15) {
        //qDebug() << "throw away payload because queue is full";
        return;
    }

    sendQueue.enqueue(std::move(payload));
}

QString Peer::dataStats() {
    if (pc == nullptr)
        return {};
    return QString("%1 ↑↓ %2")
            .arg(humanizeBytes(pc->bytesSent()))
            .arg(humanizeBytes(pc->bytesReceived()));
}

void Peer::addRemoteCandidate(const vts::server::Candidate &candidate) {
    if (pc == nullptr || pc->state() == rtc::PeerConnection::State::Connected)
        return;
    if (connected())
        return;

    qDebug() << "add remote candidate";
    auto c = rtc::Candidate(candidate.candidate(), candidate.mid());
    pc->addRemoteCandidate(c);
    qDebugStd(c);
}

void Peer::setRemoteSdp(const vts::server::Sdp &sdp) {
    if (pc == nullptr || pc->state() == rtc::PeerConnection::State::Connected)
        return;
    if (connected())
        return;

    qDebug() << "set remote sdp";
    qDebugStd(pc->signalingState() << " " << pc->state() << " " << pc->gatheringState());
    auto description = rtc::Description(sdp.sdp(), sdp.type());
    pc->setRemoteDescription(description);
    qDebugStd(description);
}

void Peer::initSmartBuf() {
    smartBuf = std::make_unique<smart_buf>(dc->maxMessageSize(), [this](auto data) {
        dc->send(std::variant<rtc::binary, rtc::string>(data));
    }, [this](auto data) {
        auto msg = std::make_unique<vts::VtsMsg>();
        if (msg->ParseFromArray(data.data(), data.size())) {
            recvQueue.enqueue(std::move(msg));
        } else {
            qDebug() << "invalid dc message at server";
        }
    });
}

void Peer::decode(std::unique_ptr<vts::VtsMsg> m) {
    ScopedQMutex _(&decoderMutex);
    if (dec != nullptr)
        dec->process(std::move(m));
}

void Peer::resetDecoder() {
    ScopedQMutex _(&decoderMutex);
    if (dec != nullptr)
        dec->reset();
}


void Peer::startDecoder() {
    ScopedQMutex _(&decoderMutex);
    dec = std::make_unique<AvToDx>(remotePeerId.toStdString(), nick, room->quality, room->d3d);
}

void Peer::stopDecoder() {
    ScopedQMutex _(&decoderMutex);
    dec.reset();
}


void Peer::sendHeartbeat() {
    qDebug() << "send dc heartbeat to" << remotePeerId;
    std::unique_ptr<vts::VtsMsg> hb = std::make_unique<vts::VtsMsg>();
    hb->set_type(vts::VTS_MSG_HEARTBEAT);
    sendAsync(std::move(hb));
}

long Peer::rtt() {
    if (pc == nullptr)
        return 0;
    auto v = pc->rtt();
    if (v.has_value()) {
        return v->count();
    }
    return 0;
}

void Peer::printSelectedCandidate() {
    if (pc == nullptr)
        return;

    rtc::Candidate local, remote;
    if (!pc->getSelectedCandidatePair(&local, &remote)) {
        return;
    }

    qDebug() << "Local Selected Candidate";
    qDebug() << QString::fromStdString(std::string(local));
    qDebug() << "Remote Selected Candidate";
    qDebug() << QString::fromStdString(std::string(remote));
}

bool Peer::failed() {
    if (pc == nullptr)
        return false;
    return pc->state() == rtc::PeerConnection::State::Failed;
}

size_t Peer::txBytes() {
    if (pc == nullptr)
        return 0;

    return pc->bytesSent();
}

size_t Peer::rxBytes() {
    if (pc == nullptr)
        return 0;

    return pc->bytesReceived();
}

static QMutex rxSpeedMutex;

size_t Peer::rxSpeed() {
    ScopedQMutex _(&rxSpeedMutex);
    rxSpeedCount.update(rxBytes());
    return rxSpeedCount.speedBytes();
}

static QMutex txSpeedMutex;

size_t Peer::txSpeed() {
    ScopedQMutex _(&txSpeedMutex);
    txSpeedCount.update(txBytes());
    return txSpeedCount.speedBytes();
}
