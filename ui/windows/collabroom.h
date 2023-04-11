#ifndef COLLABROOM_H
#define COLLABROOM_H

#include "core/peer.h"
#include <QDialog>
#include "QMutex"
#include "QThread"
#include "SpoutGL/SpoutSenderNames.h"
#include "core/speed.h"
#include "core/room_server.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <map>
#include <QDateTime>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace Ui {
class CollabRoom;
}

class DxCapture;

// This is the collab main logic
class CollabRoom : public QDialog, public IDebugCollectable
{
    Q_OBJECT
    friend class Peer;
    friend class PeerItemWidget;
    friend class RoomServer;

public:
    static CollabRoom* instance();

    explicit CollabRoom(bool isServer, QString roomId = QString(), QWidget *parent = nullptr);
    ~CollabRoom() override;

    QString debugInfo() override;

signals:
    void onUpdatePeersUi(const google::protobuf::RepeatedPtrField<vts::server::Peer>& peers);
    void onShareError(QString);
    void onFatalError(QString);
    void onRtcFailed(Peer*);

    void onNeedElevate();
    void onDxgiCaptureStatus(QString text);
    void onDowngradedToSharedMemory();

private slots:
    void updatePeersUi(const google::protobuf::RepeatedPtrField<vts::server::Peer>& peers);

    void copyRoomId();
    void setNick();
    void updateTurnServer();
    void toggleTurnVisible();

    void toggleShare();
    void startShare();
    void stopShare();
    void shareError(const QString& reason);
    void fatalError(const QString& reason);
    void rtcFailed(Peer* peer);

    void downgradedToSharedMemory();
    void dxgiCaptureStatus(QString text);
    void dxgiNeedElevate();

    void openSetting();
    void openBuyRelay();

    void toggleKeepTop();

private:
    static QString errorToReadable(const QString& e);

    void spoutShareWorkerClient();
    void spoutShareWorkerServer();
    void dxgiShareWorkerClient();
    void dxgiShareWorkerServer();
    void stopShareWorker();

    void dxgiSendWorker();

    void spoutDiscoveryUpdate();
    void heartbeatUpdate();
    void usageStatUpdate();

    std::atomic_bool needFixVtsRatio = false;
    void tryFixVtsRatio(const std::shared_ptr<DxCapture>& cap);

    std::unique_ptr<RoomServer> roomServer;
    // Callback From RoomServer
    void onNotifyPeers(const vts::server::NotifyPeers& peers);
    void onNotifySdp(const vts::server::Sdp& sdp);
    void onNotifyFrameFormat(const vts::server::FrameFormatSetting& sdp);
    void onNotifyDestroy();
    void onNotifyForceIdr();

    void onRoomInfoSucceed(const vts::server::RspRoomInfo& info);
    void onRoomInfoFailed(const std::string &error);

    void updatePeers(const google::protobuf::RepeatedPtrField<vts::server::Peer> & peers);

    std::atomic_bool notifiedForceIdr = false;
public:
    void requestIdr();

private:
    std::atomic_bool exiting = false;
    Ui::CollabRoom *ui;

    bool isServer;
    bool useDxCapture;
    QString roomId;
    QString localPeerId;

    float frameRate = 60;
    int frameWidth = 1920;
    int frameHeight = 1080;

    FpsCounter outputFps;
    FpsCounter sendProcessFps;
    FpsCounter shareRecvFps;

    QMutex peersLock;

    // As server
    QString turnServer;
    std::map<QString, std::unique_ptr<Peer>> clientPeers;
    // As client
    std::unique_ptr<Peer> serverPeer;

    std::unique_ptr<QTimer> heartbeat;
    std::unique_ptr<QTimer> usageStat;
    std::unique_ptr<QTimer> spoutDiscovery;
    std::atomic_bool shareRunning = false;
    std::unique_ptr<QThread> shareThread;
    std::unique_ptr<QThread> frameSendThread;
    std::shared_ptr<DxToFrame> d3d;

    Speed txSpeed, rxSpeed;

    std::string spoutName = "VTubeStudioSpout";
    spoutSenderNames spoutSender;

    bool keepTop = false;
};

#endif // COLLABROOM_H
