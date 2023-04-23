#ifndef PEERITEMWIDGET_H
#define PEERITEMWIDGET_H

#include <QWidget>
#include "ui/windows/collabroom.h"

namespace Ui {
    class PeerItemWidget;
}

class CollabRoom;

class PeerItemWidget : public QWidget {
Q_OBJECT
    QString peerId;

public:
    explicit PeerItemWidget(CollabRoom *room);
    ~PeerItemWidget() override;

    void updatePeer(const vts::server::Peer &p);
    static QString getNatTypeString(NatType type);

private slots:
    void updateStats();

private:
    Ui::PeerItemWidget *ui;
    CollabRoom *room;
    std::unique_ptr<QTimer> refreshTimer;
};

#endif // PEERITEMWIDGET_H
