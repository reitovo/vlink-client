#ifndef COLLABROOM_H
#define COLLABROOM_H

#include "core/peer.h"
#include <QDialog>
#include "QMutex"
#include "QThread"
#include <QJsonObject>
#include <QJsonDocument>
#include <map>
#include <QDateTime>
#include <Processing.NDI.Lib.h>

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

public:
    static CollabRoom* instance();

    explicit CollabRoom(QString roomId, bool isServer, QWidget *parent = nullptr);
    ~CollabRoom();

    QString debugInfo();

signals:
    void onUpdatePeersUi(QList<PeerUi> peerUis);
    void onReconnectWebsocket();
    void onNdiSourcesUpdated(QStringList);
    void onRoomExit(QString);
    void onShareError(QString);
    void onFatalError(QString);
    void onRtcFailed(Peer*);

    void onNeedElevate();
    void onDxgiCaptureStatus(QString text);
    void onDowngradedToSharedMemory();

private slots:
    void updatePeersUi(QList<PeerUi> peerUis);
    void updateNdiSourcesUi(QStringList list);

    void copyRoomId();
    void exitRoom(QString reason);
    void setNick();
    void updateTurnServer();
    void toggleTurnVisible();

    void toggleShare();
    void startShare();
    void stopShare();
    void shareError(QString reason);
    void fatalError(QString reason);
    void rtcFailed(Peer* peer);

    void downgradedToSharedMemory();
    void dxgiCaptureStatus(QString text);
    void dxgiNeedElevate();

    void openSetting();
    void openBuyRelay();

    void toggleKeepTop();

private:
    QString errorToReadable(const QString& e);

    void connectWebsocket();
    void updatePeers(QJsonArray peers);

    void ndiShareWorkerClient();
    void ndiShareWorkerServer();
    void dxgiShareWorkerClient();
    void dxgiShareWorkerServer();
    void stopShareWorker();

    void ndiFindWorker();
    void ndiSendWorker();
    void dxgiSendWorker();

    void peerDataChannelMessage(std::unique_ptr<VtsMsg>, Peer* peer) const;

    void heartbeatUpdate();
    void usageStatUpdate();

    std::atomic_bool needFixVtsRatio = false;
    void tryFixVtsRatio(const std::shared_ptr<DxCapture>& cap);

private:
    Ui::CollabRoom *ui;

    bool isServer;
    bool useNdiSender;
    bool useNdiReceiver;
    QString roomId;
    QString peerId;

    QMutex wsLock;
    std::unique_ptr<rtc::WebSocket> ws;
    void wsSendAsync(const std::string& content);

    volatile std::atomic_bool exiting = false;

    FpsCounter outputFps;
    FpsCounter sendProcessFps;
    FpsCounter shareRecvFps;

    NatType localNatType;

    QMutex peersLock;
    // As server
    QString turnServer;
    std::map<QString, std::unique_ptr<Peer>> servers;
    std::unique_ptr<QTimer> heartbeat;
    std::unique_ptr<QTimer> usageStat;

    // As client
    std::unique_ptr<Peer> client;

    // ndi
    std::atomic_bool shareRunning = false;
    std::unique_ptr<QThread> shareThread;
    std::unique_ptr<QThread> ndiFindThread;

    // send
    std::atomic_int ndiSourceCount = 0;
    std::atomic<const NDIlib_source_t*> ndiSources;
    std::unique_ptr<QThread> frameSendThread;

    // d3d
    std::shared_ptr<DxToFrame> d3d;

    bool keepTop = false;
};

#endif // COLLABROOM_H
