#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtNetwork>
#include <QSystemTrayIcon>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

// Main window, do initializations and go to collab.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    static MainWindow *instance();
    std::unique_ptr<QSystemTrayIcon> tray;

private:
    Ui::MainWindow *ui;

    QTranslator translator;
    void actionSetLang(QString code);

    void iterateCodec();
    void iterateHwAccels();

private slots:
    void actionExit();
    void joinRoom();
    void createRoom();
    void openSetting();
};
#endif // MAINWINDOW_H
