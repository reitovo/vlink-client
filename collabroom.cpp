#include "collabroom.h"
#include "ndi2av.h"
#include "peeritemwidget.h"
#include "qjsonarray.h"
#include "qjsondocument.h"
#include "qthread.h"
#include "qtimer.h"
#include "ui_collabroom.h"
#include "vtslink.h"

#include <QSettings>
#include <QSysInfo>
#include <QUuid>
#include <QMessageBox>

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>
}

CollabRoom::CollabRoom(QString roomId, bool isServer, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CollabRoom)
{
    ui->setupUi(this);
    this->roomId = roomId;
    this->isServer = isServer;
    this->peerId = QSysInfo::machineUniqueId();
    qDebug() << "Role is" << (isServer ? "server" : "client");

    QSettings settings;
    if (isServer) {
        turnServer = settings.value("turnServer", QString()).toString();
        qDebug() << "Turn server" << turnServer;
    }
    peerId = settings.value("peerId", QString()).toString();
    if (peerId.isEmpty()) {
        peerId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        qDebug() << "Generate new peer id" << peerId;
        settings.setValue("peerId", this->peerId);
        settings.sync();
    }
    peerId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    connectWebsocket();
    connect(this, &CollabRoom::onReconnectWebsocket, this, &CollabRoom::connectWebsocket);
    connect(this, &CollabRoom::onUpdatePeersUi, this, &CollabRoom::updatePeersUi);
    connect(this, &CollabRoom::onRoomExit, this, &CollabRoom::exitRoom);
    connect(this, &CollabRoom::onNdiToFfmpegError, this, &CollabRoom::handleNdiToFfmpegError);
    connect(ui->ndiSourceSelect, &QComboBox::currentIndexChanged, this, &CollabRoom::updateNdiSourceIndex);
    connect(ui->btnSharingStatus, &QPushButton::clicked, this, &CollabRoom::switchNdiStatus);
    connect(ui->btnSetNick, &QPushButton::clicked, this, &CollabRoom::setNick);

    // Not required, but "correct" (see the SDK documentation).
    if (!NDIlib_initialize()) {
        QMessageBox::critical(this, tr("致命错误"), tr("初始化 NDI 组件失败"));
        QApplication::quit();
    }

    // Start finding
    connect(this, &CollabRoom::onNdiSourcesUpdated, this, &CollabRoom::updateNdiSourcesUi);
    ndiFindThread = std::unique_ptr<QThread>(QThread::create([this]() {
        // We are going to create an NDI finder that locates sources on the network.
        NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2();
        if (!pNDI_find) {
            QMessageBox::critical(this, tr("致命错误"), tr("启动 NDI 发现组件失败"));
            return;
        }

        while(!exiting) {
            // Wait up till 250ms to check for new sources to be added or removed
            if (!NDIlib_find_wait_for_sources(pNDI_find, 250 /* milliseconds */)) {
                continue;
            }

            // Get the updated list of sources
            uint32_t no_sources = 0;
            ndiSources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);
            ndiSourceCount = no_sources;

            // Display all the sources.
            QStringList sources;
            printf("Network sources (%u found).\n", no_sources);
            for (uint32_t i = 0; i < no_sources; i++) {
                printf("%u. %s %s\n", i + 1, ndiSources[i].p_ndi_name, ndiSources[i].p_url_address);
                sources.append(QString("%1").arg(ndiSources[i].p_ndi_name));
            }
            emit onNdiSourcesUpdated(sources);
        }

        // Destroy the NDI finder
        NDIlib_find_destroy(pNDI_find);
    }));
    ndiFindThread->start();
}

CollabRoom::~CollabRoom()
{
    delete ui;

    exiting = true;

    stopNdiToFfmpegWorker();

    // Stop ndi finder
    if (ndiFindThread != nullptr && !ndiFindThread->isFinished() && !ndiFindThread->wait(500)) {
        qWarning() << "uneasy to exit ndi find thread";
        ndiFindThread->terminate();
        ndiFindThread->wait(500);
        ndiFindThread = nullptr;
    }

    if (ws != nullptr) {
        ws->resetCallbacks();
        ws->forceClose();
        ws = nullptr;
    }

    if (isServer) {
        for (auto& a : servers) {
            if (a != nullptr) {
                a->close();
                a = nullptr;
            }
        }
        servers.clear();
    } else {
        if (client != nullptr)
            client->close();
        client = nullptr;
    }
    NDIlib_destroy();

    qWarning() << "room exit";
}

