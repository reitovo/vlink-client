#ifndef PEERITEMWIDGET_H
#define PEERITEMWIDGET_H

#include <QWidget>
#include "ui/windows/collabroom.h"

namespace Ui {
class PeerItemWidget;
}

class PeerUi;
class PeerItemWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PeerItemWidget(QWidget *parent, CollabRoom* room);
    ~PeerItemWidget();

    void setPeerUi(PeerUi p);

private slots:
    void updateStats();

private:
    std::optional<PeerUi> peerUi;
    Ui::PeerItemWidget *ui;
    CollabRoom* room;
    std::unique_ptr<QTimer> refreshTimer;
};

#endif // PEERITEMWIDGET_H
