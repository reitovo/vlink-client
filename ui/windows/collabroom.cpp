#include "collabroom.h"
#include "NatTypeProbe/NatProb.h"
#include "frame_to_av.h"
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

#include <QDesktopServices>
#include <QSystemTrayIcon>
#include "d3d_to_frame.h"
#include "core/usage.h"
#include "dxgioutput.h"
#include "buyrelay.h"
#include "d3d_capture.h"
#include "spout_capture.h"

static CollabRoom *_instance;

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
    useDxCapture = settings.value("useDxCapture", false).toBool();
    qDebug() << "sender is" << (useDxCapture ? "dx" : "spout");

    ui->nick->setText(settings.value("nick").toString());

    this->roomId = roomId;
    this->isServer = isServer;

    d3d = std::make_shared<DxToFrame>();
    auto output = new DxgiOutput();
    if (settings.value("showDxgiWindow").toBool()) {
        output->move(0, 0);
        output->show();
    }
    d3d->init(true);

    if (useDxCapture) {
        dxgiCaptureStatus("idle");
        ui->shareMethods->setCurrentIndex(1);
    } else {
        ui->shareMethods->setCurrentIndex(0);
    }

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

        auto expires = settings.value("turnServerExpiresAt").toDateTime();
        auto ignoreTurnExpire = settings.value("ignoreTurnServerNotExpire").toBool();
        if (expires > QDateTime::currentDateTime() && !ignoreTurnExpire) {
            qDebug() << "Turn server still alive";
            QMessageBox box(this);
            box.setIcon(QMessageBox::Information);
            box.setWindowTitle(tr("中转服务器仍然可用！"));
            box.setText(tr("您上次购买的的中转服务器仍然可用，无需重新购买哦！\n"
                           "服务有效期至：%1").arg(expires.toString("yyyy/MM/dd hh:mm:ss")));
            auto ok = box.addButton(tr("我知道了"), QMessageBox::NoRole);
            auto ign = box.addButton(tr("下次购买前不再提示"), QMessageBox::NoRole);
            box.exec();
            auto ret = dynamic_cast<QPushButton *>(box.clickedButton());
            if (ret == ign) {
                settings.setValue("ignoreTurnServerNotExpire", true);
                settings.sync();
            }
        }
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
    connect(this, &CollabRoom::onDxgiCaptureStatus, this, &CollabRoom::dxgiCaptureStatus);
    connect(this, &CollabRoom::onNeedElevate, this, &CollabRoom::dxgiNeedElevate);

    connect(ui->btnSharingStatus, &QPushButton::clicked, this, &CollabRoom::toggleShare);
    connect(ui->btnSetNick, &QPushButton::clicked, this, &CollabRoom::setNick);
    connect(ui->copyRoomId, &QPushButton::clicked, this, &CollabRoom::copyRoomId);
    connect(ui->updateTurnServer, &QPushButton::clicked, this, &CollabRoom::updateTurnServer);
    connect(ui->relayHideShow, &QPushButton::clicked, this, &CollabRoom::toggleTurnVisible);
    connect(ui->openSettings, &QPushButton::clicked, this, &CollabRoom::openSetting);
    connect(ui->createRelay, &QPushButton::clicked, this, &CollabRoom::openBuyRelay);
    connect(ui->keepTop, &QPushButton::clicked, this, &CollabRoom::toggleKeepTop);
    connect(ui->btnFixRatio, &QPushButton::clicked, this, [=]() {
        this->needFixVtsRatio = true;
    });

    connect(ui->tutorialFaq, &QPushButton::clicked, this, [=]() {
        QDesktopServices::openUrl(QUrl("https://www.wolai.com/reito/nhenjFvkw5gDNM4tikEw5V"));
    });
    connect(ui->knowWhenRelay, &QPushButton::clicked, this, [=]() {
        QDesktopServices::openUrl(QUrl("https://www.wolai.com/reito/7R2Z4gPzcZSvPVRUy9fkrP"));
    });
    connect(ui->knowDeployRelay, &QPushButton::clicked, this, [=]() {
        QDesktopServices::openUrl(QUrl("https://www.wolai.com/reito/osFxEHHuiZNF3JMrhS6zV2"));
    });

    // Start sending thread
    frameSendThread = std::unique_ptr<QThread>(QThread::create([=, this]() {
        dxgiSendWorker();
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

    spoutDiscovery = std::make_unique<QTimer>(this);
    auto ignoreSpoutOpenHint = settings.value("ignoreSpoutOpenHint", false).toBool();
    connect(spoutDiscovery.get(), &QTimer::timeout, this, [=, this]() {
        std::set<std::string> senders;
        spoutSender.GetSenderNames(&senders);

        static int emptyCount = 0;
        if (senders.empty()) {
            emptyCount++;
            if (emptyCount == 3 && !ignoreSpoutOpenHint) {
                QMessageBox box(this);
                box.setIcon(QMessageBox::Information);
                box.setWindowTitle(tr("提示"));
                box.setText(tr("没有发现 Spout 来源，将无法捕获 VTube Studio 画面\n"
                               "请点击「查看详情」了解如何开启"));
                auto ok = box.addButton(tr("我知道了"), QMessageBox::NoRole);
                auto open = box.addButton(tr("查看详情"), QMessageBox::NoRole);
                auto ign = box.addButton(tr("不再提示"), QMessageBox::NoRole);
                box.exec();
                auto ret = dynamic_cast<QPushButton *>(box.clickedButton());
                if (ret == ign) {
                    QSettings s;
                    s.setValue("ignoreSpoutOpenHint", true);
                    s.sync();
                } else if (ret == open) {
                    QDesktopServices::openUrl(QUrl("https://www.wolai.com/reito/nhenjFvkw5gDNM4tikEw5V#3S3vaAGhXAPahqKyKqNy34"));
                }
            }
            ui->spoutSourceSelect->clear();
        } else {
            emptyCount = 0;
            QStringList strList;
            for (const auto &i: senders) {
                strList.push_back(QString::fromStdString(i));
            }
            setComboBoxIfChanged(strList, ui->spoutSourceSelect);
        }
    });
    spoutDiscovery->start(1000);
    connect(ui->spoutSourceSelect, &QComboBox::currentTextChanged, this, [=, this](const QString &s) {
        if (s.isEmpty())
            return;
        spoutName = s.toStdString();
        qDebug() << "set spout" << s;
    });

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

    spoutDiscovery.reset();
    usageStat.reset();
    heartbeat.reset();

    stopShareWorker();

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

    delete ui;
    qWarning() << "room exit";
}

QString CollabRoom::debugInfo() {
    auto ret = QString("Room Role: %1 Id: %2\nPeer Nick: %4 Id: %3\n%5 %6\n%7 %8")
            .arg(isServer ? "Server" : "Client").arg(roomId)
            .arg(peerId).arg(ui->nick->text())
            .arg(useDxCapture ? "Capture (D3D11) " : "Capture (Spout2) ")
            .arg(sendProcessFps.stat())
            .arg(isServer ? "Frame->Dx (D3D11 CapTick) " : "Frame->Av (D3D11 Receive) ")
            .arg(shareRecvFps.stat());
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
        error = QString("%1\n\"%2\"").arg(tr("房间不存在")).arg(roomId);
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

    QSettings s;
    s.setValue("nick", n);
    s.sync();

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
    QSettings settings;
    useDxCapture = settings.value("useDxCapture", false).toBool();
    qDebug() << "sender is" << (useDxCapture ? "dx" : "spout");

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
    ui->btnFixRatio->setEnabled(true);
    QTimer::singleShot(500, this, [this]() {
        ui->btnSharingStatus->setEnabled(true);
    });

    // Start worker
    shareRunning = true;
    if (isServer) {
        shareThread = std::unique_ptr<QThread>(QThread::create([=, this]() {
            if (useDxCapture) {
                dxgiShareWorkerServer();
            } else {
                spoutShareWorkerServer();
            }
        }));
    } else {
        shareThread = std::unique_ptr<QThread>(QThread::create([=, this]() {
            if (useDxCapture) {
                dxgiShareWorkerClient();
            } else {
                spoutShareWorkerClient();
            }
        }));
    }
    shareThread->start();
}

void CollabRoom::stopShare() {
    ui->btnSharingStatus->setText(tr("开始") + tr("分享 VTube Studio 画面"));
    ui->btnSharingStatus->setEnabled(false);
    ui->btnFixRatio->setEnabled(false);

    stopShareWorker();

    QTimer::singleShot(500, this, [this]() {
        ui->btnSharingStatus->setEnabled(true);
    });
}

void CollabRoom::spoutShareWorkerClient() {
    qInfo() << "spout capture client start";

    // spout capture
    std::shared_ptr<SpoutCapture> spout = std::make_shared<SpoutCapture>(nullptr, spoutName);
    if (!spout->init()) {
        emit onShareError("spout capture init failed");
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

        spout->captureTick(frameSeconds);

        auto e = cvt.processFast(spout);
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

    qInfo() << "spout capture client exit";
}

void CollabRoom::spoutShareWorkerServer() {
    qInfo() << "spout capture server start";

    // dx capture
    std::shared_ptr<SpoutCapture> spout = std::make_shared<SpoutCapture>(d3d, spoutName);
    if (!spout->init()) {
        emit onShareError("spout capture init failed");
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

        spout->captureTick(frameSeconds);

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

    qInfo() << "spout capture server exit";
}

void CollabRoom::dxgiShareWorkerClient() {
    qInfo() << "dx capture client start";

    // dx capture
    std::shared_ptr<DxCapture> dxCap = std::make_shared<DxCapture>(nullptr);
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

        tryFixVtsRatio(dxCap);

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

        tryFixVtsRatio(dxCap);

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

        // Set nick name
        {
            QJsonObject dto;
            dto["msg"] = ui->nick->text();
            dto["type"] = "nick";
            QJsonDocument doc(dto);

            auto content = QString::fromUtf8(doc.toJson());
            qDebug() << content;

            wsSendAsync(content.toStdString());
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
    } else if (reason == "no valid encoder") {
        err = tr("无法启动任何编码器！\n如果您曾在设置中强制使用某编码器，请尝试在顶部菜单「选项 - 设置」中取消再试。");
    } else if (reason == "cap no uyva") {
        err = tr("捕获分享模式无法使用 UYVA 编码器");
    }
    return err;
}

void CollabRoom::usageStatUpdate() {
    ui->usageStat->setText(QString("CPU: %1% FPS: %2 输入: %3 输出: %4")
                                   .arg(QString::number(usage::getCpuUsage(), 'f', 1))
                                   .arg(QString::number(outputFps.fps(), 'f', 1))
                                   .arg(useDxCapture ? "D3D11" : "Spout")
                                   .arg("D3D11")
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

        auto hours = buy->getTurnHours();
        auto expires = QDateTime::currentDateTime().addSecs(60 * ((60 * hours) + 10));

        QSettings settings;
        settings.setValue("turnServer", turnServer);
        settings.setValue("turnServerExpiresAt", expires);
        settings.setValue("turnServerMembers", buy->getTurnMembers());
        settings.setValue("ignoreTurnServerNotExpire", false);
        settings.sync();
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
        box.setText(tr("由于 VTube Studio 与本软件没有运行在同一张显卡上，因此已自动使用兼容性方案进行捕获。"));
        auto ok = box.addButton(tr("我知道了"), QMessageBox::NoRole);
        auto open = box.addButton(tr("查看详情"), QMessageBox::NoRole);
        auto ign = box.addButton(tr("不再提示"), QMessageBox::NoRole);
        box.exec();
        auto ret = dynamic_cast<QPushButton *>(box.clickedButton());
        if (ret == ign) {
            s.setValue("ignoreDowngradedToSharedMemory", true);
            s.sync();
        } else if (ret == open) {
            QDesktopServices::openUrl(QUrl("https://www.wolai.com/reito/c6iQ2dRR3aoVWEVzSydESe"));
        }
    }
}

void CollabRoom::dxgiCaptureStatus(QString text) {
    if (text == "idle") {
        text = tr("未分享");
    } else if (text == "init") {
        text = tr("尝试捕获中");
    } else if (text == "shmem") {
        text = tr("已捕获（兼容）");
    } else if (text == "shtex") {
        text = tr("已捕获");
    } else if (text == "fail") {
        text = tr("捕获失败");
    }
    ui->dxgiCaptureStatus->setText(text);
}

CollabRoom *CollabRoom::instance() {
    return _instance;
}

void CollabRoom::dxgiNeedElevate() {
    QSettings s;
    auto ig = s.value("ignoreNeedElevate", false).toBool();
    if (!ig) {
        stopShareWorker();
        QMessageBox box(this);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(tr("捕获失败"));
        box.setText(tr("捕获 VTube Studio 画面失败\n"
                       "可能的解决方案：\n"
                       "1. VTube Studio 还在启动中，请等待模型出现后再分享\n"
                       "2. 重启 Steam 与 VTube Studio，然后再次尝试开始分享"));
        auto ok = box.addButton(tr("我知道了"), QMessageBox::NoRole);
        auto open = box.addButton(tr("查看详情"), QMessageBox::NoRole);
        auto ign = box.addButton(tr("不再提示"), QMessageBox::NoRole);
        box.exec();
        auto ret = dynamic_cast<QPushButton *>(box.clickedButton());
        if (ret == ign) {
            s.setValue("ignoreNeedElevate", true);
            s.sync();
        } else if (ret == open) {
            QDesktopServices::openUrl(QUrl("https://www.wolai.com/reito/okrsy7aW8QM6EfTp35hVxs"));
        }
    }
}

void CollabRoom::tryFixVtsRatio(const std::shared_ptr<DxCapture> &cap) {
    if (needFixVtsRatio) {
        needFixVtsRatio = false;
        cap->fixWindowRatio();
    }
}
