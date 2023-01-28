#include "collabroom.h"
#include "NatTypeProbe/NatProb.h"
#include "frame_to_av.h"
#include "frame_to_d3d.h"
#include "ui/widgets/peeritemwidget.h"
#include "qjsonarray.h"
#include "qjsondocument.h"
#include "qthread.h"
#include "qtimer.h"
#include "ui/windows/settingwindow.h"
#include "ui_collabroom.h"
#include "core/vtslink.h"

#include <QSettings>
#include <QSysInfo>
#include <QUuid>
#include <QMessageBox>
#include <QClipboard>
#include "mainwindow.h"

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>
}

#include <QSystemTrayIcon>
#include "d3d_to_frame.h"
#include "core/usage.h"
#include "dxgioutput.h"
#include "buyrelay.h"
#include "d3d_capture.h"
#include <QDesktopServices>

static CollabRoom* _instance;

CollabRoom::CollabRoom(QString roomId, bool isServer, QWidget *parent) :
        QDialog(parent),
        ui(new Ui::CollabRoom) {

    ui->setupUi(this);

    QPalette palette;
    QBrush brush(QColor(255, 124, 159));
    brush.setStyle(Qt::SolidPattern);
    palette.setBrush(QPalette::Active, QPalette::ButtonText, brush);
    palette.setBrush(QPalette::Inactive, QPalette::ButtonText, brush);
    palette.setBrush(QPalette::Disabled, QPalette::ButtonText, brush);
    ui->copyRoomId->setPalette(palette);
    ui->btnSharingStatus->setPalette(palette);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);

    QSettings settings;
    useNdiSender = settings.value("useNdiSender", false).toBool();
    useNdiReceiver = settings.value("useNdiReceiver", false).toBool();

    this->roomId = roomId;
    this->isServer = isServer;

    d3d = std::make_shared<DxToFrame>();
    if (!useNdiSender) {
        auto output = new DxgiOutput();
        if (settings.value("showDxgiWindow").toBool()) {
            output->show();
        }
        d3d->init(true);
    } else {
        d3d->init(false);
    }

    qDebug() << "sender is" << (useNdiSender ? "ndi" : "swap");
//    peerId = settings.value("peerId", QString()).toString();
//    if (peerId.isEmpty()) {
//        peerId = QUuid::createUuid().toString(QUuid::WithoutBraces);
//        qDebug() << "Generate new peer id" << peerId;
//        settings.setValue("peerId", this->peerId);
//        settings.sync();
//    }
    peerId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    auto role = isServer ? "Server" : "Client";
    qDebug() << "Role is" << (role) << peerId;

    if (isServer) {
        turnServer = settings.value("turnServer", QString()).toString();
        ui->relayInput->setText(turnServer);
        qDebug() << "Turn server" << turnServer;
    }

    this->setWindowTitle(tr("VTube Studio 联动"));

    connectWebsocket();

    connect(this, &CollabRoom::onReconnectWebsocket, this, &CollabRoom::connectWebsocket);
    connect(this, &CollabRoom::onUpdatePeersUi, this, &CollabRoom::updatePeersUi);
    connect(this, &CollabRoom::onRoomExit, this, &CollabRoom::exitRoom);
    connect(this, &CollabRoom::onShareError, this, &CollabRoom::shareError);
    connect(this, &CollabRoom::onFatalError, this, &CollabRoom::fatalError);
    connect(this, &CollabRoom::onRtcFailed, this, &CollabRoom::rtcFailed);
    connect(this, &CollabRoom::onDowngradedToSharedMemory, this, &CollabRoom::downgradedToSharedMemory);

    connect(ui->btnSharingStatus, &QPushButton::clicked, this, &CollabRoom::toggleShare);
    connect(ui->btnSetNick, &QPushButton::clicked, this, &CollabRoom::setNick);
    connect(ui->copyRoomId, &QPushButton::clicked, this, &CollabRoom::copyRoomId);
    connect(ui->updateTurnServer, &QPushButton::clicked, this, &CollabRoom::updateTurnServer);
    connect(ui->relayHideShow, &QPushButton::clicked, this, &CollabRoom::toggleTurnVisible);
    connect(ui->openSettings, &QPushButton::clicked, this, &CollabRoom::openSetting);
    connect(ui->createRelay, &QPushButton::clicked, this, &CollabRoom::openBuyRelay);
    connect(ui->keepTop, &QPushButton::clicked, this, &CollabRoom::toggleKeepTop);

    connect(ui->tutorialFaq, &QPushButton::clicked, this, [=]() {
        QDesktopServices::openUrl(QUrl("https://www.wolai.com/reito/nhenjFvkw5gDNM4tikEw5V"));
    });
    connect(ui->knowWhenRelay, &QPushButton::clicked, this, [=]() {
        QDesktopServices::openUrl(QUrl("https://www.wolai.com/reito/7R2Z4gPzcZSvPVRUy9fkrP"));
    });
    connect(ui->knowDeployRelay, &QPushButton::clicked, this, [=]() {
        QDesktopServices::openUrl(QUrl("https://www.wolai.com/reito/osFxEHHuiZNF3JMrhS6zV2"));
    });

    // Not required, but "correct" (see the SDK documentation).
    if (!NDIlib_initialize()) {
        fatalError(tr("初始化 NDI 组件失败"));
        QApplication::quit();
    }

    // Start finding NDI
    connect(this, &CollabRoom::onNdiSourcesUpdated, this, &CollabRoom::updateNdiSourcesUi);
    ndiFindThread = std::unique_ptr<QThread>(QThread::create([this]() {
        ndiFindWorker();
    }));
    ndiFindThread->start();

    // Start sending thread
    frameSendThread = std::unique_ptr<QThread>(QThread::create([=]() {
        if (useNdiSender) {
            ndiSendWorker();
        } else {
            dxgiSendWorker();
        }
    }));
    frameSendThread->start();

    // If server, start sending heartbeat, and rtt update
    if (isServer) {
        heartbeat = std::make_unique<QTimer>(this);
        connect(heartbeat.get(), &QTimer::timeout, this, [this]() {
            heartbeatUpdate();
        });
        heartbeat->start(20000);
    }

    usageStat = std::make_unique<QTimer>(this);
    connect(usageStat.get(), &QTimer::timeout, this, [this]() {
        usageStatUpdate();
    });
    usageStat->start(1000);

    if (!isServer) {
        resize(QSize(381, 360));
    } else {
        resize(QSize(730, 360));
    }

    _instance = this;
}

