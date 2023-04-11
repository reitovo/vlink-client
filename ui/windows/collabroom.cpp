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

static CollabRoom *roomInstance;

CollabRoom::CollabRoom(bool isServer, QString roomId, QWidget *parent) :
        QDialog(parent),
        ui(new Ui::CollabRoom) {
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);

    // Workaround for color
    QPalette palette;
    QBrush brush(QColor(255, 124, 159));
    brush.setStyle(Qt::SolidPattern);
    palette.setBrush(QPalette::Active, QPalette::ButtonText, brush);
    palette.setBrush(QPalette::Inactive, QPalette::ButtonText, brush);
    palette.setBrush(QPalette::Disabled, QPalette::ButtonText, brush);
    ui->copyRoomId->setPalette(palette);
    ui->btnSharingStatus->setPalette(palette);

    this->setWindowTitle(tr("联动 VTube Studio Link"));

    this->isServer = isServer;

    QSettings settings;

    this->roomId = roomId;
    localPeerId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    useDxCapture = settings.value("useDxCapture", false).toBool();
    qDebug() << "sender is" << (useDxCapture ? "dx" : "spout");

    if (useDxCapture) {
        dxgiCaptureStatus("idle");
        ui->shareMethods->setCurrentIndex(1);
    } else {
        ui->shareMethods->setCurrentIndex(0);
    }

    auto role = isServer ? "Server" : "Client";
    qDebug() << "Role is" << (role) << localPeerId;

    if (isServer) {
        turnServer = settings.value("turnServer", QString()).toString();
        ui->relayInput->setText(turnServer);
        qDebug() << "Turn server" << turnServer;

        auto expires = settings.value("turnServerExpiresAt", QDateTime()).toDateTime();
        auto ignoreTurnExpire = settings.value("ignoreTurnServerNotExpire").toBool();
        if (expires > QDateTime::currentDateTime() && !ignoreTurnExpire) {
            qDebug() << "Turn server still alive";
            QMessageBox box(this);
            box.setIcon(QMessageBox::Information);
            box.setWindowTitle(tr("中转服务器仍然可用！"));
            box.setText(tr("您上次购买的的中转服务器仍然可用，已为您自动恢复，无需重新购买哦！\n"
                           "服务有效期至：%1").arg(expires.toString("yyyy/MM/dd hh:mm:ss")));
            auto ok = box.addButton(tr("我知道了"), QMessageBox::NoRole);
            auto ign = box.addButton(tr("下次购买前不再提示"), QMessageBox::NoRole);
            box.exec();
            auto ret = dynamic_cast<QPushButton *>(box.clickedButton());
            if (ret == ign) {
                settings.setValue("ignoreTurnServerNotExpire", true);
                settings.sync();
            }
        } else if (expires < QDateTime::currentDateTime() && expires != QDateTime()) {
            qDebug() << "Turn server dead, cleaning";
            QMessageBox box(this);
            box.setIcon(QMessageBox::Information);
            box.setWindowTitle(tr("中转服务器已过期"));
            box.setText(tr("您上次购买的的中转服务器已过期，已为您自动清除中转服务器"));
            box.addButton(tr("我知道了"), QMessageBox::NoRole);
            box.exec();
            settings.setValue("turnServer", QString());
            settings.setValue("turnServerExpiresAt", QDateTime());
            settings.sync();
        }
    }

    connect(this, &CollabRoom::onUpdatePeersUi, this, &CollabRoom::updatePeersUi);
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
    connect(ui->btnFixRatio, &QPushButton::clicked, this, [=, this]() {
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
    connect(ui->feedQQGroup, &QPushButton::clicked, this, [=]() {
        QDesktopServices::openUrl(QUrl("https://jq.qq.com/?_wv=1027&k=0Gtymtze"));
    });
    connect(ui->feedQQGuild, &QPushButton::clicked, this, [=]() {
        QDesktopServices::openUrl(QUrl("https://pd.qq.com/s/3y2gr1nmy"));
    });

    connect(ui->spoutSourceSelect, &QComboBox::currentTextChanged, this, [=, this](const QString &s) {
        if (s.isEmpty())
            return;
        spoutName = s.toStdString();
        qDebug() << "set spout" << s;
    });

    // If server, start sending heartbeat, and rtt update
    if (isServer) {
        heartbeat = std::make_unique<QTimer>(this);
        connect(heartbeat.get(), &QTimer::timeout, this, [this]() {
            heartbeatUpdate();
        });
        heartbeat->start(10000);
    }

    usageStat = std::make_unique<QTimer>(this);
    connect(usageStat.get(), &QTimer::timeout, this, [this]() {
        usageStatUpdate();
    });
    usageStat->start(500);

    spoutDiscovery = std::make_unique<QTimer>(this);
    connect(spoutDiscovery.get(), &QTimer::timeout, this, [=, this]() {
        spoutDiscoveryUpdate();
    });
    spoutDiscovery->start(1000);

    if (!isServer) {
        resize(QSize(381, 360));
    } else {
        resize(QSize(730, 360));
    }

    auto nick = settings.value("nick").toString();
    ui->nick->setText(nick);

    roomServer = std::make_unique<RoomServer>(this);
    if (isServer) {
        auto _frameWidth = settings.value("frameWidth", 1920).toInt();
        auto _frameHeight = settings.value("frameHeight", 1080).toInt();
        auto _frameRate = settings.value("frameRate", 60).toInt();
        roomServer->createRoom(localPeerId.toStdString(), nick.toStdString(), _frameWidth, _frameHeight, _frameRate, turnServer.toStdString());
    } else {
        roomServer->joinRoom(localPeerId.toStdString(), roomId.toStdString(), nick.toStdString());
    }

    roomInstance = this;
}

void CollabRoom::onRoomInfoSucceed(const vts::server::RspRoomInfo &info) {
    QSettings settings;

    this->roomId = QString::fromStdString(info.roomid());
    frameWidth = info.format().framewidth();
    frameHeight = info.format().frameheight();
    frameRate = info.format().framerate();

    auto output = new DxgiOutput();
    output->setSize(frameWidth, frameHeight);
    if (settings.value("showDxgiWindow").toBool()) {
        output->move(0, 0);
        output->show();
    }

    d3d = std::make_shared<DxToFrame>(frameWidth, frameHeight);
    d3d->init(true);

    // Start sending thread
    frameSendThread = std::unique_ptr<QThread>(QThread::create([=, this]() {
        dxgiSendWorker();
    }));
    frameSendThread->start();

    show();
}

void CollabRoom::onRoomInfoFailed(const string &error) {
    emit onFatalError(QString::fromStdString(error));
}

CollabRoom::~CollabRoom() {
    roomInstance = nullptr;
    exiting = true;

    auto dxgi = DxgiOutput::getWindow();
    if (dxgi) {
        dxgi->deleteLater();
    }

    spoutDiscovery.reset();
    usageStat.reset();
    heartbeat.reset();

    stopShareWorker();

    terminateQThread(frameSendThread);

    roomServer.reset();

    if (isServer) {
        ScopedQMutex _(&peersLock);
        for (auto &a: clientPeers) {
            if (a.second != nullptr) {
                a.second->close();
                a.second = nullptr;
            }
        }
        clientPeers.clear();
    } else {
        ScopedQMutex _(&peersLock);
        if (serverPeer != nullptr)
            serverPeer->close();
        serverPeer = nullptr;
    }

    delete ui;
    qWarning() << "room exit";
}

void CollabRoom::spoutDiscoveryUpdate() {
    QSettings settings;
    std::set<std::string> senders;
    spoutSender.GetSenderNames(&senders);
    auto ignoreSpoutOpenHint = settings.value("ignoreSpoutOpenHint", false).toBool();

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
}

QString CollabRoom::debugInfo() {
    auto ret = QString("Room Role: %1 Id: %2\nPeer Nick: %4 Id: %3\n%5 %6\n%7 %8")
            .arg(isServer ? "Server" : "Client").arg(roomId)
            .arg(localPeerId).arg(ui->nick->text())
            .arg(useDxCapture ? "Capture (D3D11) " : "Capture (Spout2) ")
            .arg(sendProcessFps.stat())
            .arg(isServer ? "Frame->Dx (D3D11 CapTick) " : "Frame->Av (D3D11 Receive) ")
            .arg(shareRecvFps.stat());
    return ret;
}

void CollabRoom::copyRoomId() {
    auto cb = QApplication::clipboard();
    cb->setText(roomId);
    MainWindow::instance()->tray->showMessage(tr("复制成功"),
                                              tr("请不要在直播画面中展示房间ID！\n已复制到剪贴板，快分享给参加联动的人吧~"),
                                              MainWindow::instance()->tray->icon());
}

void CollabRoom::setNick() {
    auto n = ui->nick->text();
    if (n.length() > 16) {
        n = n.left(16);
        ui->nick->setText(n);
    }
    qDebug() << "new nick" << n;


}

void CollabRoom::updateTurnServer() {
    auto ipt = ui->relayInput->text();
    turnServer = ipt;

    QSettings settings;
    settings.setValue("turnServer", turnServer);
    settings.sync();
    qDebug() << "update turn server" << turnServer;

    ScopedQMutex _(&peersLock);
    for(auto & peer : clientPeers) {
        peer.second->startServer();
    }

    MainWindow::instance()->tray->showMessage(tr("设置成功"), tr("所有联动人将重新连接，请稍后"),
                                              MainWindow::instance()->tray->icon());
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

void CollabRoom::shareError(const QString& reason) {
    stopShareWorker();

    QMessageBox::critical(this, tr("分享错误"), errorToReadable(reason));

    ui->btnSharingStatus->setText(tr("开始") + tr("分享 VTube Studio 画面"));
    ui->btnSharingStatus->setEnabled(true);
}

void CollabRoom::fatalError(const QString& reason) {
    if (reason == "nv driver old") {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Critical);
        box.setWindowTitle(tr("错误"));
        box.setText(tr("您的 NVIDIA 显卡驱动版本过低，请更新显卡驱动。\n"
                       "点击「更新」前往官网驱动下载页面"));
        auto open = box.addButton(tr("更新"), QMessageBox::NoRole);
        auto ok = box.addButton(tr("关闭"), QMessageBox::NoRole);
        box.exec();
        auto ret = dynamic_cast<QPushButton *>(box.clickedButton());
        if (ret == open) {
            QDesktopServices::openUrl(QUrl("https://www.nvidia.cn/Download/index.aspx?lang=cn"));
        }
    } else {
        // Show default dialog
        QString error;
        if (reason == "host leave") {
            error = tr("房主已离开");
        } else if (reason == "room not found") {
            error = QString("%1\n\"%2\"").arg(tr("房间不存在")).arg(roomId);
        } else if (reason == "init room req failed") {
            error = tr("请求房间信息失败");
        } else if (reason == "room init timeout") {
            error = tr("请求房间信息超时，请重试");
        }
        QMessageBox box(this);
        box.setIcon(QMessageBox::Critical);
        box.setWindowTitle(tr("错误"));
        box.setText(errorToReadable(error));
        box.addButton(tr("关闭"), QMessageBox::NoRole);
        box.exec();
    }
    close();
}

void CollabRoom::openSetting() {
    auto w = new SettingWindow(this);
    w->show();
    connect(w, &QObject::destroyed, this, [=, this]() {
        QSettings settings;
        useDxCapture = settings.value("useDxCapture", false).toBool();
        qDebug() << "sender is" << (useDxCapture ? "dx" : "spout");
        if (useDxCapture) {
            dxgiCaptureStatus("idle");
            ui->shareMethods->setCurrentIndex(1);
        } else {
            ui->shareMethods->setCurrentIndex(0);
        }
    });
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
        bool notConnected = false;
        {
            ScopedQMutex _(&peersLock);
            if (serverPeer == nullptr || !serverPeer->connected()) {
                notConnected = true;
            }
        }
        if (notConnected) {
            emit onShareError(tr("尚未成功连接服务器，无法开始分享"));
            return;
        }
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
    std::shared_ptr<SpoutCapture> spout = std::make_shared<SpoutCapture>(frameWidth, frameHeight, nullptr, spoutName);
    if (!spout->init()) {
        emit onShareError("spout capture init failed");
        return;
    }

    // ffmpeg coverter
    FrameToAv cvt(frameWidth, frameHeight, frameRate, [=, this](auto av) {
        ScopedQMutex _(&peersLock);
        if (serverPeer != nullptr)
            serverPeer->sendAsync(std::move(av));
    });

    auto initErr = cvt.init(true);
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

    int64_t frameCount = 0;
    int64_t startTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    float frameSeconds = 1.0f / frameRate;

    while (shareRunning) {
        QElapsedTimer t;
        t.start();

        if (notifiedForceIdr) {
            notifiedForceIdr = false;
            cvt.forceIdr();
        }

        spout->captureTick(frameSeconds);

        auto e = cvt.processFast(spout);
        if (e.has_value()) {
            emit onShareError(e.value());
            shareRunning = false;
            goto clean;
        }

        shareRecvFps.add(t.nsecsElapsed());

        frameCount++;
        int64_t frameTime = frameCount * 1000000.0 / frameRate;
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
    std::shared_ptr<SpoutCapture> spout = std::make_shared<SpoutCapture>(frameWidth, frameHeight, d3d, spoutName);
    if (!spout->init()) {
        emit onShareError("spout capture init failed");
        return;
    }

    int64_t frameCount = 0;
    int64_t startTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    float frameSeconds = 1.0f / frameRate;

    while (shareRunning) {

        QElapsedTimer t;
        t.start();

        spout->captureTick(frameSeconds);

        shareRecvFps.add(t.nsecsElapsed());

        frameCount++;
        int64_t frameTime = frameCount * 1000000.0 / frameRate;
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
    std::shared_ptr<DxCapture> dxCap = std::make_shared<DxCapture>(frameWidth, frameHeight, nullptr);
    if (!dxCap->init()) {
        emit onShareError("dx capture init failed");
        return;
    }

    // ffmpeg coverter
    FrameToAv cvt(frameWidth, frameHeight, frameRate, [=, this](auto av) {
        ScopedQMutex _(&peersLock);
        if (serverPeer != nullptr)
            serverPeer->sendAsync(std::move(av));
    });

    auto initErr = cvt.init(true);
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

    int64_t frameCount = 0;
    int64_t startTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    float frameSeconds = 1.0f  / frameRate;

    while (shareRunning) {
        QElapsedTimer t;
        t.start();

        if (notifiedForceIdr) {
            notifiedForceIdr = false;
            cvt.forceIdr();
        }

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
        int64_t frameTime = frameCount * 1000000.0 / frameRate;
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
    std::shared_ptr<DxCapture> dxCap = std::make_shared<DxCapture>(frameWidth, frameHeight, d3d);
    if (!dxCap->init()) {
        emit onShareError("dx capture init failed");
        return;
    }

    int64_t frameCount = 0;
    int64_t startTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    float frameSeconds = 1.0f / frameRate;

    while (shareRunning) {

        QElapsedTimer t;
        t.start();

        tryFixVtsRatio(dxCap);

        dxCap->captureTick(frameSeconds);

        shareRecvFps.add(t.nsecsElapsed());

        frameCount++;
        int64_t frameTime = frameCount * 1000000.0 / frameRate;
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
    terminateQThread(shareThread);
}

void CollabRoom::updatePeers(const google::protobuf::RepeatedPtrField<vts::server::Peer>& peers) {
    if (isServer) {
        ScopedQMutex _(&peersLock);
        QList<QString> alive;
        for (const auto& peer: peers) {
            // Find clients
            if (!peer.isserver()) {
                auto id = QString::fromStdString(peer.peerid());
                alive.append(id);

                if (!clientPeers.contains(id)) {
                    qDebug() << "create peer" << id;
                    auto server = std::make_unique<Peer>(this, id);
                    server->startServer();
                    clientPeers[id] = std::move(server);
                }
            }
        }
        // If one leaves, remove it.
        for (auto it = clientPeers.begin(); it != clientPeers.end();) {
            if (!alive.contains(it->first)) {
                it = clientPeers.erase(it);
            } else {
                it++;
            }
        }
    }

    emit onUpdatePeersUi(peers);
}

void CollabRoom::updatePeersUi(const google::protobuf::RepeatedPtrField<vts::server::Peer>& peers) {
    while (ui->peerList->count() < peers.size()) {
        auto item = new QListWidgetItem(ui->peerList);
        auto peer = new PeerItemWidget(this);
        item->setSizeHint(QSize(0, 32));
        ui->peerList->addItem(item);
        ui->peerList->setItemWidget(item, peer);
    }

    while (ui->peerList->count() > peers.size()) {
        auto item = ui->peerList->item(0);
        auto widget = ui->peerList->itemWidget(item);
        ui->peerList->removeItemWidget(item);
        delete item;
        delete widget;
    }

    auto idx = 0;
    QStringList badNatList;
    auto allUnknown = true;
    for (auto &p: peers) {
        auto item = ui->peerList->item(idx++);
        auto widget = reinterpret_cast<PeerItemWidget *>(ui->peerList->itemWidget(item));
        widget->updatePeer(p);

        auto nat = (NatType)p.nattype();
        auto qNick = QString::fromStdString(p.nick());
        auto qPeerId = QString::fromStdString(p.peerid());
        if (nat != StunTypeUnknown)
            allUnknown = false;
        if (nat != StunTypeOpen && nat != StunTypeRestrictedNat && nat != StunTypeConeNat
            && nat != StunTypePortRestrictedNat && nat != StunTypeUnavailable) {
            badNatList.append(qNick.isEmpty() ? QString("%1%2").arg(tr("用户")).arg(qPeerId.left(4)) : qNick);
        }
    }

    auto localNatType = roomServer->getLocalNatType();
    if (allUnknown) {
        ui->relayHint->setText("☕" + tr("网络检测中"));
        ui->relayHint->setToolTip(tr("检测中，请稍后"));

    }  else if (badNatList.empty() || localNatType == StunTypeOpen || localNatType == StunTypeConeNat ||
        localNatType == StunTypeRestrictedNat) {
        ui->relayHint->setText("✅" + tr("网络良好"));
        ui->relayHint->setToolTip(
                tr("当前应该无需中转服务器。\n如果长时间无法连接，请尝试换一个人创建房间再试。\n如果您曾经在上方设置过中转服务器，请确认其正在运行，否则也会造成无法建立连接。如需删除中转服务器，请清空地址后点击「连接中转服务器」即可。"));
    } else {
        ui->relayHint->setText("⚠" + tr("存在问题"));
        ui->relayHint->setToolTip(
                tr("当前可能需要中转服务器。\n以下用户 IPv4 NAT 类型无法直接连接：") + badNatList.join(", ") +
                tr("。\n但如果存在 IPv6，或许仍可以成功建立连接，请以最终结果为准。"));
    }
}

QString CollabRoom::errorToReadable(const QString &reason) {
    QString err = reason;
    if (reason == "init error") {
        err = tr("初始化错误");
    } else if (reason == "no valid encoder") {
        err = tr("没有可用的编码器，请确认您的电脑拥有显卡，且已更新显卡驱动至最新版。");
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

    // Speed Stat
    size_t tx = 0, rx = 0;
    if (isServer) {
        ScopedQMutex _(&peersLock);
        for (auto &s: clientPeers) {
            if (s.second == nullptr)
                continue;
            tx += s.second->txBytes();
            rx += s.second->rxBytes();
        }
    } else {
        ScopedQMutex _(&peersLock);
        if (serverPeer != nullptr) {
            tx += serverPeer->txBytes();
            rx += serverPeer->rxBytes();
        }
    }
    txSpeed.update(tx);
    rxSpeed.update(rx);
    ui->speedStat->setText(QString("%1 ↑↓ %2")
            .arg(txSpeed.speed())
            .arg(rxSpeed.speed()));
}

void CollabRoom::heartbeatUpdate() {
    vts::server::ReqRtt rtt;
    ScopedQMutex _(&peersLock);
    for (auto &s: clientPeers) {
        s.second->sendHeartbeat();
        rtt.mutable_rtt()->emplace(s.second->remotePeerId.toStdString(), s.second->rtt());
    }
    roomServer->setRtt(rtt);
}

void CollabRoom::dxgiSendWorker() {

    qInfo() << "start dxgi sender";

    // ffmpeg coverter
    std::unique_ptr<FrameToAv> cvt = nullptr;

    if (isServer) {
        cvt = std::make_unique<FrameToAv>(frameWidth, frameHeight, frameRate, [=, this](auto av) {
            ScopedQMutex _(&peersLock);
            // This approach is bandwidth consuming, should be replaced by relay approach
            for (auto &s: clientPeers) {
                s.second->sendAsync(av);
            }
        });
        auto initErr = cvt->init(true);
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
                if (notifiedForceIdr) {
                    notifiedForceIdr = false;
                    cvt->forceIdr();
                }

                cvt->processFast(d3d);
            }

            QElapsedTimer t1;
            t1.start();

            d3d->present();

            sendProcessFps.add(t1.nsecsElapsed());
        }

        frameCount++;
        int64_t frameTime = frameCount * 1000000.0 / frameRate;
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
        settings.setValue("turnServerExpiresAt", expires);
        settings.setValue("turnServerMembers", buy->getTurnMembers());
        settings.setValue("ignoreTurnServerNotExpire", false);
        settings.sync();
        qDebug() << "update turn server" << turnServer;

        updateTurnServer();
    }
    buy->deleteLater();
}

void CollabRoom::rtcFailed(Peer *peer) {
    qDebug() << "rtc failed";
    auto nick = peer->nick.isEmpty() ? tr("用户") + peer->remotePeerId.first(4) : peer->nick;
    if (isServer) {
        auto tray = MainWindow::instance()->tray.get();
        tray->showMessage(tr("连接失败"),
                          QString(tr("与 %1 的连接失败，可能需要中转服务器！") + tr("正在尝试重新连接")).arg(nick));
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
    return roomInstance;
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

void CollabRoom::onNotifyPeers(const vts::server::NotifyPeers &peers) {
    updatePeers(peers.peers());
}

void CollabRoom::onNotifySdp(const vts::server::Sdp &sdp) {
    ScopedQMutex _(&peersLock);
    auto from = QString::fromStdString(sdp.frompeerid());
    if (isServer) {
        for(auto & peer : clientPeers) {
            if (peer.first == from) {
                qDebug() << "client" << peer.first << "offered sdp to server" ;
                peer.second->setClientRemoteSdp(sdp);
            }
        }
    } else {
        serverPeer = std::make_unique<Peer>(this, from);
        serverPeer->startClient(sdp);
    }
}

void CollabRoom::onNotifyFrameFormat(const vts::server::FrameFormatSetting &sdp) {

}

void CollabRoom::onNotifyDestroy() {
    emit onFatalError("host leave");
}

void CollabRoom::requestIdr() {
    if (exiting)
        return;
    roomServer->requestIdr();
}

void CollabRoom::onNotifyForceIdr() {
    notifiedForceIdr = true;
}
