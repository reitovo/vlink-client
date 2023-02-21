#include "settingwindow.h"
#include "ui_settingwindow.h"
#include "collabroom.h"
#include "codec/frame_to_av.h"
#include "QSettings"

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

    connect(&debugGather, &QTimer::timeout, this, &SettingWindow::updateDebug);
    debugGather.start(100);

    auto encoders = FrameToAv::getEncoders();
    for (auto &e: encoders) {
        ui->encoders->addItem(e.readable);
    }

    ui->shouldForceEncoder->setChecked(settings.value("forceEncoder").toBool());
    ui->encoders->setCurrentText(settings.value("forceEncoderName").toString());
    ui->useNdiSender->setChecked(settings.value("useNdiSender").toBool());
    ui->useNdiReceiver->setChecked(settings.value("useNdiReceiver").toBool());
    ui->showDxgiWindow->setChecked(settings.value("showDxgiWindow").toBool());
    ui->forceShmem->setChecked(settings.value("forceShmem").toBool());
    ui->enableBuffering->setChecked(settings.value("enableBuffering").toBool());
    ui->avCQP->setValue(settings.value("avCQP", 27).toInt());
    ui->avCQPValue->setText(QString("%1").arg(settings.value("avCQP", 32).toInt()));

    connect(ui->encoders, &QComboBox::currentTextChanged, this, [=](const QString &s) {
        settings.setValue("forceEncoderName", s);
        settings.sync();
        qDebug() << "force encoder to" << s;
    });

    connect(ui->shouldForceEncoder, &QCheckBox::clicked, this, [=](bool v) {
        if (v) {
            settings.setValue("forceEncoderName", ui->encoders->currentText());
        }
        settings.setValue("forceEncoder", v);
        settings.sync();
        qDebug() << "force encoder" << v;
    });

    connect(ui->useNdiSender, &QCheckBox::clicked, this, [=](bool v) {
        settings.setValue("useNdiSender", v);
        settings.sync();
        qDebug() << "force use ndi sender" << v;
    });

    connect(ui->useNdiReceiver, &QCheckBox::clicked, this, [=](bool v) {
        settings.setValue("useNdiReceiver", v);
        settings.sync();
        qDebug() << "force use ndi receiver" << v;
    });

    connect(ui->showDxgiWindow, &QCheckBox::clicked, this, [=](bool v) {
        settings.setValue("showDxgiWindow", v);
        settings.sync();
        qDebug() << "show dxgi window" << v;
    });

    connect(ui->restoreIgnored, &QCheckBox::clicked, this, [=](bool v) {
        for (auto& i : settings.allKeys()) {
            if (i.startsWith("ignore"))
                settings.setValue(i, false);
        }
        settings.sync();
        qDebug() << "restore ignored";
    });

    connect(ui->actionSendLog, &QAction::triggered, this, [=]() {

    });

    connect(ui->actionCrash, &QAction::triggered, this, [=]() {
       throw std::exception("active crashed");
    });

    connect(ui->avCQP, &QSlider::valueChanged, this, [=](int v) {
        settings.setValue("avCQP", v);
        settings.sync();

        ui->avCQPValue->setText(QString("%1").arg(v));
    });

    connect(ui->enableBuffering, &QCheckBox::clicked, this, [=](bool v) {
        settings.setValue("enableBuffering", v);
        settings.sync();
        qDebug() << "enable buffering" << v;
    });

    connect(ui->forceShmem, &QCheckBox::clicked, this, [=](bool v) {
        settings.setValue("forceShmem", v);
        settings.sync();
        qDebug() << "force shmem" << v;
    });

    //TODO: 计算机\HKEY_CURRENT_USER\Software\Microsoft\DirectX\UserGpuPreferences
}
