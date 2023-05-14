//
// Created by reito on 2023/1/7.
//

// You may need to build the project (run Qt uic code generator) to get "ui_BuyRelay.h" resolved

#include "buyrelay.h"
#include "ui_BuyRelay.h"
#include "core/vtslink.h"
#include "QNetworkReply"
#include "QDebug"
#include "QJsonParseError"
#include "QJsonObject"
#include "qrencode.h"
#include "mainwindow.h"
#include <QPaintEngine>
#include <QMessageBox>
#include <QMovie>
#include "grpc++/grpc++.h"
#include "core/grpc_cert.h"
#include "core/util.h"
#include "collabroom.h"
#include "ui/windows/framequality.h"

BuyRelay::BuyRelay(CollabRoom *parent) :
        QDialog(parent), ui(new Ui::BuyRelay) {
    ui->setupUi(this);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);

    room = parent;
    relayQuality = parent->quality;
    refreshQuality();

    channel = grpc::CreateChannel(VLINK_GRPC_RELAY_ENDPOINT, grpc::SslCredentials(
            grpc::SslCredentialsOptions(ISRG_Root_X1, "", "")));
    service = vts::relay::RelayService::NewStub(channel);

    connect(ui->hoursSpin, &QSpinBox::valueChanged, this, [=, this](auto val) {
        refreshPrice();
    });
    connect(ui->personSpin, &QSpinBox::valueChanged, this, [=, this](auto val) {
        refreshPrice();
    });
    connect(ui->startWx, &QPushButton::clicked, this, [=, this]() {
        startWxPurchase();
    });
    connect(&queryStatusTimer, &QTimer::timeout, this, [=, this]() {
        queryStatus();
    });
    connect(ui->setFrameQualityButton, &QPushButton::clicked, this, [=, this]() {
        changeQuality();
    });

    refreshPrice();
    ui->stacked->setCurrentIndex(0);
    queryStatusTimer.setInterval(1000);

    loadingGif = new QMovie(":/images/loading.gif");
    ui->gif->setMovie(loadingGif);
    loadingGif->start();
}

BuyRelay::~BuyRelay() {
    delete ui;
}

void BuyRelay::refreshPrice() {
    auto person = ui->personSpin->value();
    auto hours = ui->hoursSpin->value();

    auto *price = new vts::relay::RspPrice();
    runDetached([=, this]() {
        grpc::ClientContext ctx;
        vts::relay::ReqRelayCreate req;
        req.set_participants(person);
        req.set_hours(hours);
        auto quality = req.mutable_maxquality();
        quality->set_framerate(relayQuality.frameRate);
        quality->set_framequality(relayQuality.frameQuality);
        quality->set_framewidth(relayQuality.frameWidth);
        quality->set_frameheight(relayQuality.frameHeight);
        req.set_roomid(room->roomId.toStdString());

        auto status = service->QueryPrice(&ctx, req, price);

        if (!status.ok()) {
            qWarning() << "query price failed" << status.error_message().c_str();
        }
    }, this, [=, this]() {
        ui->priceText->setText(QString("%1￥").arg(price->price()));
        delete price;
    });
}

void BuyRelay::startWxPurchase() {
    auto person = ui->personSpin->value();
    auto hours = ui->hoursSpin->value();

    ui->startWx->setEnabled(false);

    auto *res = new vts::relay::RspBuyQrCode();
    runDetached([=, this]() {
        grpc::ClientContext ctx;
        vts::relay::ReqRelayCreate req;
        req.set_participants(person);
        req.set_hours(hours);
        auto quality = req.mutable_maxquality();
        quality->set_framerate(relayQuality.frameRate);
        quality->set_framequality(relayQuality.frameQuality);
        quality->set_framewidth(relayQuality.frameWidth);
        quality->set_frameheight(relayQuality.frameHeight);
        req.set_roomid(room->roomId.toStdString());

        auto status = service->StartBuyWeixin(&ctx, req, res);

        if (!status.ok()) {
            qWarning() << "start purchase failed" << status.error_message().c_str();
        }
    }, this, [=, this]() {
        ui->startWx->setEnabled(true);

        code = QString::fromStdString(res->code());
        id = QString::fromStdString(res->id());
        qDebug() << "start purchase" << code << id;

        auto qr = QRcode_encodeString(code.toStdString().c_str(), 0, QRecLevel::QR_ECLEVEL_M, QRencodeMode::QR_MODE_8, 1);

        auto zoom = 4;
        QImage image(qr->width * zoom, qr->width * zoom, QImage::Format_ARGB32);
        for (int r = 0; r < qr->width * zoom; ++r) {
            for (int c = 0; c < qr->width * zoom; ++c) {
                auto b = qr->data[(r / zoom) * qr->width + (c / zoom)] & 0b1;
                image.setPixelColor(c, r,
                                    b ? QColor::fromRgb(0, 0, 0) : QColor::fromRgb(255, 255, 255));
            }
        }

        QRcode_free(qr);

        auto pix = QPixmap::fromImage(image);
        ui->qrCode->setPixmap(pix);
        ui->stacked->setCurrentIndex(1);
        queryStatusTimer.start();

        delete res;
    });
}

void BuyRelay::queryStatus() {
    auto *res = new vts::relay::RspBuyStatus();
    runDetached([=, this]() {
        grpc::ClientContext ctx;
        vts::relay::ReqBuyStatus req;
        req.set_id(id.toStdString());
        auto status = service->QueryStatus(&ctx, req, res);

        if (!status.ok()) {
            qWarning() << "get status failed" << status.error_message().c_str();
        }
    }, this, [=, this]() {
        auto status = QString::fromStdString(res->status());
        if (status == "creating" && ui->stacked->currentIndex() != 2) {
            ui->stacked->setCurrentIndex(2);
        } else if (status == "createfailed" || status == "refunding" || status == "refunded") {
            queryStatusTimer.stop();
            QMessageBox::critical(this, tr("创建中转服务器失败"), QString("%1\n%2")
                    .arg(tr("很抱歉，创建中转服务器失败，稍后将自动退款。")).arg(id));
            close();
        } else if (status == "complete") {
            queryStatusTimer.stop();
            turn = QString("%1:%2@%3").arg(res->username().c_str())
                    .arg(res->password().c_str()).arg(res->ip().c_str());
            purchasedMembers = ui->personSpin->value();
            purchasedHours = ui->hoursSpin->value();
            MainWindow::instance()->tray->showMessage(tr("创建成功"), tr("请等待自动重新连接"),
                                                      MainWindow::instance()->tray->icon());
            close();
        }
    });
}

std::optional<QString> BuyRelay::getTurnServer() {
    return turn;
}

void BuyRelay::changeQuality() {
    auto *f = new FrameQuality(relayQuality, this);

    connect(f, &FrameQuality::finished, this, [=, this]() {
        if (f->changed) {
            qDebug() << "relay frame quality" << f->quality;

            relayQuality = f->quality;
            refreshPrice();
            refreshQuality();
        }
        f->deleteLater();
    });

    f->show();
}

void BuyRelay::refreshQuality() {
    ui->frameFormat->setText(getFrameFormatDesc(relayQuality));
}
