//
// Created by reito on 2023/4/8.
//

#ifndef VTSLINK_ROOM_SERVER_H
#define VTSLINK_ROOM_SERVER_H

#include "vts_server.pb.h"
#include "QMutex"
#include <ixwebsocket/IXWebSocket.h>

class CollabRoom;
class RoomServer {
    QMutex wsLock;
    std::unique_ptr<ix::WebSocket> ws;
    CollabRoom* room;

public:
    explicit RoomServer(CollabRoom* room);
    ~RoomServer();
    void sendRtcSdp(const vts::server::MessageRtcSdp& sdp);
    void sendChangeTurn(const vts::server::TurnServerSetting& turn);

    void handleMessage(const ix::WebSocketMessagePtr &msg);
};

#endif //VTSLINK_ROOM_SERVER_H
