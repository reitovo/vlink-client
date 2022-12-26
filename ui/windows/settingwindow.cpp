#include "settingwindow.h"
#include "ui_settingwindow.h"
#include "collabroom.h"
#include "codec/ndi_to_av.h"
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

    auto encoders = NdiToAv::getEncoders();
    for (auto &e: encoders) {
        ui->encoders->addItem(e.readable);
    }

    if (settings.contains("forceEncoder")) {
        ui->shouldForceEncoder->setChecked(settings.value("forceEncoder").toBool());
    }

    if (settings.contains("forceEncoderName")) {
        ui->encoders->setCurrentText(settings.value("forceEncoderName").toString());
    }

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
}
