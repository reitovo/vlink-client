//
// Created by reito on 2023/4/8.
//

#ifndef VTSLINK_ROOM_SERVER_H
#define VTSLINK_ROOM_SERVER_H

#include "proto/vts_server.pb.h"
#include "proto/vts_server.grpc.pb.h"
#include "QMutex"
#include "NatTypeProbe/p2p_api.h"
#include <QThread>

class CollabRoom;
class RoomServer {
    CollabRoom* room;

    std::atomic_bool exiting = false;
    std::atomic_bool destroyed = false;

    std::unique_ptr<QThread> natThread;
    std::unique_ptr<QThread> notifyThread;
    std::unique_ptr<grpc::ClientContext> notifyContext;
    std::unique_ptr<vts::server::RoomService::Stub> service;
    std::shared_ptr<grpc::Channel> channel;

    std::string peerId;
    std::string roomId;

    NatType localNatType = NatType::StunTypeUnknown;

    std::chrono::time_point<std::chrono::system_clock> lastRequestIdr = std::chrono::system_clock::time_point::min();

public:
    explicit RoomServer(CollabRoom* room);
    ~RoomServer();

    void createRoom(const vts::server::ReqCreateRoom& req);
    void joinRoom(const std::string& peerId, const std::string& roomId, const std::string& nick);

    inline std::unique_ptr<grpc::ClientContext> getCtx() {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->AddMetadata("peerid", peerId);
        ctx->AddMetadata("roomid", roomId);
        return ctx;
    }

    void startReceiveNotify();
    void startNatTypeDetect();

    void setRtt(const vts::server::ReqRtt& rtt);
    void setNick(const std::string& nick);
    void setSdp(const vts::server::Sdp& sdp);
    void setNat(int type);
    void setFrameFormat(const vts::server::FrameFormatSetting& format);

    void requestIdr();

    void exit();
    void handleMessage(const vts::server::Notify &msg);

    inline NatType getLocalNatType() {
        return localNatType;
    }
};

#endif //VTSLINK_ROOM_SERVER_H
