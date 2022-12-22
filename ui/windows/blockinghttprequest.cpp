#include "blockinghttprequest.h"
#include "ui_blockinghttprequest.h"

#include <QNetworkReply>
#include <QNetworkRequest>

BlockingHttpRequest::BlockingHttpRequest(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::BlockingHttpRequest)
{
    ui->setupUi(this);

    loadingGif = new QMovie(":/images/loading.gif");
    ui->gif->setMovie(loadingGif);
    loadingGif->start();

    manager = new QNetworkAccessManager(this);
    show();
}

BlockingHttpRequest::~BlockingHttpRequest()
{
    loadingGif->stop();
    close();
    delete ui;
}

void BlockingHttpRequest::getAsync(QString url, std::function<void(QNetworkReply*)> onFinished, bool release)
{
    QNetworkRequest req;
    req.setUrl(QUrl(url));

    QNetworkReply* reply = manager->get(req);

    connect(reply, &QNetworkReply::finished, this, [=]() {
        onFinished(reply);
        reply->deleteLater();
        if (release) {
            deleteLater();
        }
    });
}

void BlockingHttpRequest::getJsonAsync(QString url, std::function<void (const QJsonObject&)> onJson,
                                       std::function<void (QNetworkReply::NetworkError, QJsonParseError)> onFailed, bool release)
{
    QNetworkRequest req;
    req.setUrl(QUrl(url));

    QNetworkReply* reply = manager->get(req);

    connect(reply, &QNetworkReply::finished, this, [=]() {
        auto error = reply->error();
        if (error != QNetworkReply::NetworkError::NoError) {
            if (onFailed != nullptr)
                onFailed(error,  QJsonParseError());
        } else {
            QJsonParseError jsonError;
            auto document = QJsonDocument::fromJson(reply->readAll(), &jsonError);
            if (jsonError.error != QJsonParseError::NoError) {
                if (onFailed != nullptr)
                    onFailed(reply->error(), jsonError);
            } else {
                if (onJson != nullptr)
                    onJson(document.object());
            }
        }
        reply->deleteLater();
        if (release) {
            deleteLater();
        }
    });
}

void BlockingHttpRequest::postJsonAsync(QString url, const QJsonDocument& data, std::function<void (const QJsonObject&)> onJson,
                                       std::function<void (QNetworkReply::NetworkError, QJsonParseError)> onFailed, bool release)
{
    QNetworkRequest req;
    req.setUrl(QUrl(url));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = manager->post(req, data.toJson());

    connect(reply, &QNetworkReply::finished, this, [=]() {
        auto error = reply->error();
        if (error != QNetworkReply::NetworkError::NoError) {
            if (onFailed != nullptr)
                onFailed(error,  QJsonParseError());
        } else {
            QJsonParseError jsonError;
            auto document = QJsonDocument::fromJson(reply->readAll(), &jsonError);
            if (jsonError.error != QJsonParseError::NoError) {
                if (onFailed != nullptr)
                    onFailed(reply->error(), jsonError);
            } else {
                if (onJson != nullptr)
                    onJson(document.object());
            }
        }
        reply->deleteLater();
        if (release) {
            deleteLater();
        }
    });
}
