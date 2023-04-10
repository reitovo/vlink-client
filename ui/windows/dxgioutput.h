//
// Created by reito on 2022/12/27.
//

#ifndef VTSLINK_DXGIOUTPUT_H
#define VTSLINK_DXGIOUTPUT_H

#include <QMainWindow>


QT_BEGIN_NAMESPACE
namespace Ui { class DxgiOutput; }
QT_END_NAMESPACE

class DxgiOutput : public QMainWindow {
Q_OBJECT

    HWND _hwnd;

public:
    explicit DxgiOutput(QWidget *parent = nullptr);

    ~DxgiOutput() override;

    static HWND getHwnd();
    static DxgiOutput* getWindow();

    void setSize(int width, int height);

private:
    Ui::DxgiOutput *ui;
};


#endif //VTSLINK_DXGIOUTPUT_H
