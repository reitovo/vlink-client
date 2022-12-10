#ifndef BLOCKINGHTTPREQUEST_H
#define BLOCKINGHTTPREQUEST_H

#include <QDialog>
#include <QMovie>
#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QNetworkReply>

namespace Ui {
class BlockingHttpRequest;
}

class BlockingHttpRequest : public QDialog
{
    Q_OBJECT

public:
    explicit BlockingHttpRequest(QWidget *parent = nullptr);
    ~BlockingHttpRequest();

    void getAsync(QString url, std::function<void(QNetworkReply*)> onFinished, bool release = true);

    void getJsonAsync(QString url, std::function<void(const QJsonObject&)> onJson,
                      std::function<void(QNetworkReply::NetworkError, QJsonParseError)> onFailed, bool release = true);

    void postJsonAsync(QString url, const QJsonDocument& data, std::function<void (const QJsonObject&)> onJson,
                                           std::function<void (QNetworkReply::NetworkError, QJsonParseError)> onFailed, bool release = true);

private:
    Ui::BlockingHttpRequest *ui;
    QPointer<QMovie> loadingGif;
    QPointer<QNetworkAccessManager> manager;
};

#endif // BLOCKINGHTTPREQUEST_H
