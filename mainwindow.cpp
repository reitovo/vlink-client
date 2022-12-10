#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "collabroom.h"

#include <QTranslator>
#include <QPushButton>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);

    auto idleText = "v" VTSLINK_VERSION " @铃当Reito";
    ui->actionVersion->setText(idleText);

    connect(ui->actionExit, SIGNAL(triggered()), this, SLOT(actionExit()));

    connect(ui->actionLangZh, &QAction::triggered, this, [this]{ actionSetLang("zh_CN"); });
    connect(ui->actionLangEn, &QAction::triggered, this, [this]{ actionSetLang("en_US"); });
    connect(ui->actionLangJa, &QAction::triggered, this, [this]{ actionSetLang("ja_JP"); });

    connect(ui->btnJoinRoom, SIGNAL(clicked()), this, SLOT(joinRoom()));
    connect(ui->btnCreateRoom, SIGNAL(clicked()), this, SLOT(createRoom()));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::actionExit()
{
    close();
}

void MainWindow::joinRoom()
{
    auto roomId = ui->iptRoomId->text();
    qDebug("Join room id %s", qPrintable(roomId));

    auto c = new CollabRoom(roomId, false);
    c->show();
    hide();
    connect(c, &QDialog::finished, this, [=]() {
        show();
        c->deleteLater();
    });

//    auto* http = new BlockingHttpRequest(this);
//    http->getAsync("https://www.baidu.com", [=](QNetworkReply* reply) {
//        auto e = reply->readAll();
//        qDebug() << "Received";
//        http->deleteLater();
//    });
}

void MainWindow::createRoom()
{
    auto roomId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    qDebug() << "Create room id" << roomId;

    auto c = new CollabRoom(roomId, true);
    c->show();
    hide();
    connect(c, &QDialog::finished, this, [=]() {
        show();
        c->deleteLater();
    });
}

void MainWindow::actionSetLang(QString code)
{
    QTranslator translator;
    if (translator.load(":/i18n/VTSLink_" + code)) {
        qApp->installTranslator(&translator);
        ui->retranslateUi(this);
    }
}

