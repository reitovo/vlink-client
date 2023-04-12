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
    CollabRoom* room;

    QSettings settings;

public:
    explicit FrameQuality(CollabRoom *parent = nullptr);
    ~FrameQuality() override;

    bool changed = false;

private:
    Ui::FrameQuality *ui;
};

#endif //VTSLINK_FRAMEQUALITY_H
