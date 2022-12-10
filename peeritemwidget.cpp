#include "peeritemwidget.h"
#include "ui_peeritemwidget.h"
#include "collabroom.h"
#include <NatTypeProbe/NatProb.h>

PeerItemWidget::PeerItemWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PeerItemWidget)
{
    ui->setupUi(this);
}

PeerItemWidget::~PeerItemWidget()
{
    delete ui;
}

void PeerItemWidget::setPeerUi(PeerUi p, bool self)
{
    ui->nick->setText((p.nick.isEmpty() ? tr("用户") + p.id.first(4) : p.nick) + (self ? tr(" (你)") : ""));

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
    ui->rtt->setText(QString("%1ms").arg(p.rtt));
}