void CollabRoom::updatePeersUi(QList<PeerUi> peerUis)
{
    qDebug() << "update peer ui";

    while (ui->peerList->count() < peerUis.count()) {
        auto item = new QListWidgetItem(ui->peerList);
        auto peer = new PeerItemWidget(this);
        item->setSizeHint(peer->sizeHint());
        ui->peerList->addItem(item);
        ui->peerList->setItemWidget(item, peer);
    }

    while (ui->peerList->count() > peerUis.count()) {
        auto item = ui->peerList->item(0);
        auto widget = ui->peerList->itemWidget(item);
        ui->peerList->removeItemWidget(item);
        delete item;
        delete widget;
    }

    auto idx = 0;
    for (auto& p : peerUis) {
        auto item = ui->peerList->item(idx++);
        auto widget = reinterpret_cast<PeerItemWidget*>(ui->peerList->itemWidget(item));
        widget->setPeerUi(p, p.id == peerId);
    }
}

void CollabRoom::updateNdiSourcesUi(QStringList list)
{
    while (ui->ndiSourceSelect->count() < list.count()) {
        ui->ndiSourceSelect->addItem("");
    }

    while (ui->ndiSourceSelect->count() > list.count()) {
        ui->ndiSourceSelect->removeItem(0);
    }

    for (int i = 0; i < list.count(); ++i) {
        ui->ndiSourceSelect->setItemText(i, list[i]);
    }
}

void CollabRoom::updateNdiSourceIndex(int idx)
{
    qDebug() << "Set current ndi source index to" << idx;
    if (idx != -1) {
        ndiSourceIdx = idx;
    }
}

void CollabRoom::exitRoom(QString reason)
{
    QString error = reason;
    if (reason == "host leave") {
        error = tr("房主已离开");
    } else if (reason == "join frame missing") {
        error = tr("协议错误");
    } else if (reason == "room id format error") {
        error = tr("房间号错误");
    }

    QMessageBox::critical(this, tr("断开连接"), error);
    close();
}

void CollabRoom::setNick()
{
    auto n = ui->nick->text();
    if (n.length() > 16) {
        n = n.left(16);
        ui->nick->setText(n);
    }
    qDebug() << "new nick" << n;
    if (ws != nullptr && ws->isOpen()) {
        auto t = QThread::create([=]() {
            QJsonObject dto;
            dto["msg"] = n;
            dto["type"] = "nick";
            QJsonDocument doc(dto);

            auto content = QString::fromUtf8(doc.toJson()).toStdString();
            std::cout << content << std::endl;

            ws->send(content);
        });
        connect(t, &QThread::finished, t, &QThread::deleteLater);
        t->start();
    }
}

void CollabRoom::handleNdiToFfmpegError(QString reason)
{
    stopNdiToFfmpegWorker();

    QString err = reason;
    if (reason == "init error") {
        err = tr("初始化错误");
    } else if (reason == "source error") {
        err = tr("NDI 来源错误");
    } else if (reason == "frame error") {
        err = tr("NDI 接收断开");
    } else if (reason == "frame format error") {
        err = tr("NDI 帧格式错误(Frame)，请确认选择了 VTuber Studio 生成的来源（包含Live2D Camera字样）");
    } else if (reason == "frame size error") {
        err = tr("NDI 分辨率错误，请在 VTuber Studio 设置中开启「NDI 输出分辨率」，并设置大小为「1920 X 1080」");
    } else if (reason == "frame change error") {
        err = tr("NDI 输出源格式发生变化，请不要在分享画面时更改 VTuber Studio 中的 NDI 设置");
    } else if (reason == "line stride error") {
        err = tr("NDI 帧格式错误(Stride)，请确认选择了 VTuber Studio 生成的来源（包含Live2D Camera字样）");
    }

    QMessageBox::critical(this, tr("NDI 错误"), err);

    ui->btnSharingStatus->setText(tr("开始") + tr("分享 VTuber Studio 画面"));
    ui->btnSharingStatus->setEnabled(true);
    ui->ndiSourceSelect->setEnabled(true);
}

void CollabRoom::switchNdiStatus()
{
    if (!ndiToFfmpegRunning) {
        ui->btnSharingStatus->setText(tr("停止") + tr("分享 VTuber Studio 画面"));
        ui->btnSharingStatus->setEnabled(false);
        ui->ndiSourceSelect->setEnabled(false);
        QTimer::singleShot(500, this, [this]() {
            ui->btnSharingStatus->setEnabled(true);
        });

        // Start worker
        ndiToFfmpegRunning = true;
        ndiToFfmpegThread = std::unique_ptr<QThread>(QThread::create([=]() {
            ndiToFfmpegWorker();
        }));
        ndiToFfmpegThread->start();
    } else {
        ui->btnSharingStatus->setText(tr("开始") + tr("分享 VTuber Studio 画面"));
        ui->btnSharingStatus->setEnabled(false);

        stopNdiToFfmpegWorker();

        QTimer::singleShot(500, this, [this]() {
            ui->btnSharingStatus->setEnabled(true);
            ui->ndiSourceSelect->setEnabled(true);
        });
    }
}

