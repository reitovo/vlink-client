#ifndef SETTINGWINDOW_H
#define SETTINGWINDOW_H

#include "qtimer.h"
#include "QSettings"
#include "QNetworkAccessManager"
#include <QMainWindow>

namespace Ui {
    class SettingWindow;
}

class CollabRoom;

class SettingWindow : public QMainWindow {
Q_OBJECT

    QTimer debugGather;
    CollabRoom *room;
    QSettings settings;
    QNetworkAccessManager networkAccessManager;

public:
    explicit SettingWindow(QWidget *parent);
    explicit SettingWindow(CollabRoom *parent);
    ~SettingWindow();

    void init();

private:
    Ui::SettingWindow *ui;

private slots:
    void updateDebug();
};

#endif // SETTINGWINDOW_H
