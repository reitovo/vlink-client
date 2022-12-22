#ifndef SETTINGWINDOW_H
#define SETTINGWINDOW_H

#include "qtimer.h"
#include <QMainWindow>

namespace Ui {
class SettingWindow;
}

class CollabRoom;
class SettingWindow : public QMainWindow
{
    Q_OBJECT

    QTimer debugGather;
    CollabRoom* room;

public:
    explicit SettingWindow(CollabRoom *parent = nullptr);
    ~SettingWindow();

private:
    Ui::SettingWindow *ui;

private slots:
    void updateDebug();
};

#endif // SETTINGWINDOW_H