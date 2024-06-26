#ifndef COLLABROOM_H
#define COLLABROOM_H

#include "core/peer.h"
#include <QDialog>
#include "QMutex"
#include "QThread"
#include "SpoutGL/SpoutSenderNames.h"
#include "core/speed.h"
#include "core/room_server.h"
#include "QMessageBox"
#include <QJsonObject>
#include <QJsonDocument>
#include <map>
#include <QDateTime>
#include <vector>
#include <tuple>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace Ui {
    class CollabRoom;
}

class DxCapture;
class DxgiOutput;

// This is the collab main logic
class CollabRoom : public QDialog, public IDebugCollectable {
Q_OBJECT
    friend class Peer;
    friend class PeerItemWidget;
    friend class RoomServer;
    friend class FrameQuality;
    friend class BuyRelay;

public:
    static CollabRoom *instance();

    explicit CollabRoom(bool isServer, QString roomId = QString(), QWidget *parent = nullptr);
    ~CollabRoom() override;

    QString debugInfo() override;

signals:
    void onUpdatePeersUi(const google::protobuf::RepeatedPtrField<vts::server::Peer> &peers);

    void onShareError(QString);
    void onFatalError(QString);
    void onRtcFailed(Peer *);
    void onRoomServerError(QString, QString);

    void onNeedElevate();
    void onDxgiCaptureStatus(QString text);
    void onDowngradedToSharedMemory();
    void onSpoutOpenSharedFailed();

    void onRoomInfoSucceed(const vts::server::RspRoomInfo &info);
    void onRoomInfoFailed(const std::string &error);
    void emitNotifyFrameFormat(const vts::server::FrameFormatSetting &sdp);

private slots:
    void updatePeersUi(const google::protobuf::RepeatedPtrField<vts::server::Peer> &peers);

    void copyRoomId();
    void setNick();
    void updateTurnServer();
    void toggleTurnVisible();
    void updateFrameQualityText();

    void toggleShare();
    void startShare();
    void stopShare();
    void roomServerError(const QString &func, const QString &reason);
    void shareError(const QString &reason);
    void fatalError(const QString &reason);
    void resetStartShareText();
    void rtcFailed(Peer *peer) const;

    void downgradedToSharedMemory();
    void dxgiCaptureStatus(QString text);
    void dxgiNeedElevate();

    void openSetting();
    void openQualitySetting();
    void openBuyRelay();

    void toggleKeepTop();

private:
    static QString errorToReadable(const QString &e);

    void spoutShareWorkerClient();
    void spoutShareWorkerServer();
    void dxgiShareWorkerClient();
    void dxgiShareWorkerServer();
    void stopShareWorker();

    void dxgiSendWorker();

    void checkDxCaptureNeedElevate();
    void spoutOpenSharedFailed();
    void spoutDiscoveryUpdate();
    void heartbeatUpdate();
    void usageStatUpdate();

    std::atomic_bool needFixVtsRatio = false;
    void tryFixVtsRatio(const std::shared_ptr<DxCapture> &cap);

    std::unique_ptr<RoomServer> roomServer;
    // Callback From RoomServer
    void onNotifyPeers(const vts::server::NotifyPeers &peers);
    void onNotifySdp(const vts::server::Sdp &sdp);
    void onNotifyTurn(const std::string &turn);
    void onNotifyCandidate(const vts::server::Candidate& candidate);
    void onNotifyFrameFormat(const vts::server::FrameFormatSetting &sdp);
    void onNotifyDestroy();
    void onNotifyForceIdr();

    void consumeCandidate();
    void consumeSdp();

    void applyNewFrameFormat(const vts::server::FrameFormatSetting &frame);
    void setShareInfo(bool start);

    void roomInfoSucceed(const vts::server::RspRoomInfo &info);
    void roomInfoFailed(const std::string &error);

    void updatePeers(const google::protobuf::RepeatedPtrField<vts::server::Peer> &peers);

    std::atomic_bool notifiedForceIdr = false;
public:
    void requestIdr(const std::string& reason, const std::string& peer);

private:
    std::atomic_bool exiting = false;
    std::atomic_bool resettingFrameFormat = false;
    Ui::CollabRoom *ui;

    bool isServer;
    bool useDxCapture;
    bool isRoomInfoReady;

    QString roomId;
    QString localPeerId;

    FrameQualityDesc quality;

    DxgiOutput *dxgiOutputWindow = nullptr;
    QMessageBox *roomOpenWaiting = nullptr;

    FpsCounter outputFps;
    FpsCounter sendProcessFps;
    FpsCounter shareRecvFps;

    QMutex peersLock;

    std::atomic_int peerCount;
    // As server
    QString turnServer;
    std::map<QString, std::unique_ptr<Peer>> clientPeers;
    // As client
    std::unique_ptr<Peer> serverPeer;
    QString currentServerPeerId;

    std::vector<std::tuple<vts::server::Candidate, QDateTime>> candidateQueue;
    std::vector<std::tuple<vts::server::Sdp, QDateTime>> sdpQueue;

    std::unique_ptr<QThread> heartbeat;
    std::unique_ptr<QTimer> spoutDiscovery;
    std::unique_ptr<QTimer> usageStat;
    std::atomic_bool shareRunning = false;
    std::unique_ptr<QThread> shareThread;
    std::unique_ptr<QThread> frameSendThread;
    std::shared_ptr<DxToFrame> d3d;

    std::string spoutSourceName = "VTubeStudioSpout";
    spoutSenderNames spoutSenderNames;

    QString roomEndpoint;
    std::atomic_bool isPrivateRoomEndpoint;
    std::atomic_bool privateServerNoSsl;

    bool keepTop = false;

    // Hint for sharing
    int notSharingCount = 0;
    std::unique_ptr<QTimer> hintShare;
};

#endif // COLLABROOM_H
