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

BuyRelay::BuyRelay(QWidget *parent) :
        QDialog(parent), ui(new Ui::BuyRelay) {
    ui->setupUi(this);

    manager = new QNetworkAccessManager(this);

    connect(ui->hoursSpin, &QSpinBox::valueChanged, this, [=](auto val) {
        refreshPrice();
    });
    connect(ui->personSpin, &QSpinBox::valueChanged, this, [=](auto val) {
        refreshPrice();
    });
    connect(ui->startWx, &QPushButton::clicked, this, [=]() {
       startWxPurchase();
    });
    connect(&queryStatusTimer, &QTimer::timeout, this, [=](){
        queryStatus();
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

    QNetworkRequest req;
    req.setUrl(QUrl(QString(VTSLINK_HTTP_BASEURL "/api/relay/buy/price/%1/%2").arg(person).arg(hours)));

    QNetworkReply* reply = manager->get(req);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        auto error = reply->error();
        if (error != QNetworkReply::NetworkError::NoError) {
            qWarning() << "request price failed" << error;
        } else {
            QJsonParseError jsonError;
            auto content = reply->readAll();
            auto document = QJsonDocument::fromJson(content, &jsonError);
            if (jsonError.error != QJsonParseError::NoError) {
                qWarning() << "parse json failed" << jsonError.errorString();
            } else {
                ui->priceText->setText(QString("%1￥").arg(document.object()["price"].toDouble()));
            }
        }
        reply->deleteLater();
    });
}

void BuyRelay::startWxPurchase() {
    auto person = ui->personSpin->value();
    auto hours = ui->hoursSpin->value();

    QNetworkRequest req;
    req.setUrl(QUrl(QString(VTSLINK_HTTP_BASEURL "/api/relay/buy/wx/start/%1/%2").arg(person).arg(hours)));

    ui->startWx->setEnabled(false);

    QNetworkReply* reply = manager->get(req);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        ui->startWx->setEnabled(true);
        auto error = reply->error();
        if (error != QNetworkReply::NetworkError::NoError) {
            qWarning() << "start purchase failed" << error;
        } else {
            QJsonParseError jsonError;
            auto content = reply->readAll();
            auto document = QJsonDocument::fromJson(content, &jsonError);
            if (jsonError.error != QJsonParseError::NoError) {
                qWarning() << "parse json failed" << jsonError.errorString();
            } else {
                auto obj = document.object();
                code = obj["code"].toString();
                id = obj["id"].toString();
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
                pix.save("temp.png");
                ui->qrCode->setPixmap(pix);
                ui->stacked->setCurrentIndex(1);
                queryStatusTimer.start();
            }
        }
        reply->deleteLater();
    });
}

void BuyRelay::queryStatus() {
    QNetworkRequest req;
    req.setUrl(QUrl(QString(VTSLINK_HTTP_BASEURL "/api/relay/buy/status/%1").arg(id)));

    QNetworkReply* reply = manager->get(req);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        auto error = reply->error();
        if (error != QNetworkReply::NetworkError::NoError) {
            qWarning() << "get status failed" << error;
        } else {
            QJsonParseError jsonError;
            auto content = reply->readAll();
            auto document = QJsonDocument::fromJson(content, &jsonError);
            if (jsonError.error != QJsonParseError::NoError) {
                qWarning() << "parse json failed" << jsonError.errorString();
            } else {
                auto obj = document.object();
                auto status = obj["status"].toString();
                if (status == "creating" && ui->stacked->currentIndex() != 2) {
                    ui->stacked->setCurrentIndex(2);
                } else if (status == "createfailed" || status == "refunding" || status == "refunded") {
                    queryStatusTimer.stop();
                    QMessageBox::critical(this, tr("创建中转服务器失败"), QString("%1\n%2")
                    .arg(tr("很抱歉，创建中转服务器失败，稍后将自动退款。")).arg(id));
                    close();
                } else if (status == "complete") {
                    queryStatusTimer.stop();
                    turn = QString("%1:%2@%3").arg(obj["username"].toString())
                            .arg(obj["password"].toString()).arg(obj["ip"].toString());
                    purchasedMembers = ui->personSpin->value();
                    purchasedHours = ui->hoursSpin->value();
                    MainWindow::instance()->tray->showMessage(tr("创建成功"), tr("请等待自动重新连接"),
                                                              MainWindow::instance()->tray->icon());
                    close();
                }
            }
        }
        reply->deleteLater();
    });
}

std::optional<QString> BuyRelay::getTurnServer() {
    return turn;
}
