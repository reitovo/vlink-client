#include "peer.h"
#include "collabroom.h"

void Peer::startServer()
{
    rtc::Configuration config;
#ifdef QT_DEBUG
    config.iceServers.emplace_back("turn:a:b@stun.reito.fun:3478");
#endif
    config.iceServers.emplace_back("stun:stun.qq.com:3478");
    config.iceServers.emplace_back("stun:stun.miwifi.com:3478");

    if (!room->turnServer.isEmpty())
        config.iceServers.emplace_back(room->turnServer.toStdString());

    pc = std::make_unique<rtc::PeerConnection>(config);

    pc->onStateChange([=](rtc::PeerConnection::State state) {
        std::cout << "Server RtcState: " << state << std::endl;
        if (state == rtc::PeerConnection::State::Failed) {
            qDebug() << "peer connection state failed";
        }
    });

    pc->onGatheringStateChange([=](rtc::PeerConnection::GatheringState state) {
        std::cout << "Server Gathering state: " << state << std::endl;
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = pc->localDescription();
            QJsonObject json;
            json["type"] = QString::fromStdString(description->typeString().c_str());
            json["sdp"] = QString::fromStdString(std::string(description.value()));
            json["target"] = id;
            json["turn"] = room->turnServer;
            QJsonObject dto;
            dto["sdp"] = json;
            dto["type"] = "sdp";
            QJsonDocument doc(dto);

            auto content = QString::fromUtf8(doc.toJson()).toStdString();
            std::cout << content << std::endl;

            if (room->ws->isOpen()) {
                qDebug() << "Send server sdp to client";
                room->ws->send(content);
            }
        }
    });

    dc = pc->createDataChannel("vts");

    dc->onOpen([]() {
        std::cout << "Server datachannel open" << std::endl;
    });

    dc->onMessage([=](std::variant<rtc::binary, rtc::string> message) {
        if (std::holds_alternative<rtc::binary>(message)) {
            auto ping = get<rtc::binary>(message);
            auto barr = QByteArray::fromRawData(reinterpret_cast<char*>(ping.data()), ping.size());
            std::cout << "Received: " << QString(barr).toStdString() << std::endl;
        }
    });

    dc->onClosed([]() {
        qDebug() << "Server datachannel closed";
    });
}

void Peer::startClient(QJsonObject serverSdp)
{
    rtc::Configuration config;
#ifdef QT_DEBUG
    config.iceServers.emplace_back("turn:a:b@stun.reito.fun:3478");
#endif
    config.iceServers.emplace_back("stun:stun.qq.com:3478");
    config.iceServers.emplace_back("stun:stun.miwifi.com:3478");

    auto turnServer = serverSdp["turn"].toString();
    if (!turnServer.isEmpty())
        config.iceServers.emplace_back(turnServer.toStdString());

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
            json["target"] = "server";
            QJsonObject dto;
            dto["sdp"] = json;
            dto["type"] = "sdp";
            QJsonDocument doc(dto);

            auto content = QString::fromUtf8(doc.toJson()).toStdString();
            std::cout << content << std::endl;

            if (room->ws->isOpen()) {
                qDebug() << "Send client sdp to server";
                room->ws->send(content);
            }
        }
    });

    pc->onDataChannel([=](std::shared_ptr<rtc::DataChannel> incoming) {
        dc = incoming;
        std::cout << "Client incoming server data channel " << pc->remoteAddress().value() << std::endl;
    });

    pc->setRemoteDescription(rtc::Description(serverSdp["sdp"].toString().toStdString(), serverSdp["type"].toString().toStdString()));
}

void Peer::close()
{
    if (dc != nullptr) {
        dc->close();
        dc = nullptr;
    }
    if (pc != nullptr) {
        pc->close();
        pc = nullptr;
    }
}
