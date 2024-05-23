#include "settingwindow.h"
#include "ui_settingwindow.h"
#include "collabroom.h"
#include "codec/frame_to_av.h"
#include "QSettings"
#include "QFile"
#include "QNetworkReply"
#include "QMessageBox"
#include "QPushButton"
#include "QLineEdit"
#include "diag/diag.h"

SettingWindow::SettingWindow(QWidget *parent) :
        QMainWindow(parent),
        ui(new Ui::SettingWindow) {
    room = nullptr;
    init();
}

SettingWindow::SettingWindow(CollabRoom *parent) :
        QMainWindow(parent),
        ui(new Ui::SettingWindow) {
    room = parent;
    init();
}

SettingWindow::~SettingWindow() {
    delete ui;
}

void SettingWindow::updateDebug() {
    ui->debugInfo->setText(DebugCenter::debugInfo());
}

void SettingWindow::init() {
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);

    ui->tabWidget->setCurrentIndex(0);

    connect(&debugGather, &QTimer::timeout, this, &SettingWindow::updateDebug);
    debugGather.start(100);

    auto encoders = FrameToAv::getEncoders();
    for (auto &e: encoders) {
        ui->encoders->addItem(e.readable);
    }

    ui->shouldForceEncoder->setChecked(settings.value("forceEncoder").toBool());
    ui->encoders->setCurrentText(settings.value("forceEncoderName").toString());
    ui->useDxCapture->setChecked(settings.value("useDxCapture").toBool());
    ui->showDxgiWindow->setChecked(settings.value("showDxgiWindow").toBool());
    ui->forceShmem->setChecked(settings.value("forceShmem").toBool());
    ui->forceNoBuffering->setChecked(settings.value("forceNoBuffering").toBool());
    ui->disableIntraRefresh->setChecked(settings.value("disableIntraRefresh").toBool());
    ui->privateRoomServer->setText(settings.value("privateRoomServer").toString());
    ui->forceRelay->setChecked(settings.value("forceRelay").toBool());
    ui->enableAmfCompatible->setChecked(settings.value("enableAmfCompatible").toBool());
    ui->privateServerNoSsl->setChecked(settings.value("privateServerNoSsl").toBool());
    ui->enableSeparateSpout->setChecked(settings.value("enableSeparateSpout").toBool());

    connect(ui->privateServerNoSsl, &QCheckBox::clicked, this, [=, this](bool v) {
        settings.setValue("privateServerNoSsl", v);
        settings.sync();

        qDebug() << "privateServerNoSsl" << v;
    });

    connect(ui->enableAmfCompatible, &QCheckBox::clicked, this, [=, this](bool v) {
        settings.setValue("enableAmfCompatible", v);
        settings.sync();

        qDebug() << "enableAmfCompatible" << v;
    });

    connect(ui->forceRelay, &QCheckBox::clicked, this, [=, this](bool v) {
        settings.setValue("forceRelay", v);
        settings.sync();

        qDebug() << "forceRelay" << v;
    });

    connect(ui->disableIntraRefresh, &QCheckBox::clicked, this, [=, this](bool v) {
        settings.setValue("disableIntraRefresh", v);
        settings.sync();

        qDebug() << "disableIntraRefresh" << v;
    });

    connect(ui->encoders, &QComboBox::currentTextChanged, this, [=, this](const QString &s) {
        settings.setValue("forceEncoderName", s);
        settings.sync();
        qDebug() << "force encoder to" << s;
    });

    connect(ui->shouldForceEncoder, &QCheckBox::clicked, this, [=, this](bool v) {
        if (v) {
            settings.setValue("forceEncoderName", ui->encoders->currentText());
        }
        settings.setValue("forceEncoder", v);
        settings.sync();
        qDebug() << "force encoder" << v;
    });

    connect(ui->useDxCapture, &QCheckBox::clicked, this, [=, this](bool v) {
        settings.setValue("useDxCapture", v);
        settings.sync();
        qDebug() << "force use dx capture" << v;
    });

    connect(ui->showDxgiWindow, &QCheckBox::clicked, this, [=, this](bool v) {
        settings.setValue("showDxgiWindow", v);
        settings.sync();
        qDebug() << "show dxgi window" << v;
    });

    connect(ui->restoreIgnored, &QPushButton::clicked, this, [=, this](bool v) {
        for (auto &i: settings.allKeys()) {
            if (i.startsWith("ignore"))
                settings.setValue(i, false);
        }
        settings.sync();
        qDebug() << "restore ignored";

        QMessageBox::information(this, tr("提示"), tr("已恢复所有忽略项"));
    });

    connect(ui->setPrivateServer, &QPushButton::clicked, this, [=, this](bool v) {
        auto a = ui->privateRoomServer->text();

        settings.setValue("privateRoomServer", a);
        settings.sync();
        qDebug() << "private room server" << a;

        if (a.isEmpty()) {
            QMessageBox::information(this, tr("提示"), tr("已清空，将使用默认服务器，重新创建房间生效。"));
        } else {
            QMessageBox::information(this, tr("提示"), tr("已更新私有服务器地址：%0，重新创建房间生效。").arg(a) );
        }
    });

    connect(ui->buttonSendLog, &QPushButton::clicked, this, [=, this]() {
        diagnoseAll();

        QNetworkRequest req;
        req.setUrl(QUrl("https://misc.reito.fun/report"));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "plain/text");

        QFile outFile(VLINK_LOG_FILE);
        outFile.open(QIODevice::ReadOnly);
        auto arr = outFile.readAll();

        QNetworkReply *reply = networkAccessManager.post(req, arr);

        auto *box = new QMessageBox;
        box->setIcon(QMessageBox::Information);
        box->setWindowTitle(tr("正在上传"));
        box->setText(tr("请稍后"));
        auto ok = box->addButton(tr("确定"), QMessageBox::NoRole);
        auto cancel = box->addButton(tr("取消"), QMessageBox::NoRole);
        ok->setEnabled(false);

        connect(reply, &QNetworkReply::uploadProgress, this, [=](qint64 sent, qint64 total) {
            box->setText(tr("请稍后，已上传 %1/%2").arg(sent).arg(total));
        });

        connect(reply, &QNetworkReply::finished, this, [=]() {
            ok->setEnabled(true);
            cancel->setEnabled(false);
            box->setWindowTitle(tr("上传完成"));
            if (reply->error() != QNetworkReply::NetworkError::NoError) {
                box->setText(reply->errorString());
            } else {
                box->setText(tr("上传成功"));
            }
            reply->deleteLater();
        });

        connect(box, &QMessageBox::finished, this, [=]() {
            auto ret = dynamic_cast<QPushButton *>(box->clickedButton());
            if (ret == cancel) {
                reply->abort();
            }
            box->deleteLater();
        });

        box->show();
    });

    connect(ui->actionCrash, &QAction::triggered, this, [=]() {
        throw std::exception("active crashed");
    });

    connect(ui->forceNoBuffering, &QCheckBox::clicked, this, [=, this](bool v) {
        settings.setValue("forceNoBuffering", v);
        settings.sync();
        qDebug() << "force no buffering" << v;
    });

    connect(ui->forceShmem, &QCheckBox::clicked, this, [=, this](bool v) {
        settings.setValue("forceShmem", v);
        settings.sync();
        qDebug() << "force shmem" << v;
    });

    connect(ui->enableSeparateSpout, &QCheckBox::clicked, this, [=, this](bool v) {
        settings.setValue("enableSeparateSpout", v);
        settings.sync();
        qDebug() << "enableSeparateSpout" << v;
    });

    //TODO: 计算机\HKEY_CURRENT_USER\Software\Microsoft\DirectX\UserGpuPreferences
}
