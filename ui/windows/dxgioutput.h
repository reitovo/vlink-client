//
// Created by reito on 2023/4/21.
//

#ifndef VTSLINK_DXGIDIALOG_H
#define VTSLINK_DXGIDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui { class DxgiOutput; }
QT_END_NAMESPACE

class DxgiOutput : public QDialog {
Q_OBJECT

    HWND _hwnd;

public:
    explicit DxgiOutput(QWidget *parent = nullptr);
    ~DxgiOutput() override;

    static HWND getHwnd();
    static DxgiOutput *getWindow();

    void setSize(int width, int height);

private:
    Ui::DxgiOutput *ui;
};

#endif //VTSLINK_DXGIDIALOG_H
