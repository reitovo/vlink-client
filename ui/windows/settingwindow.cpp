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
    ui->showDxgiWindow->setChecked(settings.value("showDxgiWindow").toBool());

    connect(ui->encoders, &QComboBox::currentTextChanged, this, [&](const QString &s) {
        settings.setValue("forceEncoderName", s);
        settings.sync();
        qDebug() << "force encoder to" << s;
    });

    connect(ui->shouldForceEncoder, &QCheckBox::clicked, this, [&](bool v) {
        if (v) {
            settings.setValue("forceEncoderName", ui->encoders->currentText());
        }
        settings.setValue("forceEncoder", v);
        settings.sync();
        qDebug() << "force encoder" << v;
    });

    connect(ui->useNdiSender, &QCheckBox::clicked, this, [&](bool v) {
        settings.setValue("useNdiSender", v);
        settings.sync();
        qDebug() << "force use ndi sender" << v;
    });

    connect(ui->showDxgiWindow, &QCheckBox::clicked, this, [&](bool v) {
        settings.setValue("showDxgiWindow", v);
        settings.sync();
        qDebug() << "show dxgi window" << v;
    });
}
