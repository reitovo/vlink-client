//
// Created by reito on 2023/4/12.
//

#ifndef VTSLINK_FRAMEQUALITY_H
#define VTSLINK_FRAMEQUALITY_H

#include <QDialog>
#include <QSettings>
#include "core/util.h"

QT_BEGIN_NAMESPACE
namespace Ui { class FrameQuality; }
QT_END_NAMESPACE

class CollabRoom;

class FrameQuality : public QDialog {
Q_OBJECT
    QSettings settings;

    void updateBandwidthEstimate();

public:
    explicit FrameQuality(FrameQualityDesc init, QWidget *parent = nullptr);
    ~FrameQuality() override;

    bool changed = false;
    FrameQualityDesc quality;

private:
    Ui::FrameQuality *ui;
};

#endif //VTSLINK_FRAMEQUALITY_H
