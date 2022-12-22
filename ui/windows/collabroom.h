#ifndef COLLABROOM_H
#define COLLABROOM_H

#include "core/peer.h"
#include <QDialog>
#include "av_to_d3d.h"
#include "QMutex"
#include "QThread"
#include "rtc/rtc.hpp"
#include <QJsonObject>
#include <QJsonDocument>
#include <map>
#include <QDateTime>

#include <Processing.NDI.Lib.h>
#include "vts.pb.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace Ui {
class CollabRoom;
}

// This is the collab main logic
class CollabRoom : public QDialog, public IDebugCollectable
{
    Q_OBJECT
    friend class Peer;
    friend class PeerItemWidget;

public:
    explicit CollabRoom(QString roomId, bool isServer, QWidget *parent = nullptr);
    ~CollabRoom();

    QString debugInfo();

signals:
    void onUpdatePeersUi(QList<PeerUi> peerUis);
    void onReconnectWebsocket();
    void onNdiSourcesUpdated(QStringList);
    void onRoomExit(QString);
    void onNdiToFfmpegError(QString);
    void onFatalError(QString);

private slots:
    void updatePeersUi(QList<PeerUi> peerUis);
    void updateNdiSourcesUi(QStringList list);
    void updateNdiSourceIndex(int idx);

    void copyRoomId();
    void exitRoom(QString reason);
    void setNick();
    void updateTurnServer();
    void toggleTurnVisible();

    void toggleNdiToFfmpeg();
    void startNdiToFfmpeg();
    void stopNdiToFfmpeg();
    void ndiToFfmpegError(QString reason);
    void fatalError(QString reason);

    void openSetting();

private:
    void connectWebsocket();
    void updatePeers(QJsonArray peers);

    void ndiToFfmpegWorkerClient();
    void ndiToFfmpegWorkerServer();
    void stopNdiToFfmpegWorker();

    void ndiFindWorker();
    void ndiSendWorker();

    void peerDataChannelMessage(std::unique_ptr<VtsMsg>, Peer* peer);

private:
    Ui::CollabRoom *ui;

    bool isServer;
    QString roomId;
    QString peerId;

    QMutex wsLock;
    std::unique_ptr<rtc::WebSocket> ws;
    void wsSendAsync(std::string content);

    std::atomic_bool exiting = false;

    QMutex peersLock;
    // As server
    QString turnServer;
    std::map<QString, std::unique_ptr<Peer>> servers;
    std::unique_ptr<QTimer> heartbeat;

    // As client
    std::unique_ptr<Peer> client;

    // ndi
    std::unique_ptr<QThread> ndiFindThread;
    std::atomic<const NDIlib_source_t*> ndiSources;
    int ndiSourceIdx = -1;
    std::atomic_int ndiSourceCount = 0;
    std::atomic_bool ndiToFfmpegRunning = false;
    std::unique_ptr<QThread> ndiToFfmpegThread;
    std::unique_ptr<QThread> ndiSendThread;

    // d3d
    std::shared_ptr<DxToNdi> d3d;
};

#endif // COLLABROOM_H
