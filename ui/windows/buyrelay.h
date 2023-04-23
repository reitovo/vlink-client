//
// Created by reito on 2023/1/7.
//

#ifndef VTSLINK_BUYRELAY_H
#define VTSLINK_BUYRELAY_H

#include <QDialog>
#include "QNetworkAccessManager"
#include "QPointer"
#include "QTimer"

#include "proto/relay.pb.h"
#include "proto/relay.grpc.pb.h"

QT_BEGIN_NAMESPACE
namespace Ui { class BuyRelay; }
QT_END_NAMESPACE

class CollabRoom;

class BuyRelay : public QDialog {
Q_OBJECT
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<vts::relay::RelayService::Stub> service;

public:
    explicit BuyRelay(CollabRoom *parent = nullptr);

    ~BuyRelay() override;

    std::optional<QString> getTurnServer();

    inline int getTurnHours() {
        return purchasedHours;
    }

    inline int getTurnMembers() {
        return purchasedMembers;
    }

private:
    CollabRoom *room;
    Ui::BuyRelay *ui;
    QPointer<QMovie> loadingGif;

    QString code;
    QString id;
    QTimer queryStatusTimer;
    std::optional<QString> turn;

    int purchasedHours;
    int purchasedMembers;

    void refreshPrice();
    void startWxPurchase();
    void queryStatus();
};

#endif //VTSLINK_BUYRELAY_H
