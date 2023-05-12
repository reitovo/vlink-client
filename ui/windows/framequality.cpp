//
// Created by reito on 2023/4/12.
//

// You may need to build the project (run Qt uic code generator) to get "ui_FrameQuality.h" resolved

#include "framequality.h"
#include "ui_FrameQuality.h"
#include "collabroom.h"
#include "math.h"

FrameQuality::FrameQuality(FrameQualityDesc init, QWidget *parent) :
        QDialog(parent), ui(new Ui::FrameQuality) {
    ui->setupUi(this);

    quality = init;

    ui->frameQuality->setCurrentIndex(init.frameQuality);

    switch ((int)init.frameRate) {
        default:
        case 30:
            ui->frameRate->setCurrentIndex(0);
            break;
        case 60:
            ui->frameRate->setCurrentIndex(1);
            break;
    }

    switch (init.frameWidth) {
        case 1280:
            ui->frameSize->setCurrentIndex(0);
            break;
        default:
        case 1600:
            ui->frameSize->setCurrentIndex(1);
            break;
        case 1920:
            ui->frameSize->setCurrentIndex(2);
            break;
    }

    connect(ui->frameQuality, &QComboBox::currentIndexChanged, this, [=, this](int v) {
        changed = true;
        settings.setValue("frameQualityIdx", v);

        quality.frameQuality = v;

        settings.sync();
        qDebug() << "frameQualityIdx" << v;

        updateBandwidthEstimate();
    });

    connect(ui->frameRate, &QComboBox::currentIndexChanged, this, [=, this](int v) {
        changed = true;

        switch (v) {
            case 0:
                quality.frameRate = 30;
                break;
            case 1:
                quality.frameRate = 60;
                break;
        }

        settings.setValue("frameRate", quality.frameRate);
        settings.sync();
        qDebug() << "frameRateIdx" << v;

        updateBandwidthEstimate();
    });

    connect(ui->frameSize, &QComboBox::currentIndexChanged, this, [=, this](int v) {
        changed = true;

        switch (v) {
            case 0:
                quality.frameWidth = 1280;
                quality.frameHeight = 720;
                break;
            case 1:
                quality.frameWidth = 1600;
                quality.frameHeight = 900;
                break;
            case 2:
                quality.frameWidth = 1920;
                quality.frameHeight = 1080;
                break;
        }

        settings.setValue("frameWidth", quality.frameWidth);
        settings.setValue("frameHeight", quality.frameHeight);
        settings.sync();
        qDebug() << "frameSizeIdx" << v;

        updateBandwidthEstimate();
    });

    updateBandwidthEstimate();
}

FrameQuality::~FrameQuality() {
    delete ui;
}

void FrameQuality::updateBandwidthEstimate() {
    double pixelFactor = quality.frameWidth * quality.frameHeight / 1920.0 / 1080.0;
    double fpsFactor = sin(M_PI_2 * quality.frameRate / 60);
    double qualityFactor = pow(2, quality.frameQuality) * 4;
    double maxBandwidth = qualityFactor * fpsFactor * pixelFactor;
    double constBandwidth = maxBandwidth * 0.45;

    ui->constBandwidth->setText(QString::number(constBandwidth, 'f', 2) + " Mbps");
    ui->maxBandwidth->setText(QString::number(maxBandwidth, 'f', 2) + " Mbps");
}