void CollabRoom::connectWebsocket()
{
    qDebug("Connect to reito server websocket");
    ws = std::make_unique<rtc::WebSocket>();

    ws->onOpen([=]() {
        qDebug() << "Websocket open";
        {
            QJsonObject join;
            join["roomId"] = roomId;
            join["peerId"] = peerId;
            join["isServer"] = isServer;
            QJsonObject dto;
            dto["join"] = join;
            dto["type"] = "join";
            QJsonDocument doc(dto);

            qDebug("Send join frame");
            ws->send(doc.toJson().toStdString());
        }

        auto t = QThread::create([=]() {
            qDebug() << "Start nat type determine";
            CNatProb natProb;
            if (!natProb.Init("stun.qq.com")) {
                cout<<"natProb init failed."<<endl;
            }
            NatType type = natProb.GetNatType();
            qDebug() << "nat type" << QString(natProb.DescribeNatType(type).c_str());

            QJsonObject dto;
            dto["nat"] = type;
            dto["type"] = "nat";
            QJsonDocument doc(dto);

            auto content = QString::fromUtf8(doc.toJson()).toStdString();
            std::cout << content << std::endl;

            ws->send(content);
        });
        connect(t, &QThread::finished, t, &QThread::deleteLater);
        t->start();
    });

    ws->onClosed([=]() {
        qDebug("Websocket close");
        if (exiting)
            return;

        QThread::sleep(1);
        emit onReconnectWebsocket();
    });

    ws->onMessage([=](std::variant<rtc::binary, rtc::string> message) {
        if (std::holds_alternative<rtc::string>(message)) {
            auto msg = std::get<rtc::string>(message);
            std::cout << "WebSocket received: " << msg << std::endl;
            auto frame = QJsonDocument::fromJson(QByteArray(msg.data(), msg.size()));
            auto type = frame["type"].toString();
            if (type == "peers") {
                auto peers = frame["peers"].toArray();
                updatePeers(peers);
            } else if (type == "turn") {

            } else if (type == "bye")  {
                exiting = true;
                emit onRoomExit("host leave");
            } else if (type == "error") {
                auto err = frame["msg"].toString();
                exiting = true;
                emit onRoomExit(err);
            }
        }
    });

    ws->open(VTSLINK_WS_BASEURL "/api/room/join");
}

void CollabRoom::updatePeers(QJsonArray peers)
{
    if (isServer) {
        QList<QString> alive;
        for (auto peer : peers) {
            auto p = peer.toObject();
            auto svr = p["isServer"].toBool();
            // Find clients
            if (!svr) {
                auto id = p["id"].toString();
                alive.append(id);
                if (!servers.contains(id)) {
                    qDebug() << "create peer" << id;
                    auto server = std::make_shared<Peer>(this, id);
                    server->startServer();
                    servers[id] = std::move(server);
                }

                auto sdps = p["sdps"].toObject();
                if (sdps.contains("server")) {
                    qDebug() << "client offered sdp to us";
                    auto clientSdp = sdps["server"].toObject();
                    servers[id]->pc->setRemoteDescription(rtc::Description(clientSdp["sdp"].toString().toStdString(), clientSdp["type"].toString().toStdString()));
                }
            }
        }
        // If one leaves, remove it.
        QList<QString> remove;
        for (auto s : servers.asKeyValueRange()) {
            if (!alive.contains(s.first)) {
                remove.append(s.first);
            }
        }
        for(auto& r : remove) {
            servers.remove(r);
        }
    } else {
        for (auto peer : peers) {
            auto p = peer.toObject();
            auto svr = p["isServer"].toBool();
            // Find server
            if (svr) {
                auto sdps = p["sdps"].toObject();
                // If server has offered the remote description to us
                if (sdps.contains(peerId)) {
                    qDebug() << "server offered sdp to us";
                    auto sdp = sdps[peerId].toObject();
                    auto time = QDateTime::fromMSecsSinceEpoch(sdp["time"].toInteger());
                    // If the turn server changed or client not started
                    if (client == nullptr || client->sdpTime < time) {
                        // We need to reset the connection
                        client = std::make_shared<Peer>(this, peerId);
                        client->sdpTime = time;
                        client->startClient(sdp);
                    }
                }
                break;
            }
        }
        // We do not need to handle if the server leave, because the whole room will be destroyed.
    }

    // Update UI
    QList<PeerUi> peerUis;
    for (auto peer : peers) {
        PeerUi u;
        auto p = peer.toObject();
        u.id = p["id"].toString();
        u.nat = (NatType) p["nat"].toInt();
        u.nick = p["nick"].toString();
        u.rtt = p["rtt"].toInteger();
        peerUis.append(u);
    }

    emit onUpdatePeersUi(peerUis);
}