CollabRoom::~CollabRoom() {
    _instance = nullptr;
    exiting = true;

    auto dxgi = DxgiOutput::getWindow();
    if (dxgi) {
        dxgi->deleteLater();
    }

    usageStat.reset();
    heartbeat.reset();

    stopShareWorker();

    // Stop ndi finder
    if (ndiFindThread != nullptr && !ndiFindThread->isFinished() && !ndiFindThread->wait(500)) {
        qWarning() << "uneasy to exit ndi find thread";
        ndiFindThread->terminate();
        ndiFindThread->wait(500);
        ndiFindThread = nullptr;
    }

    if (frameSendThread != nullptr && !frameSendThread->isFinished() && !frameSendThread->wait(500)) {
        qWarning() << "uneasy to exit frame send thread";
        frameSendThread->terminate();
        frameSendThread->wait(500);
        frameSendThread = nullptr;
    }

    wsLock.lock();
    if (ws != nullptr) {
        ws->resetCallbacks();
        ws->forceClose();
        ws = nullptr;
    }
    wsLock.unlock();

    peersLock.lock();
    if (isServer) {
        for (auto &a: servers) {
            if (a.second != nullptr) {
                a.second->close();
                a.second = nullptr;
            }
        }
        servers.clear();
    } else {
        if (client != nullptr)
            client->close();
        client = nullptr;
    }
    peersLock.unlock();

    NDIlib_destroy();

    delete ui;
    qWarning() << "room exit";
}

QString CollabRoom::debugInfo() {
    auto ret = QString("Room Role: %1 Id: %2\nPeer Nick: %4 Id: %3\n%5 %6\n%7 %8")
            .arg(isServer ? "Server" : "Client").arg(roomId)
            .arg(peerId).arg(ui->nick->text())
            .arg(useNdiSender ? "Dx->Frame (D3D11 Map) " : "Dx->Frame (D3D11 Present) ")
            .arg(sendProcessFps.stat());
    if (useNdiReceiver) {
        ret = ret.arg(isServer ? "Frame->Dx (NDI Receive) " : "Frame->Av (NDI Receive) ")
                .arg(shareRecvFps.stat());
    } else {
        ret = ret.arg(isServer ? "Frame->Dx (D3D11 CapTick) " : "Frame->Av (D3D11 Receive) ")
                .arg(shareRecvFps.stat());
    }
    return ret;
}

