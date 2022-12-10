#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QFontDatabase>
#include <QJsonObject>
#include <QSslSocket>

#include "rtc/rtc.hpp"

extern "C" {
#include <libavutil\avutil.h>
}

int main(int argc, char *argv[])
{
    av_log_set_level(AV_LOG_DEBUG);

    rtc::InitLogger(rtc::LogLevel::Debug, [](rtc::LogLevel level, std::string message) {
        switch (level) {
        case rtc::LogLevel::Verbose:
        case rtc::LogLevel::Debug: {
            qDebug(message.c_str());
            break;
        }
        case rtc::LogLevel::Info: {
            qInfo(message.c_str());
            break;
        }
        case rtc::LogLevel::Warning: {
            qWarning(message.c_str());
            break;
        }
        case rtc::LogLevel::Error: {
            qCritical(message.c_str());
            break;
        }
        case rtc::LogLevel::Fatal: {
            qFatal(message.c_str());
            break;
        }
        }
    });

    QCoreApplication::setOrganizationName("Reito");
    QCoreApplication::setOrganizationDomain("reito.fun");
    QCoreApplication::setApplicationName("VTSLink");

    QApplication a(argc, argv);

    QFontDatabase::addApplicationFont(":/fonts/SmileySans-Oblique.ttf");
    QFontDatabase::addApplicationFont(":/fonts/AlibabaPuHuiTi-2-55-Regular.ttf");

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "VTSLink_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    MainWindow w;
    w.show();

    auto ret = a.exec();

    return ret;
}
