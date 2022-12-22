
#include "dependency/NatTypeProbe/include/NatTypeProbe/NatProb.h"
#include "peeritemwidget.h"
#include "qtimer.h"
#include "ui_peeritemwidget.h"
#include "ui/windows/collabroom.h"

PeerItemWidget::PeerItemWidget(QWidget *parent, CollabRoom* room) :
    QWidget(parent),
    ui(new Ui::PeerItemWidget)
{
    ui->setupUi(this);
    this->room = room;

    refreshTimer = std::make_unique<QTimer>(this);
    connect(refreshTimer.get(), SIGNAL(timeout()), this, SLOT(updateStats()));
    refreshTimer->start(200);
}

PeerItemWidget::~PeerItemWidget()
{
    delete ui;
}

void PeerItemWidget::setPeerUi(PeerUi p)
{
    QString natType = QString(CNatProb::DescribeNatType(p.nat).c_str());
    switch (p.nat) {
    default: {
        natType = tr("正在获取 NAT");
        break;
    }
    case NatType::StunTypeUnknown: {
        natType = tr("未知 NAT");
        break;
    }
    case NatType::StunTypePortRestrictedNat: {
        natType = tr("端口受限 NAT");
        break;
    }
    case NatType::StunTypeConeNat: {
        natType = tr("全锥 NAT");
        break;
    }
    case NatType::StunTypeRestrictedNat: {
        natType = tr("受限 NAT");
        break;
    }
    case NatType::StunTypeSymNat: {
        natType = tr("对称 NAT");
        break;
    }
    case NatType::StunTypeFailure: {
        natType = tr("无法获取 NAT");
        break;
    }
    case NatType::StunTypeBlocked: {
        natType = tr("UDP 阻断");
        break;
    }
    case NatType::StunTypeOpen: {
        natType = tr("公网 IP");
        break;
    }
    }
    ui->natType->setText(natType);

    auto self = p.peerId == room->peerId;
    auto nick = (p.nick.isEmpty() ? tr("用户") + p.peerId.first(4) : p.nick) + (self ? tr(" (你)") : "");
    if (p.isServer) {
        nick += tr(" (房主)");
    }
    ui->nick->setText(nick);

    if (p.rtt == 0) {
        ui->rtt->clear();
    } else {
        ui->rtt->setText(QString("%1ms").arg(p.rtt));
    }

    peerUi = p;
}

void PeerItemWidget::updateStats()
{
    if (!peerUi.has_value())
        return;

    auto& p = peerUi.value();
    auto self = p.peerId == room->peerId;

    Peer* peer = nullptr;
    room->peersLock.lock();
    if (room->isServer && room->servers.contains(p.peerId)) {
        peer = room->servers[p.peerId].get();
    } else if (!room->isServer && room->peerId == p.peerId) {
        peer = room->client.get();
    }

    if (peer == nullptr) {
        ui->conn->setText(self ? tr("本机") : QString());
        ui->bytes->clear();
    } else {
        ui->conn->setText(peer->connected() ? peer->usingTurn() ? tr("已中转") : tr("已连接") : tr("连接中"));
        ui->bytes->setText(peer->dataStats());
    }
    room->peersLock.unlock();
}
