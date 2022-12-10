#ifndef PEERITEMWIDGET_H
#define PEERITEMWIDGET_H

#include <QWidget>

namespace Ui {
class PeerItemWidget;
}

class PeerUi;
class PeerItemWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PeerItemWidget(QWidget *parent = nullptr);
    ~PeerItemWidget();

    void setPeerUi(PeerUi p, bool self);

private:
    Ui::PeerItemWidget *ui;
};

#endif // PEERITEMWIDGET_H
