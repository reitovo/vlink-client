//
// Created by reito on 2023/4/12.
//

// You may need to build the project (run Qt uic code generator) to get "ui_FrameQuality.h" resolved

#include "framequality.h"
#include "ui_FrameQuality.h"
#include "collabroom.h"

FrameQuality::FrameQuality(CollabRoom *parent) :
        QDialog(parent), ui(new Ui::FrameQuality) {
    ui->setupUi(this);

    this->room = parent;

    ui->frameQuality->setCurrentIndex(settings.value("frameQualityIdx", 0).toInt());
    ui->frameRate->setCurrentIndex(settings.value("frameRateIdx", 1).toInt());
    ui->frameSize->setCurrentIndex(settings.value("frameSizeIdx", 2).toInt());

    connect(ui->frameQuality, &QComboBox::currentIndexChanged, this, [=, this](int v) {
        changed = true;
        settings.setValue("frameQualityIdx", v);

        room->frameQuality = v;

        settings.sync();
        qDebug() << "frameQualityIdx" << v;
    });

    connect(ui->frameRate, &QComboBox::currentIndexChanged, this, [=, this](int v) {
        changed = true;
        settings.setValue("frameRateIdx", v);

        switch (v) {
            case 0:
                room->frameRate = 30;
                break;
            case 1:
                room->frameRate = 60;
                break;
        }

        settings.setValue("frameRate", room->frameRate);
        settings.sync();
        qDebug() << "frameRateIdx" << v;
    });

    connect(ui->frameSize, &QComboBox::currentIndexChanged, this, [=, this](int v) {
        changed = true;
        settings.setValue("frameSizeIdx", v);

        switch (v) {
            case 0:
                room->frameWidth = 1280;
                room->frameHeight = 720;
                break;
            case 1:
                room->frameWidth = 1600;
                room->frameHeight = 900;
                break;
            case 2:
                room->frameWidth = 1920;
                room->frameHeight = 1080;
                break;
        }

        settings.setValue("frameWidth", room->frameWidth);
        settings.setValue("frameHeight", room->frameHeight);
        settings.sync();
        qDebug() << "frameSizeIdx" << v;
    });
}

FrameQuality::~FrameQuality() {
    delete ui;
}
