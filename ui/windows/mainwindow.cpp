#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "collabroom.h"
#include "settingwindow.h"
#include "dx-capture.h"
#include "QMessageBox"

#include <QTranslator>
#include <QPushButton>
#include <QDesktopServices>
#include <utility>

#include "dxgi1_2.h"

static MainWindow *_instance;

MainWindow::MainWindow(QWidget *parent)
        : QMainWindow(parent), ui(new Ui::MainWindow) {
    _instance = this;
    ui->setupUi(this);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);

    auto idleText = "by @铃当Reito";
    ui->actionVersion->setText(idleText);

    ui->iptRoomId->setStyleSheet("QLineEdit[echoMode=\"2\"]{ lineedit-password-character: 42 }");


    const QStringList uiLanguages = QLocale::system().uiLanguages();
    qDebug() << "Languages" << uiLanguages.join(", ");

    connect(ui->actionExit, SIGNAL(triggered()), this, SLOT(actionExit()));

    connect(ui->actionLangZh, &QAction::triggered, this, [this] { actionSetLang("zh_CN"); });
    connect(ui->actionLangEn, &QAction::triggered, this, [this] { actionSetLang("en_US"); });
    connect(ui->actionLangJa, &QAction::triggered, this, [this] {
        tray->showMessage(tr("Not Translated Yet"), tr("Not translated yet, sorry for that."), tray->icon());
        //actionSetLang("ja-JP");
    });

    connect(ui->btnJoinRoom, SIGNAL(clicked()), this, SLOT(joinRoom()));
    connect(ui->btnCreateRoom, SIGNAL(clicked()), this, SLOT(createRoom()));
    connect(ui->actionOpenSetting, SIGNAL(triggered()), this, SLOT(openSetting()));

    connect(ui->actionLicense, &QAction::triggered, this, [=]() {
        QDesktopServices::openUrl(QUrl("https://www.wolai.com/reito/eQGuG4smx19LYTqTtmkKtn"));
    });

    tray = std::make_unique<QSystemTrayIcon>(this);
    QIcon icon;
    icon.addFile(QString::fromUtf8(":/images/icon.ico"), QSize(), QIcon::Normal, QIcon::Off);
    tray->setIcon(icon);
    tray->show();

    // iterateCodec();
    // iterateHwAccels();
    debugVideoAdapters();

    QSettings settings;
    auto lang = settings.value("languageCode", "").toString();
    if (lang.isEmpty()) {
        QStringList validLangCode{"zh_CN", "en_US"};
        for (QString locale: uiLanguages) {
            auto code = locale.replace("-", "_");
            if (validLangCode.contains(code)) {
                actionSetLang(code);
                qDebug() << "Using language" << code;
                break;
            }
        }
    } else {
        actionSetLang(lang);
    }

    dx_capture_load();

    auto ignoreFirstTimeHint = settings.value("ignoreFirstTimeHint", false).toBool();
    if (!ignoreFirstTimeHint) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(tr("新手教程"));
        box.setText(tr("欢迎使用本软件进行 VTube Studio 联动！\n\n首次使用？别担心，点击「进入」阅读使用教程。\n\n提示：后续也可以在主界面打开「使用教程 / 常见问题」进行阅读"));
        auto ok = box.addButton(tr("进入"), QMessageBox::NoRole);
        auto ign = box.addButton(tr("不再提示"), QMessageBox::NoRole);
        box.exec();
        auto ret = dynamic_cast<QPushButton *>(box.clickedButton());
        if (ret == ign) {
            settings.setValue("ignoreFirstTimeHint", true);
            settings.sync();
        } else if (ret == ok) {
            QDesktopServices::openUrl(QUrl("https://www.wolai.com/reito/nhenjFvkw5gDNM4tikEw5V"));
        }
    }

    auto lastJoinRoomId = settings.value("lastJoinRoomId", "").toString();
    if (!lastJoinRoomId.isEmpty()) {
        ui->iptRoomId->setText(lastJoinRoomId);
    }
}

