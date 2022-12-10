#ifndef COLLABROOM_H
#define COLLABROOM_H

#include <QDialog>
#include "qthread.h"
#include "rtc/rtc.hpp"
#include <QJsonObject>
#include <QJsonDocument>
#include <QMap>
#include <QDateTime>

#include <Processing.NDI.Lib.h>
#include <NatTypeProbe/NatProb.h>
#include "peer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace Ui {
class CollabRoom;
}

class CollabRoom : public QDialog
{
    Q_OBJECT
    friend class Peer;

public:
    explicit CollabRoom(QString roomId, bool isServer, QWidget *parent = nullptr);
    ~CollabRoom();

signals:
    void onUpdatePeersUi(QList<PeerUi> peerUis);
    void onReconnectWebsocket();
    void onNdiSourcesUpdated(QStringList);
    void onRoomExit(QString);
    void onNdiToFfmpegError(QString);

private slots:
    void updatePeersUi(QList<PeerUi> peerUis);
    void updateNdiSourcesUi(QStringList list);
    void updateNdiSourceIndex(int idx);

    void exitRoom(QString reason);
    void switchNdiStatus();
    void setNick();
    void handleNdiToFfmpegError(QString reason);

private:
    void connectWebsocket();
    void updatePeers(QJsonArray peers);
    void ndiToFfmpegWorker();

    void stopNdiToFfmpegWorker();

private:
    Ui::CollabRoom *ui;

    bool isServer;
    QString roomId;
    QString peerId;

    std::unique_ptr<rtc::WebSocket> ws;

    std::atomic_bool exiting = false;

    // As server
    QString turnServer;
    QMap<QString, std::shared_ptr<Peer>> servers;

    // As client
    std::shared_ptr<Peer> client;

    // ndi
    std::unique_ptr<QThread> ndiFindThread;
    std::atomic<const NDIlib_source_t*> ndiSources;
    int ndiSourceIdx = -1;
    atomic_int ndiSourceCount = 0;
    std::atomic_bool ndiToFfmpegRunning = false;
    std::unique_ptr<QThread> ndiToFfmpegThread;
};

#endif // COLLABROOM_H