void CollabRoom::updatePeersUi(QList<PeerUi> peerUis) {
    qDebug() << "update peer ui";

    while (ui->peerList->count() < peerUis.count()) {
        auto item = new QListWidgetItem(ui->peerList);
        auto peer = new PeerItemWidget(this, this);
        item->setSizeHint(QSize(0, 32));
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
    for (auto &p: peerUis) {
        auto item = ui->peerList->item(idx++);
        auto widget = reinterpret_cast<PeerItemWidget *>(ui->peerList->itemWidget(item));
        widget->setPeerUi(p);
    }
}

void CollabRoom::updateNdiSourcesUi(QStringList list) {
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

void CollabRoom::copyRoomId() {
    auto cb = QApplication::clipboard();
    cb->setText(roomId);
    MainWindow::instance()->tray->showMessage(tr("复制成功"),
                                              tr("请不要在直播画面中展示房间ID！\n已复制到剪贴板，快分享给参加联动的人吧~"),
                                              MainWindow::instance()->tray->icon());
}

void CollabRoom::exitRoom(QString reason) {
    QString error = reason;
    if (reason == "host leave") {
        error = tr("房主已离开");
    } else if (reason == "join frame missing") {
        error = tr("协议错误");
    } else if (reason == "room id format error") {
        error = tr("房间号错误");
    } else if (reason == "room not found") {
        error = tr("房间不存在");
    }

    stopShare();
    QMessageBox::critical(this, tr("断开连接"), error);
    close();
}

void CollabRoom::setNick() {
    auto n = ui->nick->text();
    if (n.length() > 16) {
        n = n.left(16);
        ui->nick->setText(n);
    }
    qDebug() << "new nick" << n;

    QJsonObject dto;
    dto["msg"] = n;
    dto["type"] = "nick";
    QJsonDocument doc(dto);

    auto content = QString::fromUtf8(doc.toJson());
    qDebug() << content;

    wsSendAsync(content.toStdString());
}

void CollabRoom::updateTurnServer() {
    auto ipt = ui->relayInput->text();
    turnServer = ipt;

    QSettings settings;
    settings.setValue("turnServer", turnServer);
    qDebug() << "update turn server" << turnServer;

    peersLock.lock();
    servers.clear();
    peersLock.unlock();

    MainWindow::instance()->tray->showMessage(tr("设置成功"), tr("所有联动人将重新连接，请稍后"),
                                              MainWindow::instance()->tray->icon());

    QJsonObject dto;
    dto["type"] = "get";
    QJsonDocument doc(dto);
    auto content = QString::fromUtf8(doc.toJson());
    qDebug() << content;

    wsSendAsync(content.toStdString());
}

void CollabRoom::toggleTurnVisible() {
    if (ui->relayInput->echoMode() == QLineEdit::Normal) {
        ui->relayInput->setEchoMode(QLineEdit::Password);
        ui->relayHideShow->setIcon(QIcon(":/images/show.png"));
    } else if (ui->relayInput->echoMode() == QLineEdit::Password) {
        ui->relayInput->setEchoMode(QLineEdit::Normal);
        ui->relayHideShow->setIcon(QIcon(":/images/hide.png"));
    }
}

void CollabRoom::shareError(QString reason) {
    stopShareWorker();

    QMessageBox::critical(this, tr("分享错误"), errorToReadable(reason));

    ui->btnSharingStatus->setText(tr("开始") + tr("分享 VTube Studio 画面"));
    ui->btnSharingStatus->setEnabled(true);
    ui->ndiSourceSelect->setEnabled(true);
}

void CollabRoom::fatalError(QString reason) {
    QMessageBox::critical(this, tr("致命错误"), errorToReadable(reason));
    close();
}

void CollabRoom::openSetting() {
    auto w = new SettingWindow(this);
    w->show();
}

void CollabRoom::toggleShare() {
    if (!shareRunning) {
        startShare();
    } else {
        stopShare();
    }
}

void CollabRoom::startShare() {
    if (!isServer) {
        peersLock.lock();
        if (client == nullptr || !client->connected()) {
            peersLock.unlock();
            emit onShareError(tr("尚未成功连接服务器，无法开始分享"));
            return;
        }
        peersLock.unlock();
    }

    ui->btnSharingStatus->setText(tr("停止") + tr("分享 VTube Studio 画面"));
    ui->btnSharingStatus->setEnabled(false);
    ui->ndiSourceSelect->setEnabled(false);
    QTimer::singleShot(500, this, [this]() {
        ui->btnSharingStatus->setEnabled(true);
    });

    // Start worker
    shareRunning = true;
    if (isServer) {
        shareThread = std::unique_ptr<QThread>(QThread::create([=]() {
            if (useNdiReceiver) {
                ndiShareWorkerServer();
            } else {
                dxgiShareWorkerServer();
            }
        }));
    } else {
        shareThread = std::unique_ptr<QThread>(QThread::create([=]() {
            if (useNdiReceiver) {
                ndiShareWorkerClient();
            } else {
                dxgiShareWorkerClient();
            }
        }));
    }
    shareThread->start();
}

void CollabRoom::stopShare() {
    ui->btnSharingStatus->setText(tr("开始") + tr("分享 VTube Studio 画面"));
    ui->btnSharingStatus->setEnabled(false);

    stopShareWorker();

    QTimer::singleShot(500, this, [this]() {
        ui->btnSharingStatus->setEnabled(true);
        ui->ndiSourceSelect->setEnabled(true);
    });
}

void CollabRoom::ndiShareWorkerClient() {
    qInfo() << "ndi to ffmpeg client start";

    const NDIlib_source_t *source = nullptr;
    auto name = ui->ndiSourceSelect->currentText();
    for (auto i = 0; i < ndiSourceCount; ++i) {
        if (name == ndiSources[i].p_ndi_name) {
            source = &ndiSources[i];
            break;
        }
    }

    if (source == nullptr) {
        emit onShareError("source error");
        return;
    }

    qDebug() << "ndi to ffmpeg ndi2av";
    // ffmpeg coverter
    FrameToAv cvt([=](auto av) {
        peersLock.lock();
        if (client != nullptr)
            client->sendAsync(std::move(av));
        peersLock.unlock();
    });

    auto initErr = cvt.init(VTSLINK_FRAME_WIDTH, VTSLINK_FRAME_HEIGHT, VTSLINK_FRAME_D, VTSLINK_FRAME_N, false);
    if (initErr.has_value()) {
        emit onShareError(initErr.value());
        shareRunning = false;
    }

    auto useUYVA = cvt.useUYVA();
    auto fmt = useUYVA ? NDIlib_recv_color_format_fastest : NDIlib_recv_color_format_BGRX_BGRA;

    NDIlib_recv_create_v3_t create;
    create.color_format = fmt;
    create.bandwidth = NDIlib_recv_bandwidth_highest;
    create.allow_video_fields = false;

    // We now have at least one source, so we create a receiver to look at it.
    NDIlib_recv_instance_t pNDI_recv = NDIlib_recv_create_v3(&create);
    if (!pNDI_recv) {
        emit onShareError("init error");
        shareRunning = false;
        return;
    }

    // Connect to our sources
    NDIlib_recv_connect(pNDI_recv, source);

    int frameD = VTSLINK_FRAME_D;
    int frameN = VTSLINK_FRAME_N;
    int64_t frameCount = 0;
    int64_t startTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    while (shareRunning) {
        // The descriptors
        NDIlib_video_frame_v2_t video_frame;
        NDIlib_audio_frame_v2_t audio_frame;
        char cc[5] = {0};

        QElapsedTimer t;
        t.start();

        switch (NDIlib_recv_capture_v2(pNDI_recv, &video_frame, nullptr, nullptr, 100)) {
            // No data
            //        case NDIlib_frame_type_none:
            //            printf("No data received.\n");
            //            break;

            // Video data
            case NDIlib_frame_type_video: {
//            *(uint32_t*)cc = video_frame.FourCC;
//            qDebug() << "video" << video_frame.xres <<  video_frame.yres << video_frame.frame_rate_D << video_frame.frame_rate_N << video_frame.frame_format_type << cc;

                shareRecvFps.add(t.nsecsElapsed());

                auto e = cvt.process(&video_frame);
                if (e.has_value()) {
                    emit onShareError(e.value());
                    shareRunning = false;
                    goto clean;
                }

                NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);

                frameCount++;
                int64_t frameTime = frameCount * 1000000.0 * frameD / frameN;
                int64_t nextTime = startTime + frameTime;
                int64_t currentTime = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                auto sleepTime = nextTime - currentTime;
                if (sleepTime > 0) {
                    QThread::usleep(sleepTime);
                }

                break;
            }

                // Audio data
                //        case NDIlib_frame_type_audio:
                //            printf("Audio data received (%d samples).\n", audio_frame.no_samples);
                //            NDIlib_recv_free_audio_v2(pNDI_recv, &audio_frame);
                //            break;

            case NDIlib_frame_type_error:
                emit onShareError("frame error");
                shareRunning = false;
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
    NDIlib_recv_destroy(pNDI_recv);

    qInfo() << "ndi to ffmpeg client exit";
}

void CollabRoom::ndiShareWorkerServer() {
    qInfo() << "ndi to ffmpeg server start";

    const NDIlib_source_t *source = nullptr;
    auto name = ui->ndiSourceSelect->currentText();
    for (auto i = 0; i < ndiSourceCount; ++i) {
        if (name == ndiSources[i].p_ndi_name) {
            source = &ndiSources[i];
            break;
        }
    }

    if (source == nullptr) {
        emit onShareError("source error");
        return;
    }

    FrameToDx cvt(d3d);
    if (!cvt.init()) {
        emit onShareError("ndi to dx init failed");
        return;
    }

    // We now have at least one source, so we create a receiver to look at it.
    NDIlib_recv_instance_t pNDI_recv = NDIlib_recv_create_v3();
    if (!pNDI_recv) {
        emit onShareError("init error");
        return;
    }

    // Connect to our sources
    NDIlib_recv_connect(pNDI_recv, source);

    int frameD = VTSLINK_FRAME_D;
    int frameN = VTSLINK_FRAME_N;
    int64_t frameCount = 0;
    int64_t startTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    while (shareRunning) {
        // The descriptors
        NDIlib_video_frame_v2_t video_frame;
        NDIlib_audio_frame_v2_t audio_frame;
        char cc[5] = {0};

        QElapsedTimer t;
        t.start();

        switch (NDIlib_recv_capture_v2(pNDI_recv, &video_frame, nullptr, nullptr, 100)) {
            // No data
            //        case NDIlib_frame_type_none:
            //            printf("No data received.\n");
            //            break;

            // Video data
            case NDIlib_frame_type_video: {
//            *(uint32_t*)cc = video_frame.FourCC;
//            qDebug() << "video" << video_frame.xres <<  video_frame.yres << video_frame.frame_rate_D << video_frame.frame_rate_N << video_frame.frame_format_type << cc;

                shareRecvFps.add(t.nsecsElapsed());

                cvt.update(&video_frame);

                NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);

                frameCount++;
                int64_t frameTime = frameCount * 1000000.0 * frameD / frameN;
                int64_t nextTime = startTime + frameTime;
                int64_t currentTime = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                auto sleepTime = nextTime - currentTime;
                if (sleepTime > 0) {
                    QThread::usleep(sleepTime);
                }

                break;
            }

                // Audio data
                //        case NDIlib_frame_type_audio:
                //            printf("Audio data received (%d samples).\n", audio_frame.no_samples);
                //            NDIlib_recv_free_audio_v2(pNDI_recv, &audio_frame);
                //            break;

            case NDIlib_frame_type_error:
                emit onShareError("frame error");
                shareRunning = false;
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
    // Destroy the receiver
    NDIlib_recv_destroy(pNDI_recv);

    qInfo() << "ndi to ffmpeg server exit";
}

void CollabRoom::dxgiShareWorkerClient() {
    qInfo() << "dx capture client start";

    // dx capture
    std::shared_ptr<DxCapture> dxCap = std::make_shared<DxCapture>();
    if (!dxCap->init()) {
        emit onShareError("dx capture init failed");
        return;
    }

    // ffmpeg coverter
    FrameToAv cvt([=](auto av) {
        peersLock.lock();
        if (client != nullptr)
            client->sendAsync(std::move(av));
        peersLock.unlock();
    });

    auto initErr = cvt.init(VTSLINK_FRAME_WIDTH, VTSLINK_FRAME_HEIGHT, VTSLINK_FRAME_D, VTSLINK_FRAME_N, false);
    if (initErr.has_value()) {
        emit onShareError(initErr.value());
        shareRunning = false;
        return;
    }

    auto useUYVA = cvt.useUYVA();
    if (useUYVA) {
        emit onShareError("cap no uyva");
        shareRunning = false;
        return;
    }

    int frameD = VTSLINK_FRAME_D;
    int frameN = VTSLINK_FRAME_N;
    int64_t frameCount = 0;
    int64_t startTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    float frameSeconds = 1.0f * frameD / frameN;

    while (shareRunning) {
        QElapsedTimer t;
        t.start();

        dxCap->captureTick(frameSeconds);

        auto e = cvt.processFast(dxCap);
        if (e.has_value()) {
            emit onShareError(e.value());
            shareRunning = false;
            goto clean;
        }

        shareRecvFps.add(t.nsecsElapsed());

        frameCount++;
        int64_t frameTime = frameCount * 1000000.0 * frameD / frameN;
        int64_t nextTime = startTime + frameTime;
        int64_t currentTime = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        auto sleepTime = nextTime - currentTime;
        if (sleepTime > 0) {
            QThread::usleep(sleepTime);
        }

    }

    clean:
    cvt.stop();

    qInfo() << "dx capture client exit";
}

void CollabRoom::dxgiShareWorkerServer() {
    qInfo() << "dx capture server start";

    // dx capture
    std::shared_ptr<DxCapture> dxCap = std::make_shared<DxCapture>(d3d);
    if (!dxCap->init()) {
        emit onShareError("dx capture init failed");
        return;
    }

    int frameD = VTSLINK_FRAME_D;
    int frameN = VTSLINK_FRAME_N;
    int64_t frameCount = 0;
    int64_t startTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    float frameSeconds = 1.0f * frameD / frameN;

    while (shareRunning) {

        QElapsedTimer t;
        t.start();

        dxCap->captureTick(frameSeconds);

        shareRecvFps.add(t.nsecsElapsed());

        frameCount++;
        int64_t frameTime = frameCount * 1000000.0 * frameD / frameN;
        int64_t nextTime = startTime + frameTime;
        int64_t currentTime = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        auto sleepTime = nextTime - currentTime;
        if (sleepTime > 0) {
            QThread::usleep(sleepTime);
        }
    }

    clean:
    qInfo() << "dx capture server exit";
}

void CollabRoom::stopShareWorker() {
    shareRunning = false;
    if (shareThread != nullptr && !shareThread->isFinished() && !shareThread->wait(500)) {
        qWarning() << "uneasy to exit share thread";
        shareThread->terminate();
        shareThread->wait(500);
        shareThread = nullptr;
    }
}

void CollabRoom::connectWebsocket() {
    qDebug("Connect to reito server websocket");
    ws = std::make_unique<rtc::WebSocket>();

    ws->onOpen([this]() {
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
            wsSendAsync(doc.toJson().toStdString());
        }

        auto t = QThread::create([=]() {
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

            QJsonObject dto;
            dto["nat"] = type;
            dto["type"] = "nat";
            QJsonDocument doc(dto);

            auto content = QString::fromUtf8(doc.toJson());
            qDebug() << content;

            wsSendAsync(content.toStdString());
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
            qDebug() << "WebSocket received: " << QString::fromStdString(msg);
            auto frame = QJsonDocument::fromJson(QByteArray(msg.data(), msg.size()));
            auto type = frame["type"].toString();
            if (type == "peers") {
                auto peers = frame["peers"].toArray();
                updatePeers(peers);
            } else if (type == "bye") {
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

void CollabRoom::updatePeers(QJsonArray peers) {
    if (isServer) {
        QList<QString> alive;
        for (auto peer: peers) {
            auto p = peer.toObject();
            auto svr = p["isServer"].toBool();
            // Find clients
            if (!svr) {
                auto id = p["id"].toString();
                alive.append(id);

                peersLock.lock();
                if (!servers.contains(id)) {
                    qDebug() << "create peer" << id;
                    auto server = std::make_unique<Peer>(this, id, QDateTime::currentDateTime());
                    server->startServer();
                    servers[id] = std::move(server);
                }
                peersLock.unlock();

                auto sdps = p["sdps"].toObject();
                if (sdps.contains("server")) {
                    auto clientSdp = sdps["server"].toObject();
                    auto ver = clientSdp["time"].toInteger();
                    if (ver == servers[id]->timeVersion().toMSecsSinceEpoch()) {
                        qDebug() << "client offered sdp to us";
                        servers[id]->setClientRemoteSdp(clientSdp);
                    }
                }
            }
        }
        peersLock.lock();
        // If one leaves, remove it.
        for (auto it = servers.begin(); it != servers.end();) {
            if (!alive.contains(it->first)) {
                it = servers.erase(it);
            } else {
                it++;
            }
        }

        // If one failed, remote it.
        for (auto it = servers.begin(); it != servers.end();) {
            if (it->second->failed()) {
                emit
                it = servers.erase(it);
            } else {
                it++;
            }
        }

        peersLock.unlock();
    } else {
        for (auto peer: peers) {
            auto p = peer.toObject();
            auto svr = p["isServer"].toBool();
            // Find server
            if (svr) {
                auto id = p["id"].toString();
                auto sdps = p["sdps"].toObject();
                // If server has offered the remote description to us
                if (sdps.contains(peerId)) {
                    auto sdp = sdps[peerId].toObject();
                    auto time = QDateTime::fromMSecsSinceEpoch(sdp["time"].toInteger());
                    // If the turn server changed or client not started
                    peersLock.lock();
                    if (client == nullptr || client->timeVersion() < time) {
                        qDebug() << "server offered sdp to us";
                        // We need to reset the connection

                        client = std::make_unique<Peer>(this, id, time);
                        client->startClient(sdp);
                    }
                    peersLock.unlock();
                }
                break;
            }
        }
        // We do not need to handle if the server leave, because the whole room will be destroyed.
    }

    // Update UI and calculate nat
    QStringList badNatList;
    QList<PeerUi> peerUis;
    for (auto peer: peers) {
        PeerUi u;
        auto p = peer.toObject();
        u.peerId = p["id"].toString();
        u.nat = (NatType) p["nat"].toInt();
        u.nick = p["nick"].toString();
        u.rtt = p["rtt"].toInteger();
        u.isServer = p["isServer"].toBool();
        peerUis.append(u);

        if (u.nat != StunTypeOpen && u.nat != StunTypeRestrictedNat && u.nat != StunTypeConeNat
            && u.nat != StunTypePortRestrictedNat && u.nat != StunTypeUnavailable) {
            badNatList.append(u.nick.isEmpty() ? QString("%1%2").arg(tr("用户")).arg(u.peerId.left(4)) : u.nick);
        }
    }

    if (badNatList.empty() || localNatType == StunTypeOpen || localNatType == StunTypeConeNat ||
        localNatType == StunTypeRestrictedNat) {
        ui->relayHint->setText(
                tr("当前应该无需中转服务器。\n如果长时间无法连接，请尝试换一个人创建房间再试。\n如果您曾经在上方设置过中转服务器，请确认其正在运行，否则也会造成无法建立连接。如需删除中转服务器，请清空地址后点击「连接中转服务器」即可。"));
    } else {
        ui->relayHint->setText(
                tr("当前可能需要中转服务器。\n以下用户 IPv4 NAT 类型无法直接连接：") + badNatList.join(", ") +
                tr("。\n但如果存在 IPv6，或许仍可以成功建立连接，请以最终结果为准。"));
    }

    emit onUpdatePeersUi(peerUis);
}

void CollabRoom::ndiFindWorker() {
    qInfo() << "start ndi find";

    // We are going to create an NDI finder that locates sources on the network.
    NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2();
    if (!pNDI_find) {
        emit onFatalError(tr("启动 NDI 发现组件失败"));
        return;
    }

    ndiSourceCount = 0;
    auto time = QDateTime::currentDateTime();
    while (!exiting) {
        if (ndiSourceCount == 0 && (QDateTime::currentDateTime() - time).count() > 2000) {
            time = QDateTime::currentDateTime();
            NDIlib_find_destroy(pNDI_find);
            pNDI_find = NDIlib_find_create_v2();
            if (!pNDI_find) {
                emit onFatalError(tr("启动 NDI 发现组件失败"));
                return;
            }
        }

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
            qDebug() << QString("%1. %2 %3").arg(i + 1).arg(ndiSources[i].p_ndi_name).arg(ndiSources[i].p_url_address);
            auto name = QString(ndiSources[i].p_ndi_name);
            if (!name.contains("(VTS Link)")) {
                sources.append(name);
            }
        }
        emit onNdiSourcesUpdated(sources);
    }

    // Destroy the NDI finder
    NDIlib_find_destroy(pNDI_find);

    qInfo() << "end ndi find";
}

void CollabRoom::ndiSendWorker() {
    qInfo() << "start ndi sender";

    // Create an NDI source that is called "My Video and Audio" and is clocked to the video.
    NDIlib_send_create_t NDI_send_create_desc;
    auto name = (QString("%1").arg(tr("联动 (VTS Link)"))).toUtf8();
    NDI_send_create_desc.p_ndi_name = name.data();

    // We create the NDI sender
    NDIlib_send_instance_t pNDI_send = NDIlib_send_create(&NDI_send_create_desc);
    if (!pNDI_send) {
        // try with suffix
        auto role = isServer ? "Server" : "Client";
        qDebug() << "Role is" << (role) << peerId;

        name = QString("%1 (%3)").arg(tr("联动 (VTS Link)")).arg(role).arg(peerId.left(2)).toUtf8();
        NDI_send_create_desc.p_ndi_name = name.data();
        pNDI_send = NDIlib_send_create(&NDI_send_create_desc);
        if (!pNDI_send) {
            emit onFatalError(tr("启动 NDI 视频源失败"));
            return;
        }
    }

    // We are going to create a 1920x1080 interlaced frame at 60Hz.
    NDIlib_video_frame_v2_t NDI_video_frame;
    NDI_video_frame.xres = VTSLINK_FRAME_WIDTH;
    NDI_video_frame.yres = VTSLINK_FRAME_HEIGHT;
    NDI_video_frame.FourCC = NDIlib_FourCC_type_BGRA;
    NDI_video_frame.frame_rate_D = VTSLINK_FRAME_D;
    NDI_video_frame.frame_rate_N = VTSLINK_FRAME_N;
    NDI_video_frame.line_stride_in_bytes = VTSLINK_FRAME_WIDTH * 4;

    qDebug() << "ndi send ndi2av";
    // ffmpeg coverter
    std::unique_ptr<FrameToAv> cvt = nullptr;

    if (isServer) {
        cvt = std::make_unique<FrameToAv>([=](auto av) {
            peersLock.lock();
            // This approach is bandwidth consuming, should be replaced by relay approach
            for (auto &s: servers) {
                s.second->sendAsync(av);
            }
            peersLock.unlock();
        });
        auto initErr = cvt->init(VTSLINK_FRAME_WIDTH, VTSLINK_FRAME_HEIGHT, VTSLINK_FRAME_D, VTSLINK_FRAME_N, true);
        if (initErr.has_value()) {
            emit onFatalError(initErr.value());
            NDIlib_send_destroy(pNDI_send);
            return;
        }
    }

    while (!exiting) {
        // We now submit the frame. Note that this call will be clocked so that we end up submitting
        // at exactly 60fps.

        QElapsedTimer t;
        t.start();

        if (d3d->render()) {

            // encode and send
            if (isServer) {
                cvt->processFast(d3d);
            }

            QElapsedTimer t1;
            t1.start();
            if (d3d->mapNdi(&NDI_video_frame)) {
                sendProcessFps.add(t1.nsecsElapsed());

                // blocking
                NDIlib_send_send_video_v2(pNDI_send, &NDI_video_frame);
            }

            d3d->unmapNdi();

            outputFps.add(t.nsecsElapsed());
        } else {
            QThread::msleep(1);
        }
    }

    // Destroy the NDI sender
    NDIlib_send_destroy(pNDI_send);

    if (isServer) {
        cvt->stop();
    }

    qInfo() << "end ndi sender";
}

void CollabRoom::peerDataChannelMessage(std::unique_ptr<VtsMsg> m, Peer *peer) const {
    // receive av from remote peer
    switch (m->type()) {
        case VTS_MSG_AVFRAME: {
            peer->decode(std::move(m));
            break;
        }
        case VTS_MSG_AVSTOP: {
            peer->resetDecoder();
            break;
        }
        case VTS_MSG_HEARTBEAT: {
            // server first, then client reply
            qDebug() << "receive dc heartbeat from" << peer->peerId;
            if (!isServer) {
                peer->sendHeartbeat();
            }
            break;
        }
        default: {
            break;
        }
    }
}

void CollabRoom::wsSendAsync(const std::string &content) {
    if (exiting)
        return;

    auto t = QThread::create([=]() {
        wsLock.lock();
        if (ws != nullptr && ws->isOpen())
            ws->send(content);
        wsLock.unlock();
    });
    connect(t, &QThread::finished, t, &QThread::deleteLater);
    t->start();
}

QString CollabRoom::errorToReadable(const QString &reason) {
    QString err = reason;
    if (reason == "init error") {
        err = tr("初始化错误");
    } else if (reason == "source error") {
        err = tr("NDI 来源错误");
    } else if (reason == "frame error") {
        err = tr("NDI 接收断开");
    } else if (reason == "frame format error") {
        err = tr("NDI 帧格式错误(Frame)，请确认选择了 VTube Studio 生成的来源（包含Live2D Camera字样）");
    } else if (reason == "frame size error") {
        err = tr("NDI 分辨率错误，请在 VTube Studio 设置中开启「NDI 输出分辨率」，并设置大小为「1920 X 1080」");
    } else if (reason == "frame change error") {
        err = tr("NDI 输出源格式发生变化，请不要在分享画面时更改 VTube Studio 中的 NDI 设置");
    } else if (reason == "line stride error") {
        err = tr("NDI 帧格式错误(Stride)，请确认选择了 VTube Studio 生成的来源（包含Live2D Camera字样）");
    } else if (reason == "no valid encoder") {
        err = tr("无法启动任何编码器！\n如果您曾在设置中强制使用某编码器，请尝试在顶部菜单「选项 - 设置」中取消再试。");
    } else if (reason == "cap no uyva") {
        err = tr("捕获分享模式无法使用 UYVA 编码器");
    }
    return err;
}

void CollabRoom::usageStatUpdate() {
    ui->usageStat->setText(QString("CPU: %1% FPS: %2")
                                   .arg(QString::number(usage::getCpuUsage(), 'f', 1))
                                   .arg(QString::number(outputFps.fps(), 'f', 1))
    );
}

void CollabRoom::heartbeatUpdate() {
    QJsonObject dict;

    peersLock.lock();
    for (auto &s: servers) {
        s.second->sendHeartbeat();
        dict[s.second->peerId] = (qint64) s.second->rtt();
    }
    peersLock.unlock();

    QJsonObject dto;
    dto["rtts"] = dict;
    dto["type"] = "rtt";
    QJsonDocument doc(dto);
    auto content = QString::fromUtf8(doc.toJson());
    qDebug() << content;

    wsSendAsync(content.toStdString());
}

void CollabRoom::dxgiSendWorker() {

    qInfo() << "start dxgi sender";

    // ffmpeg coverter
    std::unique_ptr<FrameToAv> cvt = nullptr;

    int frameD = VTSLINK_FRAME_D;
    int frameN = VTSLINK_FRAME_N;

    if (isServer) {
        cvt = std::make_unique<FrameToAv>([=](auto av) {
            peersLock.lock();
            // This approach is bandwidth consuming, should be replaced by relay approach
            for (auto &s: servers) {
                s.second->sendAsync(av);
            }
            peersLock.unlock();
        });
        auto initErr = cvt->init(VTSLINK_FRAME_WIDTH, VTSLINK_FRAME_HEIGHT, frameD, frameN, true);
        if (initErr.has_value()) {
            emit onFatalError(initErr.value());
            return;
        }
    }

    int64_t frameCount = 0;
    int64_t startTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    while (!exiting) {
        QElapsedTimer t;
        t.start();

        if (d3d->render()) {

            // encode and send
            if (isServer) {
                cvt->processFast(d3d);
            }

            QElapsedTimer t1;
            t1.start();

            d3d->present();

            sendProcessFps.add(t1.nsecsElapsed());
        }

        frameCount++;
        int64_t frameTime = frameCount * 1000000.0 * frameD / frameN;
        int64_t nextTime = startTime + frameTime;
        int64_t currentTime = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        auto sleepTime = nextTime - currentTime;
        if (sleepTime > 0) {
            QThread::usleep(sleepTime);
        }

        outputFps.add(t.nsecsElapsed());
    }

    if (isServer) {
        cvt->stop();
    }

    qInfo() << "end dxgi sender";
}

void CollabRoom::openBuyRelay() {
    auto buy = new BuyRelay(this);
    buy->exec();
    auto turn = buy->getTurnServer();
    if (turn.has_value()) {
        turnServer = turn.value();
        ui->relayInput->setText(turnServer);
        qDebug() << "bought relay" << turnServer;

        QSettings settings;
        settings.setValue("turnServer", turnServer);
        qDebug() << "update turn server" << turnServer;

        peersLock.lock();
        servers.clear();
        peersLock.unlock();

        QJsonObject dto;
        dto["type"] = "get";
        QJsonDocument doc(dto);
        auto content = QString::fromUtf8(doc.toJson());
        qDebug() << content;

        wsSendAsync(content.toStdString());
    }
    buy->deleteLater();
}

void CollabRoom::rtcFailed(Peer *peer) {
    qDebug() << "rtc failed";
    auto nick = peer->nick.isEmpty() ? tr("用户") + peer->peerId.first(4) : peer->nick;
    if (isServer) {
        auto tray = MainWindow::instance()->tray.get();
        tray->showMessage(tr("连接失败"),
                          QString(tr("与 %1 的连接失败，可能需要中转服务器！") + tr("正在尝试重新连接")).arg(nick));

        peersLock.lock();

        for (auto it = servers.begin(); it != servers.end();) {
            if (it->second.get() == peer) {
                qDebug() << "destroy peer for reconnect";
                it = servers.erase(it);
            } else {
                it++;
            }
        }

        peersLock.unlock();

        QJsonObject dto;
        dto["type"] = "get";
        QJsonDocument doc(dto);
        auto content = QString::fromUtf8(doc.toJson());
        qDebug() << content;

        wsSendAsync(content.toStdString());
    } else {
        auto tray = MainWindow::instance()->tray.get();
        tray->showMessage(tr("连接失败"), QString(tr("与 %1 的连接失败，请等待服务器重新连接")).arg(nick));
    }
}

void CollabRoom::toggleKeepTop() {
    keepTop = !keepTop;

    QPalette palette;
    QBrush brush(keepTop ? QColor::fromRgb(0, 119, 238) : QColor::fromRgb(0, 0, 0));
    brush.setStyle(Qt::SolidPattern);
    palette.setBrush(QPalette::Active, QPalette::ButtonText, brush);
    palette.setBrush(QPalette::Inactive, QPalette::ButtonText, brush);
    palette.setBrush(QPalette::Disabled, QPalette::ButtonText, brush);

    ui->keepTop->setPalette(palette);
    setWindowFlag(Qt::WindowStaysOnTopHint, keepTop);
    show();
}

void CollabRoom::downgradedToSharedMemory() {
    QSettings s;
    auto ig = s.value("ignoreDowngradedToSharedMemory", false).toBool();
    if (!ig) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(tr("性能提示"));
        box.setText(tr("由于 VTube Studio 与本软件没有运行在同一张显卡上，因此已自动使用兼容性方案进行捕获。\n点击「打开」以了解原因与解决方案。\n点击「忽略」不再出现本提示。"));
        auto ok = box.addButton(tr("我知道了"), QMessageBox::NoRole);
        auto open = box.addButton(tr("打开"), QMessageBox::NoRole);
        auto ign = box.addButton(tr("忽略"), QMessageBox::NoRole);
        box.exec();
        auto ret = dynamic_cast<QPushButton*>(box.clickedButton());
        if (ret == ign) {
            s.setValue("ignoreDowngradedToSharedMemory", true);
            s.sync();
        } else if (ret == open) {
            QDesktopServices::openUrl(QUrl("https://www.wolai.com/reito/c6iQ2dRR3aoVWEVzSydESe"));
        }
    }
}

CollabRoom *CollabRoom::instance() {
    return _instance;
}
