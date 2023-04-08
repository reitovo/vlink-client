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

    explicit CollabRoom(QString roomId, bool isServer, QWidget *parent = nullptr);
    ~CollabRoom() override;

    QString debugInfo() override;

signals:
    void onUpdatePeersUi(QList<PeerUi> peerUis);
    void onRoomExit(QString);
    void onShareError(QString);
    void onFatalError(QString);
    void onRtcFailed(Peer*);

    void onNeedElevate();
    void onDxgiCaptureStatus(QString text);
    void onDowngradedToSharedMemory();

private slots:
    void updatePeersUi(QList<PeerUi> peerUis);

    void copyRoomId();
    void exitRoom(const QString& reason);
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
    QString errorToReadable(const QString& e);

    void spoutShareWorkerClient();
    void spoutShareWorkerServer();
    void dxgiShareWorkerClient();
    void dxgiShareWorkerServer();
    void stopShareWorker();

    void dxgiSendWorker();

    void heartbeatUpdate();
    void usageStatUpdate();

    std::atomic_bool needFixVtsRatio = false;
    void tryFixVtsRatio(const std::shared_ptr<DxCapture>& cap);

    std::unique_ptr<RoomServer> roomServer;
    // Callback From RoomServer
    void onNotifyRoomInfo(const vts::server::NotifyRoomInfo& info);
    void onNotifyRtcSdp(const vts::server::NotifyRtcSdp& sdp);

    void updatePeers(const google::protobuf::RepeatedPtrField<vts::server::Peer> & peers);

private:
    std::atomic_bool exiting = false;
    Ui::CollabRoom *ui;

    bool isServer;
    bool useDxCapture;
    QString roomId;
    QString peerId;

    float frameRate = 60;
    int frameWidth = 1920;
    int frameHeight = 1080;

    FpsCounter outputFps;
    FpsCounter sendProcessFps;
    FpsCounter shareRecvFps;

    NatType localNatType = NatType::StunTypeUnknown;

    QMutex peersLock;

    // As server
    QString turnServer;
    std::map<std::string, std::unique_ptr<Peer>> clientPeers;
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
