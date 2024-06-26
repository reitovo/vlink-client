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
#include "QSettings"

RoomServer::RoomServer(CollabRoom* room) {
    this->room = room;

    const auto cred = room->privateServerNoSsl ? grpc::InsecureChannelCredentials() : grpc::SslCredentials(
            grpc::SslCredentialsOptions(ISRG_Root_X1, "", "")) ;
    channel = grpc::CreateChannel(room->roomEndpoint.toStdString(), cred);
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
        case vts::server::Notify::kCandidate:
            room->onNotifyCandidate(notify.candidate());
            break;
        case vts::server::Notify::kFrame:
            room->onNotifyFrameFormat(notify.frame());
            break;
        case vts::server::Notify::kTurn:
            room->onNotifyTurn(notify.turn().turn());
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
    startNatTypeDetect();
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
            emit room->onRoomInfoFailed("room init error " + std::to_string(status.error_code()));
            return;
        }

        this->roomId = rsp.roomid();
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
            emit room->onRoomInfoFailed(status.error_code() == grpc::NOT_FOUND ? "room not found" : "room init error " + std::to_string(status.error_code()));
            return;
        }

        emit room->onRoomInfoSucceed(rsp);
    });
    CollabRoom::connect(worker, &QThread::finished, worker, &QThread::deleteLater);
    worker->setParent(room);
    worker->start();
}

void RoomServer::setSdp(const vts::server::Sdp &sdp) {
    auto context = getCtx();
    if (context == nullptr)
        return;

    vts::server::RspCommon rsp;
    auto status = service->SetSdp(context.get(), sdp, &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
        requestHasFailed = true;
    }
}

void RoomServer::setCandidate(const vts::server::Candidate& candidate) {
    auto context = getCtx();
    if (context == nullptr)
        return;

    vts::server::RspCommon rsp;
    auto status = service->SetCandidate(context.get(), candidate, &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
        requestHasFailed = true;
    }
}

void RoomServer::setNat(int type) {
    auto context = getCtx();
    if (context == nullptr)
        return;

    vts::server::ReqNatType req;
    req.set_nattype(type);

    vts::server::RspCommon rsp;
    auto status = service->SetNatType(context.get(), req, &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
        requestHasFailed = true;
    }
}

void RoomServer::setStat(const vts::server::ReqStat &stat) {
    auto context = getCtx();
    if (context == nullptr)
        return;

    vts::server::RspCommon rsp;
    auto status = service->SetStat(context.get(), stat, &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        heartbeatFailureCount++;
        if (heartbeatFailureCount >= 3) {
            requestHasFailed = true;
            emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
        }
    } else {
        heartbeatFailureCount = 0;
    }
}

void RoomServer::setNick(const string &nick) {
    auto context = getCtx();
    if (context == nullptr)
        return;

    vts::server::ReqNickname req;
    req.set_nick(nick);

    vts::server::RspCommon rsp;
    auto status = service->SetNickName(context.get(), req, &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
        requestHasFailed = true;
    }
}

void RoomServer::setTurn(const string &turn) {
    auto context = getCtx();
    if (context == nullptr)
        return;

    vts::server::TurnInfo t;
    t.set_turn(turn);

    vts::server::RspCommon rsp;
    auto status = service->SetTurn(context.get(), t, &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
        requestHasFailed = true;
    }
}

void RoomServer::setFrameFormat(const vts::server::FrameFormatSetting &format) {
    auto context = getCtx();
    if (context == nullptr)
        return;

    vts::server::RspCommon rsp;
    auto status = service->SetFrameFormat(context.get(), format, &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
        requestHasFailed = true;
    }
}

void RoomServer::setShareInfo(const std::string& gpu, const std::string& capture, bool start) {
    auto context = getCtx();
    if (context == nullptr)
        return;

    vts::server::ReqShareInfo req;
    req.set_gpu(gpu);
    req.set_capture(capture);
    req.set_start(start);
    req.set_iswireless(isWireless());
    req.set_is2g4(is2G4Wireless());

    vts::server::RspCommon rsp;
    auto status = service->SetShareInfo(context.get(), req, &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
        requestHasFailed = true;
    }
}

void RoomServer::exit() {
    auto context = getCtx();
    if (context == nullptr)
        return;

    vts::server::RspCommon rsp;
    auto status = service->Exit(context.get(), vts::server::ReqCommon(), &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
    }
}

void RoomServer::requestIdr(const std::string& reason, const std::string& peer) {
    auto now = std::chrono::system_clock::now();
    if (lastRequestIdr[peer] + std::chrono::seconds(2) > now) {
        return;
    }
    lastRequestIdr[peer] = now;
    qDebug() << "request idr";

    auto context = getCtx();
    if (context == nullptr)
        return;

    vts::server::ReqIdr req;
    req.set_reason(reason);
    req.set_timestamp(QDateTime::currentMSecsSinceEpoch());
    req.set_peerid(peer);

    vts::server::RspCommon rsp;
    auto status = service->RequestIdr(context.get(), req, &rsp);
    if (!status.ok()) {
        qDebug() << __FUNCTION__ << "failed:" << status.error_message().c_str();
        emit room->onRoomServerError(__FUNCTION__, status.error_message().c_str());
        requestHasFailed = true;
    }
}
