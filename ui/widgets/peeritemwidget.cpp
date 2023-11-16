#include "peeritemwidget.h"
#include "qtimer.h"
#include "ui_peeritemwidget.h"
#include "ui/windows/collabroom.h"

PeerItemWidget::PeerItemWidget(CollabRoom *room) :
        QWidget(room),
        ui(new Ui::PeerItemWidget) {
    ui->setupUi(this);
    this->room = room;

    refreshTimer = std::make_unique<QTimer>(this);
    connect(refreshTimer.get(), SIGNAL(timeout()), this, SLOT(updateStats()));
    refreshTimer->start(200);
}

PeerItemWidget::~PeerItemWidget() {
    delete ui;
}

void PeerItemWidget::updatePeer(const vts::server::Peer &p) {
    ui->natType->setText(getNatTypeString((NatType) p.nattype()));

    auto self = p.peerid() == room->localPeerId.toStdString();
    auto qPeerId = QString::fromStdString(p.peerid());
    auto qNick = QString::fromStdString(p.nick());
    auto nick = (qNick.isEmpty() ? tr("用户") + qPeerId.first(4) : qNick) + (self ? tr(" (你)") : "");
    if (p.isserver()) {
        nick += tr(" (房主)");
    }
    ui->nick->setText(nick);

    if (p.rtt() == 0) {
        ui->rtt->clear();
    } else {
        ui->rtt->setText(QString("%1ms").arg(p.rtt()));
    }

    peerId = qPeerId;
}

void PeerItemWidget::updateStats() {
    auto self = peerId == room->localPeerId;

    Peer *peer = nullptr;
    ScopedQMutex _(&room->peersLock);
    if (room->isServer && room->clientPeers.contains(peerId)) {
        peer = room->clientPeers[peerId].get();
    } else if (!room->isServer && room->localPeerId == peerId) {
        peer = room->serverPeer.get();
    }

    if (peer == nullptr) {
        ui->conn->setText(self ? tr("本机") : QString());
        ui->bytes->clear();
    } else {
        ui->conn->setText(peer->connected() ? peer->usingTurn() ? tr("已中转") : tr("已连接") : tr("连接中"));
        ui->bytes->setText(peer->dataStats());
    }
}

QString PeerItemWidget::getNatTypeString(NatType type) {
    QString natType;
    switch (type) {
        default: {
            natType = tr("NAT ?");
            break;
        }
        case NatType::StunTypeUnknown: {
            natType = tr("NAT ?");
            break;
        }
        case NatType::StunTypePortRestrictedNat: {
            natType = tr("NAT C");
            break;
        }
        case NatType::StunTypeConeNat: {
            natType = tr("NAT B");
            break;
        }
        case NatType::StunTypeRestrictedNat: {
            natType = tr("NAT B");
            break;
        }
        case NatType::StunTypeSymNat: {
            natType = tr("NAT D");
            break;
        }
        case NatType::StunTypeFailure: {
            natType = tr("NAT ?");
            break;
        }
        case NatType::StunTypeBlocked: {
            natType = tr("NAT F");
            break;
        }
        case NatType::StunTypeOpen: {
            natType = tr("NAT A");
            break;
        }
    }
    return natType;
}
