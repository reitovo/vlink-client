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
#include "QDesktopServices"

BuyRelay::BuyRelay(CollabRoom *parent) :
        QDialog(parent), ui(new Ui::BuyRelay) {
    ui->setupUi(this);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);

    room = parent;
    relayQuality = parent->quality;
    refreshQuality();

    channel = grpc::CreateChannel("dns://119.29.29.29:53/" + VLINK_GRPC_RELAY_ENDPOINT, grpc::SslCredentials(
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
    connect(ui->openEvent, &QPushButton::clicked, this, [=]() {
        QDesktopServices::openUrl(QUrl("https://www.wolai.com/gX1EU9Zi2k4WvBnzH9kH9T"));
    });
    connect(ui->refundRelay, &QPushButton::clicked, this, [=, this]() {
        refundPrevious();
    });

    QSettings settings;
    auto previous = settings.value("previousRelayId").toString();
    ui->refundRelay->setEnabled(!previous.isEmpty());

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

        return true;
    }, this, [=, this]() {
        ui->priceText->setText(QString("%1￥").arg(price->price()));
        delete price;
    });
}

void BuyRelay::startWxPurchase() {
    auto person = ui->personSpin->value();
    auto hours = ui->hoursSpin->value();

    ui->startWx->setEnabled(false);

    runDetachedThenFinishOnUI<vts::relay::RspBuyQrCode>([=, this](auto res, auto status) {
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
        req.set_coupon(ui->coupon->text().toStdString());

        *status = service->StartBuyWeixin(&ctx, req, res);
    }, this, [=, this](auto res, auto status) {
        ui->startWx->setEnabled(true);

        if (!status->ok()) {
            qWarning() << "start purchase failed" << status->error_message().c_str();
            auto msg = status->error_message();
            if (msg == "room not found") {
                onFatalError(tr("购买失败"), tr("房间不存在，不支持在非官方房间服务器上创建中转服务器"));
            } else if (msg == "coupon not found") {
                onFatalError(tr("购买失败"), tr("优惠券不存在"));
            } else if (msg == "coupon no remain") {
                onFatalError(tr("购买失败"), tr("优惠券已使用完毕"));
            }
            return;
        }

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

        QSettings settings;
        settings.setValue("previousRelayId", id);
        settings.sync();
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

        return true;
    }, this, [=, this]() {
        auto status = QString::fromStdString(res->status());
        if (status == "creating" && ui->stacked->currentIndex() != 2) {
            ui->stacked->setCurrentIndex(2);
        } else if (status == "createfailed" || status == "refunding" || status == "refunded") {
            queryStatusTimer.stop();
            QMessageBox::critical(this, tr("创建中转服务器失败"), QString("%1\n%2")
                    .arg(tr("很抱歉，创建中转服务器失败，稍后将自动退款。")).arg(id));

            QSettings settings;
            settings.remove("previousRelayId");
            settings.sync();
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

void BuyRelay::refundPrevious() {
    QSettings settings;
    auto previous = settings.value("previousRelayId").toString();

    auto *res = new vts::server::RspCommon();
    grpc::ClientContext ctx;
    vts::relay::ReqRefund req;
    req.set_id(previous.toStdString());
    auto status = service->Refund(&ctx, req, res);

    if (!status.ok()) {
        qWarning() << "refund failed" << status.error_message().c_str();
        auto reason = tr("未知错误");
        switch (status.error_code()) {
            case grpc::StatusCode::NOT_FOUND:
                reason = tr("中转服务器不存在或已结束服务");
                break;
            case grpc::StatusCode::FAILED_PRECONDITION:
                reason = tr("已进行退款，请检查交易记录");
                break;
            case grpc::StatusCode::ALREADY_EXISTS:
                reason = tr("正在处理退款，请稍后再试");
                break;
            case grpc::StatusCode::OUT_OF_RANGE:
                reason = tr("很抱歉，已超出可退款期限");
                break;
        }

        QMessageBox::critical(this, tr("退款失败"), reason + "\n" + tr("如有疑问，请截图此窗口并加群反馈\n%1\n%2")
            .arg(previous).arg(status.error_message().c_str()));
        return;
    }

    settings.remove("previousRelayId");
    settings.sync();
    QMessageBox::information(this, tr("退款成功"), tr("退款成功，请稍后核实您的交易记录，并检查退款已到账"));
    refunded = true;
    close();
}

void BuyRelay::onFatalError(const QString &title, const QString &msg) {
    auto *box = new QMessageBox(this);
    box->setIcon(QMessageBox::Critical);
    box->setWindowTitle(title);
    box->setText(msg);
    box->addButton(tr("确定"), QMessageBox::NoRole);

    connect(box, &QMessageBox::finished, this, [=, this]() {
        box->deleteLater();
    });

    box->show();
}
