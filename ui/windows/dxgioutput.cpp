//
// Created by reito on 2023/4/21.
//

// You may need to build the project (run Qt uic code generator) to get "ui_dxgidialog.h" resolved

#include "dxgioutput.h"

#include <memory>
#include "ui_dxgioutput.h"

static DxgiOutput *instance;

DxgiOutput::DxgiOutput(QWidget *parent) :
        QDialog(parent), ui(new Ui::DxgiOutput) {
    instance = this;
    _hwnd = (HWND) instance->winId();
    setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);
    setModal(false);
    setWindowModality(Qt::NonModal);

    ui->setupUi(this);

    timer = std::make_unique<QTimer>(this);
    connect(timer.get(), &QTimer::timeout, this, [=, this]() {
        setWindowTitle(tr("调试输出窗口"));
    });
    timer->setInterval(500);
    timer->start();
}

DxgiOutput::~DxgiOutput() {
    qDebug() << "dxgi output exit";

    instance = nullptr;
    delete ui;
}

DxgiOutput *DxgiOutput::getWindow() {
    return instance;
}

HWND DxgiOutput::getHwnd() {
    return instance->_hwnd;
}

void DxgiOutput::setSize(int width, int height) {
    resize(width * 3 / 10, height * 3 / 10);
}
