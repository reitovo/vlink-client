#include "settingwindow.h"
#include "ui_settingwindow.h"
#include "collabroom.h"

SettingWindow::SettingWindow(CollabRoom *parent) :
    QMainWindow(parent),
    ui(new Ui::SettingWindow)
{
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose);

    room = parent;
    connect(&debugGather, &QTimer::timeout, this, &SettingWindow::updateDebug);
    debugGather.start(100);
}

SettingWindow::~SettingWindow()
{
    delete ui;
}

void SettingWindow::updateDebug()
{
    ui->debugInfo->setText(DebugCenter::debugInfo());
}
