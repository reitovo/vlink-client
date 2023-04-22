//
// Created by reito on 2023/4/12.
//

#ifndef VTSLINK_FRAMEQUALITY_H
#define VTSLINK_FRAMEQUALITY_H

#include <QDialog>
#include <QSettings>

QT_BEGIN_NAMESPACE
namespace Ui { class FrameQuality; }
QT_END_NAMESPACE

class CollabRoom;

class FrameQuality : public QDialog {
Q_OBJECT
    QSettings settings;

    void updateBandwidthEstimate();

public:
    explicit FrameQuality(CollabRoom *parent = nullptr);
    ~FrameQuality() override;

    bool changed = false;
    float frameRate = 60;
    int frameWidth = 1920;
    int frameHeight = 1080;
    int frameQuality = 0;

private:
    Ui::FrameQuality *ui;
};

#endif //VTSLINK_FRAMEQUALITY_H