void CollabRoom::ndiToFfmpegWorker()
{
    if (ndiSourceIdx < 0 || ndiSourceIdx >= ndiSourceCount) {
        emit onNdiToFfmpegError("source error");
        return;
    }

    // We now have at least one source, so we create a receiver to look at it.
    NDIlib_recv_instance_t pNDI_recv = NDIlib_recv_create_v3();
    if (!pNDI_recv) {
        emit onNdiToFfmpegError("init error");
        return;
    }

    // Connect to our sources
    NDIlib_recv_connect(pNDI_recv, ndiSources + ndiSourceIdx);

    // ffmpeg coverter
    ndi2av cvt;

    while (ndiToFfmpegRunning) {
        // The descriptors
        NDIlib_video_frame_v2_t video_frame;
        NDIlib_audio_frame_v2_t audio_frame;
        char cc[5] = {0};

        switch (NDIlib_recv_capture_v2(pNDI_recv, &video_frame, nullptr, nullptr, 100)) {
        // No data
        //        case NDIlib_frame_type_none:
        //            printf("No data received.\n");
        //            break;

        // Video data
        case NDIlib_frame_type_video: {
            if (!cvt.isInited()) {
                if (video_frame.xres != 1920 || video_frame.yres != 1080) {
                    emit onNdiToFfmpegError("frame size error");
                    ndiToFfmpegRunning = false;
                }
                if (video_frame.FourCC != NDIlib_FourCC_video_type_BGRA || video_frame.frame_format_type != NDIlib_frame_format_type_progressive) {
                    emit onNdiToFfmpegError("frame format error");
                    ndiToFfmpegRunning = false;
                }
                if (video_frame.line_stride_in_bytes != video_frame.xres * 4) {
                    emit onNdiToFfmpegError("line stride error");
                    ndiToFfmpegRunning = false;
                }
                auto err0 = cvt.init(video_frame.xres, video_frame.yres, video_frame.frame_rate_D, video_frame.frame_rate_N, video_frame.frame_format_type, video_frame.FourCC);
                if (err0.has_value()) {
                    emit onNdiToFfmpegError(err0.value());
                    ndiToFfmpegRunning = false;
                }
                if (!ndiToFfmpegRunning)
                    goto clean;

                cvt.setOnPacketReceived([](uint8_t* data, int len) {
                    qDebug() << "data" << len;
                });
            }

            *(uint32_t*)cc = video_frame.FourCC;
            qDebug() << "video" << video_frame.xres <<  video_frame.yres << video_frame.frame_rate_D << video_frame.frame_rate_N << video_frame.frame_format_type << cc;

            auto e = cvt.process(&video_frame);
            if (e.has_value()) {
                emit onNdiToFfmpegError(e.value());
                ndiToFfmpegRunning = false;
                goto clean;
            }

            NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);
            break;
        }

            // Audio data
            //        case NDIlib_frame_type_audio:
            //            printf("Audio data received (%d samples).\n", audio_frame.no_samples);
            //            NDIlib_recv_free_audio_v2(pNDI_recv, &audio_frame);
            //            break;

        case NDIlib_frame_type_error:
            emit onNdiToFfmpegError("frame error");
            ndiToFfmpegRunning = false;
            goto clean;
        case NDIlib_frame_type_none:
        case NDIlib_frame_type_audio:
        case NDIlib_frame_type_metadata:
        case NDIlib_frame_type_status_change:
        case NDIlib_frame_type_max:
            break;
        }
    }

clean:
    cvt.stop();
    // Destroy the receiver
    qDebug() << "ndi to ffmpeg exit...";
    NDIlib_recv_destroy(pNDI_recv);
}

void CollabRoom::stopNdiToFfmpegWorker()
{
    ndiToFfmpegRunning = false;
    if (ndiToFfmpegThread != nullptr && !ndiToFfmpegThread->isFinished() && !ndiToFfmpegThread->wait(500)) {
        qWarning() << "uneasy to exit ndi convert thread";
        ndiToFfmpegThread->terminate();
        ndiToFfmpegThread->wait(500);
        ndiToFfmpegThread = nullptr;
    }
}
