//
// Created by reito on 2023/4/8.
//

#include "grpc++/grpc++.h"
#include "room_server.h"
#include "vtslink.h"
#include "ui/windows/collabroom.h"
#include "NatTypeProbe/NatProb.h"
#include <QDebug>
#include "grpc_cert.h"

RoomServer::RoomServer(CollabRoom* room) {
    this->room = room;

    channel = grpc::CreateChannel(VTSLINK_GRPC_ENDPOINT, grpc::SslCredentials(
            grpc::SslCredentialsOptions(ISRG_Root_X1, "", "")));
    service = vts::server::RoomService::NewStub(channel);
}

void RoomServer::handleMessage(const vts::server::Notify &notify) {
    switch (notify.notify_case()) {
        case vts::server::Notify::kPeers:
            room->onNotifyPeers(notify.peers());
            break;
        case vts::server::Notify::kSdp:
            room->onNotifySdp(notify.sdp());
            break;
        case vts::server::Notify::kFrame:
            room->onNotifyFrameFormat(notify.frame());
            break;
        case vts::server::Notify::kRoomDestroy: {
            static QMutex destroyedMutex;
            ScopedQMutex _(&destroyedMutex);
            if (!destroyed) {
                destroyed = true;
                room->onNotifyDestroy();
            }
        }
        case vts::server::Notify::kForceIdr: {
            room->onNotifyForceIdr();
        }
        default:
            break;
    }
}

RoomServer::~RoomServer() {
    exit();
    exiting = true;
    if (notifyContext)
        notifyContext->TryCancel();
    terminateQThread(natThread, __FUNCTION__);
    terminateQThread(notifyThread, __FUNCTION__);
}

void RoomServer::startReceiveNotify() {
    notifyContext = getCtx();
    notifyContext->set_deadline(std::chrono::system_clock::time_point::max());
    auto notify = service->ReceiveNotify(notifyContext.get(), vts::server::ReqCommon());
    notifyThread = std::unique_ptr<QThread>(QThread::create([this, notify = std::move(notify)] {
        vts::server::Notify msg;
        while (notify->Read(&msg) && !exiting) {
            handleMessage(msg);
        }
        notify->Finish();
        qDebug() << "notify thread exited";
    }));
    notifyThread->start();
}

void RoomServer::startNatTypeDetect() {
    natThread = std::unique_ptr<QThread>(QThread::create([=, this]() {
        qDebug() << "Start nat type determine";
        CNatProb natProb;
        if (!natProb.Init("stun.miwifi.com")) {
            qDebug() << "natProb init failed.";
        }
        int retry = 0;
        NatType type;
        while (true) {
            type = natProb.GetNatType();
            if (type != StunTypeBlocked && type != StunTypeFailure && type != StunTypeUnknown && retry++ < 3) {
                break;
            }
            QThread::msleep(100);
        }
        qDebug() << "nat type" << QString(natProb.DescribeNatType(type).c_str());
        localNatType = type;

        if (exiting)
            return;

        setNat(type);
    }));
    natThread->start();
}

void RoomServer::createRoom(const vts::server::ReqCreateRoom& req) {
    this->peerId = req.peerid();

    auto worker = QThread::create([=, this]() {
        vts::server::RspRoomInfo rsp;

        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
        auto status = service->CreateRoom(&context, req, &rsp);

        if (!status.ok()) {
            qDebug() << "createRoom failed:" << status.error_message().c_str();
            emit room->onRoomInfoFailed(status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ? "room init timeout" : status.error_message());
            return;
        }

        this->roomId = rsp.roomid();
        startReceiveNotify();
        startNatTypeDetect();
        emit room->onRoomInfoSucceed(rsp);
    });
    CollabRoom::connect(worker, &QThread::finished, worker, &QThread::deleteLater);
    worker->setParent(room);
    worker->start();
}

void RoomServer::joinRoom(const std::string& peerId, const std::string& roomId, const std::string& nick) {
    this->peerId = peerId;
    this->roomId = roomId;

    auto worker = QThread::create([=, this]() {
        vts::server::RspRoomInfo rsp;

        vts::server::ReqJoinRoom req;
        req.set_peerid(peerId);
        req.set_roomid(roomId);
        req.set_nick(nick);

        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
        auto status = service->JoinRoom(&context, req, &rsp);

        if (!status.ok()) {
            qDebug() << "joinRoom failed:" << status.error_message().c_str();
            emit room->onRoomInfoFailed(status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ? "room init timeout" : status.error_message());
            return;
        }

        startReceiveNotify();
        startNatTypeDetect();
        emit room->onRoomInfoSucceed(rsp);
    });
    CollabRoom::connect(worker, &QThread::finished, worker, &QThread::deleteLater);
    worker->setParent(room);
    worker->start();
}

void RoomServer::setSdp(const vts::server::Sdp &sdp) {
    auto context = getCtx();
    vts::server::RspCommon rsp;
    auto status = service->SetSdp(context.get(), sdp, &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
    }
}

void RoomServer::setNat(int type) {
    auto context = getCtx();

    vts::server::ReqNatType req;
    req.set_nattype(type);

    vts::server::RspCommon rsp;
    auto status = service->SetNatType(context.get(), req, &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
    }
}

void RoomServer::setRtt(const vts::server::ReqRtt &rtt) {
    auto context = getCtx();
    vts::server::RspCommon rsp;
    auto status = service->SetRtt(context.get(), rtt, &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
    }
}

void RoomServer::setNick(const string &nick) {
    auto context = getCtx();

    vts::server::ReqNickname req;
    req.set_nick(nick);

    vts::server::RspCommon rsp;
    auto status = service->SetNickName(context.get(), req, &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
    }
}

void RoomServer::setFrameFormat(const vts::server::FrameFormatSetting &format) {
    auto context = getCtx();
    vts::server::RspCommon rsp;
    auto status = service->SetFrameFormat(context.get(), format, &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
    }
}

void RoomServer::exit() {
    auto context = getCtx();
    vts::server::RspCommon rsp;
    auto status = service->Exit(context.get(), vts::server::ReqCommon(), &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
    }
}

void RoomServer::requestIdr() {
    auto now = std::chrono::system_clock::now();
    if (lastRequestIdr + std::chrono::seconds(1) > now) {
        return;
    }
    lastRequestIdr = now;
    qDebug() << "request idr";

    auto context = getCtx();
    vts::server::RspCommon rsp;
    auto status = service->RequestIdr(context.get(), vts::server::ReqCommon(), &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
    }
}
