#ifndef SETTINGWINDOW_H
#define SETTINGWINDOW_H

#include "qtimer.h"
#include "QSettings"
#include "out/build/x64-RelWithDebInfo/vcpkg_installed/x64-windows/include/Qt6/QtNetwork/QNetworkAccessManager"
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
    QSettings settings;
    QNetworkAccessManager networkAccessManager;

public:
    explicit SettingWindow(QWidget* parent);
    explicit SettingWindow(CollabRoom *parent);
    ~SettingWindow();

    void init();

private:
    Ui::SettingWindow *ui;

private slots:
    void updateDebug();
};

#endif // SETTINGWINDOW_H