MainWindow::~MainWindow() {
    _instance = nullptr;
    tray->hide();

    dx_capture_unload();

    qDebug() << "destroy main window";
    delete ui;
    qDebug() << "bye";
}

MainWindow *MainWindow::instance() {
    return _instance;
}

void MainWindow::actionExit() {
    close();
}

void MainWindow::joinRoom() {
    auto roomId = ui->iptRoomId->text().trimmed();
    qDebug("Join room id %s", qPrintable(roomId));

    QSettings settings;
    settings.setValue("lastJoinRoomId", roomId);
    settings.sync();

    auto c = new CollabRoom(false, roomId);
    hide();
    connect(c, &QDialog::destroyed, this, [=]() {
        show();
    });
}

void MainWindow::createRoom() {
    qDebug() << "Create room" ;
    auto c = new CollabRoom(true);
    hide();
    connect(c, &QDialog::destroyed, this, [=]() {
        show();
    });
}

void MainWindow::actionSetLang(QString code) {
    qApp->removeTranslator(&translator);
    if (translator.load(":/i18n/VTSLink_" + code)) {
        qApp->installTranslator(&translator);
        ui->retranslateUi(this);

        QSettings settings;
        settings.setValue("languageCode", code);
        settings.sync();

        qDebug() << "set lang" << code;
    } else {
        qDebug() << "failed to load translator" << code;
    }
}

void MainWindow::iterateCodec() {
    void *iter = NULL;
    const AVCodec *codec = NULL;

    qDebug() << "Supported codecs";
    while ((codec = av_codec_iterate(&iter))) {
        if (av_codec_is_encoder(codec)) {
            if (codec->pix_fmts) {
                std::stringstream ss;
                ss << codec->name;
                ss << " enc: ";
                AVPixelFormat fmt;
                int idx = 0;
                while (true) {
                    fmt = codec->pix_fmts[idx++];
                    if (fmt == AV_PIX_FMT_NONE)
                        break;
                    ss << av_get_pix_fmt_name(fmt) << " ";
                }
                qDebug() << ss.str().c_str();
            }
        } else if (av_codec_is_decoder(codec)) {
            if (codec->pix_fmts) {
                std::stringstream ss;
                ss << codec->name;
                ss << " dec: ";
                AVPixelFormat fmt;
                int idx = 0;
                while (true) {
                    fmt = codec->pix_fmts[idx++];
                    if (fmt == AV_PIX_FMT_NONE)
                        break;
                    ss << av_get_pix_fmt_name(fmt) << " ";
                }
                qDebug() << ss.str().c_str();
            }
        }
    }
}

void MainWindow::iterateHwAccels() {
    qDebug() << "available hwaccels";
    AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
    while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE) {
        auto t = av_hwdevice_get_type_name(type);
        qDebug() << t;
    }
}

void MainWindow::openSetting() {
    auto w = new SettingWindow(this);
    w->show();
}

void MainWindow::debugVideoAdapters() {
    // We need dxgi to share texture
    IDXGIFactory2* pDXGIFactory;
    IDXGIAdapter* pAdapter = NULL;
    HRESULT hr = CreateDXGIFactory(IID_IDXGIFactory2, (void**)&pDXGIFactory);
    if (FAILED(hr)) {
        qDebug() << "failed to create dxgi factory";
        return;
    }

    int count = 0;
    while (true){
        hr = pDXGIFactory->EnumAdapters(count, &pAdapter);
        if (FAILED(hr)) {
            break;
        }

        DXGI_ADAPTER_DESC descAdapter;
        hr = pAdapter->GetDesc(&descAdapter);
        if (FAILED(hr)) {
            break;
        }

        qDebug() << "adapter device" << count << QString::fromWCharArray(descAdapter.Description);
        auto vendor = getGpuVendorTypeFromVendorId(descAdapter.VendorId);
        qDebug() << "device vendor" << getGpuVendorName(vendor);
        count++;
    }
}
