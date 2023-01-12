#include "peer.h"
#include "ui/windows/collabroom.h"
#include <QTimer>
#include "util.h"

Peer::Peer(CollabRoom *room, QString id, QDateTime timeVersion) {
    this->room = room;
    this->peerId = id;
    this->sdpTime = timeVersion;

    dcThreadAlive = true;
    dcThread = std::unique_ptr<QThread>(QThread::create([=]() {
        while (dcThreadAlive) {
            QThread::usleep(500);

            if (!dcInited) {
                continue;
            }

            std::shared_ptr<VtsMsg> msg;
            if (sendQueue.try_dequeue(msg)) {
                auto data = msg->SerializeAsString();
                smartBuf->send(data);
            }

            std::unique_ptr<VtsMsg> msg2;
            if (recvQueue.try_dequeue(msg2)) {
                room->peerDataChannelMessage(std::move(msg2), this);
            }
        }
    }));
    dcThread->start();

    dec = std::make_unique<AvToDx>(room->d3d);
}

Peer::~Peer() {
    dcThreadAlive = false;

    if (dcThread != nullptr && !dcThread->isFinished() && !dcThread->wait(500)) {
        qWarning() << "uneasy to exit peer send thread";
        dcThread->terminate();
        dcThread->wait(500);
        dcThread = nullptr;
    }

    close();

    qDebug() << "Peer destroyed";
}

