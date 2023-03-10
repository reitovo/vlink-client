//
// Created by reito on 2023/1/7.
//

#ifndef VTSLINK_BUYRELAY_H
#define VTSLINK_BUYRELAY_H

#include <QDialog>
#include "QNetworkAccessManager"
#include "QPointer"
#include "QTimer"

QT_BEGIN_NAMESPACE
namespace Ui { class BuyRelay; }
QT_END_NAMESPACE

class BuyRelay : public QDialog {
Q_OBJECT

public:
    explicit BuyRelay(QWidget *parent = nullptr);

    ~BuyRelay() override;

    std::optional<QString> getTurnServer();
    inline int getTurnHours() {
        return purchasedHours;
    }
    inline int getTurnMembers() {
        return purchasedMembers;
    }

private:
    Ui::BuyRelay *ui;
    QPointer<QNetworkAccessManager> manager;
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
