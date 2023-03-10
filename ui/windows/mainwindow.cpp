#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "collabroom.h"
#include "settingwindow.h"
#include "dx-capture.h"

#include <QTranslator>
#include <QPushButton>
#include <QDesktopServices>

static MainWindow *_instance;

MainWindow::MainWindow(QWidget *parent)
        : QMainWindow(parent), ui(new Ui::MainWindow) {
    _instance = this;
    ui->setupUi(this);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);

    auto idleText = "by @铃当Reito";
    ui->actionVersion->setText(idleText);

    ui->iptRoomId->setStyleSheet("QLineEdit[echoMode=\"2\"]{ lineedit-password-character: 42 }");


    const QStringList uiLanguages = QLocale::system().uiLanguages();
    qDebug() << "Languages" << uiLanguages.join(", ");

    connect(ui->actionExit, SIGNAL(triggered()), this, SLOT(actionExit()));

    connect(ui->actionLangZh, &QAction::triggered, this, [this] { actionSetLang("zh_CN"); });
    connect(ui->actionLangEn, &QAction::triggered, this, [this] { actionSetLang("en_US"); });
    connect(ui->actionLangJa, &QAction::triggered, this, [this] {
        tray->showMessage(tr("Not Translated Yet"), tr("Not translated yet, sorry for that."), tray->icon());
        //actionSetLang("ja-JP");
    });

    connect(ui->btnJoinRoom, SIGNAL(clicked()), this, SLOT(joinRoom()));
    connect(ui->btnCreateRoom, SIGNAL(clicked()), this, SLOT(createRoom()));
    connect(ui->actionOpenSetting, SIGNAL(triggered()), this, SLOT(openSetting()));

    connect(ui->actionLicense, &QAction::triggered, this, [=]() {
        QDesktopServices::openUrl(QUrl("https://www.wolai.com/reito/eQGuG4smx19LYTqTtmkKtn"));
    });

    tray = std::make_unique<QSystemTrayIcon>(this);
    QIcon icon;
    icon.addFile(QString::fromUtf8(":/images/icon.ico"), QSize(), QIcon::Normal, QIcon::Off);
    tray->setIcon(icon);
    tray->show();

    iterateCodec();
    iterateHwAccels();

    QSettings settings;
    auto lang = settings.value("languageCode", "").toString();
    if (lang.isEmpty()) {
        QStringList validLangCode{"zh_CN", "en_US"};
        for (QString locale: uiLanguages) {
            auto code = locale.replace("-", "_");
            if (validLangCode.contains(code)) {
                actionSetLang(code);
                qDebug() << "Using language" << code;
                break;
            }
        }
    } else {
        actionSetLang(lang);
    }

    dx_capture_load();
}

MainWindow::~MainWindow() {
    _instance = nullptr;
    tray->hide();

    dx_capture_unload();

    qDebug() << "destroy main window";
    delete ui;
    qDebug() << "bye";
}

MainWindow *MainWindow::instance() {
    return _instance;
}

void MainWindow::actionExit() {
    close();
}

void MainWindow::joinRoom() {
    auto roomId = ui->iptRoomId->text().trimmed();
    qDebug("Join room id %s", qPrintable(roomId));

    auto c = new CollabRoom(roomId, false);
    c->show();
    hide();
    connect(c, &QDialog::destroyed, this, [=]() {
        show();
    });

//    auto* http = new BlockingHttpRequest(this);
//    http->getAsync("https://www.baidu.com", [=](QNetworkReply* reply) {
//        auto e = reply->readAll();
//        qDebug() << "Received";
//        http->deleteLater();
//    });
}

void MainWindow::createRoom() {
    auto roomId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    qDebug() << "Create room id" << roomId;

    auto c = new CollabRoom(roomId, true);
    c->show();
    hide();
    connect(c, &QDialog::destroyed, this, [=]() {
        show();
    });
}

void MainWindow::actionSetLang(QString code) {
    qApp->removeTranslator(&translator);
    if (translator.load(":/i18n/VTSLink_" + code)) {
        qApp->installTranslator(&translator);
        ui->retranslateUi(this);

        QSettings settings;
        settings.setValue("languageCode", code);
        settings.sync();

        qDebug() << "set lang" << code;
    } else {
        qDebug() << "failed to load translator" << code;
    }
}

void MainWindow::iterateCodec() {
    void *iter = NULL;
    const AVCodec *codec = NULL;

    qDebug() << "Supported codecs";
    while ((codec = av_codec_iterate(&iter))) {
        if (av_codec_is_encoder(codec)) {
            if (codec->pix_fmts) {
                std::stringstream ss;
                ss << codec->name;
                ss << " enc: ";
                AVPixelFormat fmt;
                int idx = 0;
                while (true) {
                    fmt = codec->pix_fmts[idx++];
                    if (fmt == AV_PIX_FMT_NONE)
                        break;
                    ss << av_get_pix_fmt_name(fmt) << " ";
                }
                qDebug() << ss.str().c_str();
            }
        } else if (av_codec_is_decoder(codec)) {
            if (codec->pix_fmts) {
                std::stringstream ss;
                ss << codec->name;
                ss << " dec: ";
                AVPixelFormat fmt;
                int idx = 0;
                while (true) {
                    fmt = codec->pix_fmts[idx++];
                    if (fmt == AV_PIX_FMT_NONE)
                        break;
                    ss << av_get_pix_fmt_name(fmt) << " ";
                }
                qDebug() << ss.str().c_str();
            }
        }
    }
}

void MainWindow::iterateHwAccels() {
    qDebug() << "available hwaccels";
    AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
    while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE) {
        auto t = av_hwdevice_get_type_name(type);
        qDebug() << t;
    }
}

void MainWindow::openSetting() {
    auto w = new SettingWindow(this);
    w->show();
}
