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

    frameQuality = parent->frameQuality;
    frameRate = parent->frameRate;
    frameWidth = parent->frameWidth;
    frameHeight = parent->frameHeight;

    ui->frameQuality->setCurrentIndex(settings.value("frameQualityIdx", 0).toInt());
    ui->frameRate->setCurrentIndex(settings.value("frameRateIdx", 1).toInt());
    ui->frameSize->setCurrentIndex(settings.value("frameSizeIdx", 2).toInt());

    connect(ui->frameQuality, &QComboBox::currentIndexChanged, this, [=, this](int v) {
        changed = true;
        settings.setValue("frameQualityIdx", v);

        frameQuality = v;

        settings.sync();
        qDebug() << "frameQualityIdx" << v;
    });

    connect(ui->frameRate, &QComboBox::currentIndexChanged, this, [=, this](int v) {
        changed = true;
        settings.setValue("frameRateIdx", v);

        switch (v) {
            case 0:
                frameRate = 30;
                break;
            case 1:
                frameRate = 60;
                break;
        }

        settings.setValue("frameRate", frameRate);
        settings.sync();
        qDebug() << "frameRateIdx" << v;
    });

    connect(ui->frameSize, &QComboBox::currentIndexChanged, this, [=, this](int v) {
        changed = true;
        settings.setValue("frameSizeIdx", v);

        switch (v) {
            case 0:
                frameWidth = 1280;
                frameHeight = 720;
                break;
            case 1:
                frameWidth = 1600;
                frameHeight = 900;
                break;
            case 2:
                frameWidth = 1920;
                frameHeight = 1080;
                break;
        }

        settings.setValue("frameWidth", frameWidth);
        settings.setValue("frameHeight", frameHeight);
        settings.sync();
        qDebug() << "frameSizeIdx" << v;
    });
}

FrameQuality::~FrameQuality() {
    delete ui;
}
