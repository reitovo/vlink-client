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
            if (!connected()) {
                QThread::msleep(10);
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

bool Peer::connected()
{
    dcLock.lock();
    auto ret = dc != nullptr && dc->isOpen();
    dcLock.unlock();
    return ret;
}

bool Peer::usingTurn()
{
    if (pc == nullptr)
        return false;

    rtc::Candidate local, remote;
    if (!pc->getSelectedCandidatePair(&local, &remote)) {
        return false;
    }

    return remote.type() == rtc::Candidate::Type::Relayed || local.type() == rtc::Candidate::Type::Relayed;
}

void Peer::startServer()
{
    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.qq.com:3478");
    config.iceServers.emplace_back("stun:stun.miwifi.com:3478");

    if (!room->turnServer.isEmpty())
        config.iceServers.emplace_back("turn:" + room->turnServer.toStdString());

    pc = std::make_unique<rtc::PeerConnection>(config);

    pc->onStateChange([=](rtc::PeerConnection::State state) {
        std::cout << "Server RtcState: " << state << std::endl;
        if (state == rtc::PeerConnection::State::Failed) {
            qDebug() << "peer connection state failed";
            // try reset in a outside monitor thread loop
        }
    });

    pc->onGatheringStateChange([=](rtc::PeerConnection::GatheringState state) {
        std::cout << "Server Gathering state: " << state << std::endl;
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = pc->localDescription();
            QJsonObject json;
            json["type"] = QString::fromStdString(description->typeString().c_str());
            json["sdp"] = QString::fromStdString(std::string(description.value()));
            json["target"] = peerId;
            json["turn"] = room->turnServer;
            json["time"] = sdpTime.toMSecsSinceEpoch();
            QJsonObject dto;
            dto["sdp"] = json;
            dto["type"] = "sdp";
            QJsonDocument doc(dto);

            auto content = QString::fromUtf8(doc.toJson()).toStdString();
            std::cout << content << std::endl;

            qDebug() << "Send server sdp to client";
            room->wsSendAsync(content);
        }
    });

    dcLock.lock();
    dc = pc->createDataChannel("vts");
    dcLock.unlock();

    dc->onOpen([this]() {
        std::cout << "Server datachannel open" << std::endl;
        initSmartBuf();
    });

    dc->onMessage([=](std::variant<rtc::binary, rtc::string> message) {
        if (std::holds_alternative<rtc::string>(message)) {
            auto& raw = get<rtc::string>(message);
            smartBuf->onReceive(raw);
        }
    });

    dc->onClosed([this]() {
        qDebug() << "Server datachannel closed";
        smartBuf.reset();
    });
}

void Peer::startClient(QJsonObject serverSdp)
{
    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.qq.com:3478");
    config.iceServers.emplace_back("stun:stun.miwifi.com:3478");

    auto turnServer = serverSdp["turn"].toString();
    if (!turnServer.isEmpty())
        config.iceServers.emplace_back("turn:" + turnServer.toStdString());

    pc = std::make_unique<rtc::PeerConnection>(config);

    pc->onStateChange(
                [](rtc::PeerConnection::State state) {
        std::cout << "Client RtcState: " << state << std::endl;
        // We should do nothing about failed at client side, as the logic starts from server side.
    });

    pc->onGatheringStateChange([=](rtc::PeerConnection::GatheringState state) {
        std::cout << "Client Gathering state: " << state << std::endl;
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = pc->localDescription();
            QJsonObject json;
            json["type"] = QString::fromStdString(description->typeString().c_str());
            json["sdp"] = QString::fromStdString(std::string(description.value()));
            json["time"] = sdpTime.toMSecsSinceEpoch();
            json["turn"] = turnServer;
            json["target"] = "server";
            QJsonObject dto;
            dto["sdp"] = json;
            dto["type"] = "sdp";
            QJsonDocument doc(dto);

            auto content = QString::fromUtf8(doc.toJson()).toStdString();
            std::cout << content << std::endl;

            qDebug() << "Send client sdp to server";
            room->wsSendAsync(content);
        }
    });

    pc->onDataChannel([=](std::shared_ptr<rtc::DataChannel> incoming) {
        dcLock.lock();
        dc = incoming;
        dcLock.unlock();

        qDebug() << "Client incoming server data channel " << QString::fromStdString(pc->remoteAddress().value());

        dc->onOpen([this]() {
            qDebug() << "Server datachannel open";
            initSmartBuf();
        });

        dc->onMessage([=](std::variant<rtc::binary, rtc::string> message) {
            if (std::holds_alternative<rtc::string>(message)) {
                auto& raw = get<rtc::string>(message);
                smartBuf->onReceive(raw);
            }
        });

        dc->onClosed([this]() {
            qDebug() << "Server datachannel closed";
            smartBuf.reset();
        });
    });

    pc->setRemoteDescription(rtc::Description(serverSdp["sdp"].toString().toStdString(),
                             serverSdp["type"].toString().toStdString()));
}

void Peer::close()
{
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

void Peer::sendAsync(std::shared_ptr<VtsMsg> payload)
{
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

void Peer::setRemoteSdp(QJsonObject sdp) {
    if (pc == nullptr ||
            pc->remoteDescription().has_value() ||
            pc->state() == rtc::PeerConnection::State::Connected)
        return;
    if (connected())
        return;
    pc->setRemoteDescription(rtc::Description(sdp["sdp"].toString().toStdString(), sdp["type"].toString().toStdString()));
}

void Peer::initSmartBuf()
{
    smartBuf = std::make_unique<smartbuf>(dc->maxMessageSize(), [this](auto data) {
        dc->send(std::variant<rtc::binary, rtc::string>(data));
    }, [this] (auto data) {
        auto msg = std::make_unique<VtsMsg>();
        if (msg->ParseFromArray(data.data(), data.size())) {
            recvQueue.enqueue(std::move(msg));
        } else {
            qDebug() << "invalid dc message at server";
        }
    });
}

void Peer::decode(const VtsAvFrame &frame)
{
    dec->process(frame);
}

void Peer::sendHeartbeat()
{
    qDebug() << "send dc heartbeat";
    std::unique_ptr<VtsMsg> hb = std::make_unique<VtsMsg>();
    hb->set_version(1);
    hb->set_type(VTS_MSG_HEARTBEAT);
    sendQueue.enqueue(std::move(hb));
}

long Peer::rtt()
{
    if (pc == nullptr)
        return 0;
    auto v = pc->rtt();
    if (v.has_value()) {
        return v->count();
    }
    return 0;
}
