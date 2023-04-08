//
// Created by reito on 2023/4/8.
//

#include "room_server.h"
#include "vtslink.h"
#include "ui/windows/collabroom.h"
#include <QDebug>

void RoomServer::sendRtcSdp(const vts::server::MessageRtcSdp &sdp) {

}

RoomServer::RoomServer(CollabRoom* room) {
    this->room = room;
    ws = std::make_unique<ix::WebSocket>();

    ws->setUrl(VTSLINK_WS_BASEURL "/api/room/join");
    ws->setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
        handleMessage(msg);
    });
    ws->start();
}

void RoomServer::handleMessage(const ix::WebSocketMessagePtr &msg) {
    wsLock.lock();
    if (msg->type == ix::WebSocketMessageType::Open) {
        qDebug() << "Room Server Connected";
    } else if (msg->type == ix::WebSocketMessageType::Close) {
        qDebug() << "Room Server Disconnected";
    } else if (msg->type == ix::WebSocketMessageType::Message) {
        vts::server::VtsNotify notify;
        notify.ParseFromString(msg->str);
        switch (notify.type()) {
            case vts::server::VtsNotify_NotifyType_NotifyRoomInfo:
                room->onNotifyRoomInfo(notify.roominfo());
                break;
            case vts::server::VtsNotify_NotifyType_NotifyRtcSdp:
                break;
            default:
                break;
        }
    }
    wsLock.unlock();
}

RoomServer::~RoomServer() {
    wsLock.lock();
    ws->stop();
    ws.reset();
    wsLock.unlock();
}