bool Peer::connected() {
    dcLock.lock();
    auto ret = dc != nullptr && dc->isOpen();
    dcLock.unlock();
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
    config.iceServers.emplace_back("stun:stun.qq.com:3478");
    config.iceServers.emplace_back("stun:stun.miwifi.com:3478");
    config.iceServers.emplace_back("stun:stun.syncthing.net:3478");
    config.iceServers.emplace_back("stun:stun.stunprotocol.org:3478");

    if (!room->turnServer.isEmpty())
        config.iceServers.emplace_back("turn:" + room->turnServer.toStdString());

    pc = std::make_unique<rtc::PeerConnection>(config);

    pc->onStateChange([=](rtc::PeerConnection::State state) {
        qDebugStd("Server RtcState: " << state);
        if (state == rtc::PeerConnection::State::Failed) {
            emit room->onRtcFailed(this);
        }
    });

    pc->onGatheringStateChange([=](rtc::PeerConnection::GatheringState state) {
        qDebugStd("Server Gathering state: " << state);
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = pc->localDescription();
            if (description.has_value()) {
                auto desc = processLocalDescription(description.value());

                QJsonObject json;
                json["type"] = QString::fromStdString(desc.typeString());
                json["sdp"] = QString::fromStdString(std::string(desc));
                json["target"] = peerId;
                json["turn"] = room->turnServer;
                json["time"] = sdpTime.toMSecsSinceEpoch();
                QJsonObject dto;
                dto["sdp"] = json;
                dto["type"] = "sdp";
                QJsonDocument doc(dto);

                auto content = QString::fromUtf8(doc.toJson()).toStdString();
                qDebugStd(content.c_str());

                qDebug() << "Send server sdp to client";
                room->wsSendAsync(content);
            }
        }
    });

    dcLock.lock();
    dc = pc->createDataChannel("vts");
    dcLock.unlock();

    dc->onOpen([this]() {
        qDebug() << "Server datachannel open";
        initSmartBuf();
        dcInited = true;
        printSelectedCandidate();
    });

    dc->onMessage([=](std::variant<rtc::binary, rtc::string> message) {
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

void Peer::startClient(QJsonObject serverSdp) {
    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.qq.com:3478");
    config.iceServers.emplace_back("stun:stun.miwifi.com:3478");
    config.iceServers.emplace_back("stun:stun.syncthing.net:3478");
    config.iceServers.emplace_back("stun:stun.stunprotocol.org:3478");

    auto turnServer = serverSdp["turn"].toString();
    if (!turnServer.isEmpty())
        config.iceServers.emplace_back("turn:" + turnServer.toStdString());

    pc = std::make_unique<rtc::PeerConnection>(config);

    pc->onStateChange(
            [this](rtc::PeerConnection::State state) {
                qDebugStd("Client RtcState: " << state);
                if (state == rtc::PeerConnection::State::Failed) {
                    emit room->onRtcFailed(this);
                }
            });

    pc->onGatheringStateChange([=](rtc::PeerConnection::GatheringState state) {
        qDebugStd("Client Gathering state: " << state);
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = pc->localDescription();
            if (description.has_value()) {
                auto desc = processLocalDescription(description.value());

                QJsonObject json;
                json["type"] = QString::fromStdString(desc.typeString());
                json["sdp"] = QString::fromStdString(std::string(desc));
                json["time"] = sdpTime.toMSecsSinceEpoch();
                json["turn"] = turnServer;
                json["target"] = "server";
                QJsonObject dto;
                dto["sdp"] = json;
                dto["type"] = "sdp";
                QJsonDocument doc(dto);

                auto content = QString::fromUtf8(doc.toJson()).toStdString();
                qDebugStd(content.c_str());

                qDebug() << "Send client sdp to server";
                room->wsSendAsync(content);
            }
        }
    });

    pc->onDataChannel([=](std::shared_ptr<rtc::DataChannel> incoming) {
        dcLock.lock();
        dc = incoming;
        dcLock.unlock();

        qDebug() << "Client incoming server data channel " << QString::fromStdString(pc->remoteAddress().value());

        dc->onOpen([this]() {
            qDebug() << "Client datachannel open";
            initSmartBuf();
            dcInited = true;
            printSelectedCandidate();
        });

        dc->onMessage([=](std::variant<rtc::binary, rtc::string> message) {
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

    qDebug() << "set server remote sdp";
    qDebugStd(pc->signalingState() << pc->state() << pc->gatheringState());
    auto description = rtc::Description(serverSdp["sdp"].toString().toStdString(),
                                        serverSdp["type"].toString().toStdString());
    pc->setRemoteDescription(description);
    qDebugStd(description);
}

void Peer::close() {
    dcInited = false;
    if (dc != nullptr) {
        dcLock.lock();
        dc->resetCallbacks();
        dc->close();
        dc = nullptr;
        dcLock.unlock();
    }
    if (pc != nullptr) {
        pc->resetCallbacks();
        pc->close();
        pc = nullptr;
    }
    smartBuf.reset();
}

void Peer::sendAsync(std::shared_ptr<VtsMsg> payload) {
    if (sendQueue.size_approx() > 10) {
        //qDebug() << "throw away payload because queue is full";
        return;
    }

    sendQueue.enqueue(std::move(payload));
}

QString Peer::dataStats() {
    if (pc == nullptr)
        return QString();
    return QString("%1 ↑↓ %2")
            .arg(humanizeBytes(pc->bytesSent()))
            .arg(humanizeBytes(pc->bytesReceived()));
}

QDateTime Peer::timeVersion() {
    return sdpTime;
}

void Peer::setClientRemoteSdp(QJsonObject sdp) {
    if (pc == nullptr || pc->state() == rtc::PeerConnection::State::Connected ||
        pc->signalingState() == rtc::PeerConnection::SignalingState::Stable)
        return;
    if (connected())
        return;

    qDebug() << "set client remote sdp";
    qDebugStd(pc->signalingState() << pc->state() << pc->gatheringState());
    auto description = rtc::Description(sdp["sdp"].toString().toStdString(),
                                        sdp["type"].toString().toStdString());
    pc->setRemoteDescription(description);
    qDebugStd(description);
}

void Peer::initSmartBuf() {
    smartBuf = std::make_unique<smart_buf>(dc->maxMessageSize(), [this](auto data) {
        dc->send(std::variant<rtc::binary, rtc::string>(data));
    }, [this](auto data) {
        auto msg = std::make_unique<VtsMsg>();
        if (msg->ParseFromArray(data.data(), data.size())) {
            recvQueue.enqueue(std::move(msg));
        } else {
            qDebug() << "invalid dc message at server";
        }
    });
}

void Peer::decode(std::unique_ptr<VtsMsg> m) {
    dec->process(std::move(m));
}

void Peer::resetDecoder() {
    dec->reset();
}

void Peer::sendHeartbeat() {
    qDebug() << "send dc heartbeat";
    std::unique_ptr<VtsMsg> hb = std::make_unique<VtsMsg>();
    hb->set_type(VTS_MSG_HEARTBEAT);
    sendQueue.enqueue(std::move(hb));
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

rtc::Description Peer::processLocalDescription(rtc::Description desc) {
    auto candidates = desc.extractCandidates();
    auto ret = desc;

//    for (auto it = candidates.begin(); it != candidates.end();) {
//        bool remove = false;
//
//        if (it->type() != rtc::Candidate::Type::ServerReflexive || it->family() != rtc::Candidate::Family::Ipv4)
//            remove = true;
//
//        if (remove) {
//            it = candidates.erase(it);
//        } else {
//            it++;
//        }
//    }

    ret.addCandidates(candidates);
    return ret;
}
