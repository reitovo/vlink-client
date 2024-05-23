#include "ui/windows/mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QFontDatabase>
#include <QJsonObject>
#include <QSslSocket>

#include "rtc/rtc.hpp"
#include "util/base.h"
#include <cstring>

#include "ui/windows/collabroom.h"
#include <QCommandLineParser>

extern "C" {
#include <libavutil\avutil.h>
}

#include <windows.h>
#include "concurrentqueue/concurrentqueue.h"

#ifdef HAS_CRASHPAD

#include "git.h"
#include <client/crash_report_database.h>
#include <client/settings.h>
#include <client/crashpad_client.h>

void initializeCrashpad();

#endif

void redirectDebugOutput();

void redirectStandard();

void writeQtLogThread();

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);

volatile bool ffmpegLogEnableDebug = false;

static void dxCaptureMessageHandler(int log_level, const char *format, va_list args,
                                    void *param) {
    char out[4096];
    vsnprintf(out, sizeof(out), format, args);

    switch (log_level) {
        case LOG_DEBUG:
            qDebug().noquote() << "dxcap debug:" << out;
            break;

        case LOG_INFO:
            qInfo().noquote() << "dxcap info:" << out;
            break;

        case LOG_WARNING:
            qWarning().noquote() << "dxcap warn:" << out;
            break;

        case LOG_ERROR:
            qCritical().noquote() << "dxcap error:" << out;
            break;
    }

    UNUSED_PARAMETER(param);
}

namespace {
    moodycamel::ConcurrentQueue<QString> logQueue;
    QThread *writeLogFileThread;
}

int main(int argc, char *argv[]) {
    _putenv_s("GRPC_DNS_RESOLVER", "native");

    QFile log(VLINK_LOG_FILE);
    if (log.exists() && log.size() > 1024 * 1024 * 4)
        log.remove();

    QThread::create(redirectStandard)->start();
    base_set_log_handler(dxCaptureMessageHandler, nullptr);
    if (!IsDebuggerPresent()) {
        writeLogFileThread = QThread::create(writeQtLogThread);
        writeLogFileThread->start();
        qInstallMessageHandler(customMessageHandler);
        QThread::create(redirectDebugOutput)->start();
    }

    av_log_set_flags(AV_LOG_PRINT_LEVEL);
    av_log_set_callback([](void *avcl, int level, const char *fmt, va_list vl) {
        int prefix = 1;
        char buf[1024];
        av_log_format_line2(avcl, level, fmt, vl, buf, 1024, &prefix);
        auto err = QString(buf).trimmed();
        if (level <= AV_LOG_ERROR) {
            qCritical().noquote() << err;
        } else if (level == AV_LOG_WARNING) {
            qWarning().noquote() << err;
        }
        if (ffmpegLogEnableDebug) {
            if (level == AV_LOG_INFO) {
                qInfo().noquote() << err;
            } else if (level <= AV_LOG_TRACE) {
                qDebug().noquote() << err;
            }
        }

        if (err.contains("The minimum required Nvidia driver for nvenc is")) {
            if (CollabRoom::instance())
                    emit CollabRoom::instance()->onShareError("nv driver old");
        }

//        if (err.contains("non-existing PPS 0 referenced")) {
//            if (CollabRoom::instance())
//                CollabRoom::instance()->requestIdr("DECODING_NO_PPS", "");
//        }
    });

    rtc::InitLogger(rtc::LogLevel::Debug, [](rtc::LogLevel level, std::string message) {
        switch (level) {
            case rtc::LogLevel::Verbose:
            case rtc::LogLevel::Debug: {
                qDebug().noquote() << message.c_str();
                break;
            }
            case rtc::LogLevel::Info: {
                qInfo().noquote() << message.c_str();
                break;
            }
            case rtc::LogLevel::Warning: {
                qWarning().noquote() << message.c_str();
                break;
            }
            case rtc::LogLevel::Error:
            case rtc::LogLevel::Fatal: {
                qCritical().noquote() << message.c_str();
                break;
            }
        }
    });

    QCoreApplication::setOrganizationName("Reito");
    QCoreApplication::setOrganizationDomain("reito.fun");
    QCoreApplication::setApplicationName("VLink");

    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addOption(QCommandLineOption("build-id", QString(), "build-id", "Debug"));
    parser.addOption(QCommandLineOption("turn-server", QString(), "turn-server"));
    parser.addOption(QCommandLineOption("force-use-turn", QString()));
    parser.process(app);

    vts::info::BuildId = parser.value("build-id");

    if (parser.isSet("turn-server")) {
        vts::info::OverrideTurnServer = parser.value("turn-server");
    }
    if (parser.isSet("force-use-turn")) {
        vts::info::OverrideForceUseTurn = true;
    }

    std::vector<int> fonts;
    fonts.push_back(QFontDatabase::addApplicationFont(":/fonts/SmileySans-Oblique.ttf"));
    fonts.push_back(QFontDatabase::addApplicationFont(":/fonts/MiSans-Demibold.ttf"));

#ifdef HAS_CRASHPAD
    initializeCrashpad();
#endif

    qDebug() << "isElevated =" << isElevated();
    qDebug() << "isWireless =" << isWireless();
    qDebug() << "is2G4Wireless =" << is2G4Wireless();

    MainWindow w;
    w.show();

    auto ret = QApplication::exec();

    return ret;
}

#ifdef HAS_CRASHPAD

void initializeCrashpad() {
    using namespace crashpad;
    std::map<std::string, std::string> annotations;
    std::vector<std::string> arguments;

    //改为自己的crashpad_handler.exe 路径
    QString exePath = "./crashpad_handler.exe";

    // Register your own token at backtrace.io!
    std::string url("https://submit.backtrace.io/reito/" VTSLINK_BACKTRACE_SUBMIT_TOKEN "/minidump");
    annotations["token"] = VTSLINK_BACKTRACE_SUBMIT_TOKEN;
    annotations["format"] = "minidump";
    annotations["git-branch"] = GIT_BRANCH;
    annotations["git-commit"] = GIT_HASH;
    annotations["build-id"] = vts::info::BuildId.toStdString();

    arguments.emplace_back("--no-rate-limit");
    arguments.emplace_back("--attachment=../vtslink.log");

    //放dump的文件夹 按需改
    base::FilePath db(QString("../crash").toStdWString());
    //crashpad_handler.exe 按需改
    base::FilePath handler(exePath.toStdWString());

    std::unique_ptr<CrashReportDatabase> database =
            crashpad::CrashReportDatabase::Initialize(db);

    if (database == nullptr || database->GetSettings() == NULL) {
        qCritical() << "CrashReportDatabase Init Error";
        return;
    }

    database->GetSettings()->SetUploadsEnabled(true);

    CrashpadClient client;
    bool ret = client.StartHandler(handler,
                                   db,
                                   db,
                                   url,
                                   annotations,
                                   arguments,
                                   true,
                                   true);
    if (!ret) {
        return;
    }

    ret = client.WaitForHandlerStart(INFINITE);
    if (!ret) {
        qCritical() << "CrashpadClient Start Error";
        return;
    }
}

#endif

void writeQtLogThread() {
    std::cout << "writeQtLogThread start" << std::endl;

    QFile outFile(VLINK_LOG_FILE);
    outFile.open(QIODevice::WriteOnly | QIODevice::Append);
    QTextStream textStream(&outFile);
    QString txt;
    while (!QCoreApplication::closingDown()) {
        if (logQueue.try_dequeue(txt)) {
            textStream << txt << "\r\n";
            textStream.flush();
        } else {
            QThread::msleep(1);
        }
    }

    std::cout << "writeQtLogThread exit" << std::endl;
}

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    Q_UNUSED(context);

    QString dt = QDateTime::currentDateTime().toString("dd/MM/yyyy hh:mm:ss");
    QString txt = QString("[%1] ").arg(dt);

    switch (type) {
        case QtInfoMsg:
            txt += QString("{Info}     %1").arg(msg);
            break;
        case QtDebugMsg:
            txt += QString("{Debug}    %1").arg(msg);
            break;
        case QtWarningMsg:
            txt += QString("{Warning}  %1").arg(msg);
            break;
        case QtCriticalMsg:
            txt += QString("{Critical} %1").arg(msg);
            break;
        case QtFatalMsg:
            txt += QString("{Fatal}    %1").arg(msg);
            break;
    }

    logQueue.enqueue(txt);
}

const int MAX_DebugBuffer = 4096;

typedef struct dbwin_buffer {
    DWORD dwProcessId;
    char data[4096 - sizeof(DWORD)];
} DEBUGBUFFER, *PDEBUGBUFFER;

void redirectDebugOutput() {
    HANDLE hMapping = NULL;
    HANDLE hAckEvent = NULL;
    HANDLE hReadyEvent = NULL;
    PDEBUGBUFFER pdbBuffer = NULL;

    auto thisProcId = QCoreApplication::applicationPid();
    std::cout << "redirectDebugOutput start" << thisProcId;

    // 打开事件句柄
    hAckEvent = CreateEvent(NULL, FALSE, FALSE, TEXT("DBWIN_BUFFER_READY"));
    if (hAckEvent == NULL) {
        CloseHandle(hAckEvent);
        return;
    }

    hReadyEvent = CreateEvent(NULL, FALSE, FALSE, TEXT("DBWIN_DATA_READY"));
    if (hReadyEvent == NULL) {
        CloseHandle(hReadyEvent);
        return;
    }
    // 创建文件映射
    hMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, MAX_DebugBuffer, TEXT("DBWIN_BUFFER"));
    if (hMapping == NULL) {
        CloseHandle(hMapping);
        return;
    }

    // 映射调试缓冲区
    pdbBuffer = (PDEBUGBUFFER) MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);

    // 循环
    while (!QCoreApplication::closingDown()) {
        // 激活事件
        SetEvent(hAckEvent);
        // 等待缓冲区数据
        if (WaitForSingleObject(hReadyEvent, INFINITE) == WAIT_OBJECT_0) {
            if (pdbBuffer->dwProcessId == thisProcId) {
                // 保存信息，这就是我们想要的，有了这个信息，想打log或是输出到控制台就都可以啦
                qDebug().noquote() << QString("%1").arg(QString::fromUtf8(pdbBuffer->data));
            }
        }
    }

    std::cout << "redirectDebugOutput exit";

    // 释放
    if (pdbBuffer) {
        UnmapViewOfFile(pdbBuffer);
    }
    CloseHandle(hMapping);
    CloseHandle(hReadyEvent);
    CloseHandle(hAckEvent);
}

void redirectStandard() {
    std::stringstream coutBuffer, cerrBuffer;
    std::wstringstream wcoutBuffer, wcerrBuffer;
    std::cout.rdbuf(coutBuffer.rdbuf());
    std::wcout.rdbuf(wcoutBuffer.rdbuf());
    std::cerr.rdbuf(cerrBuffer.rdbuf());
    std::wcerr.rdbuf(wcerrBuffer.rdbuf());

    while (!QCoreApplication::closingDown()) {
        if (coutBuffer.tellp() != std::stringstream::pos_type(0)) {
            qDebug() << "[cout]" << coutBuffer.str().c_str();
            coutBuffer.str("");
        }
        if (cerrBuffer.tellp() != std::stringstream::pos_type(0)) {
            qDebug() << "[cerr]" << cerrBuffer.str().c_str();
            cerrBuffer.str("");
        }
        if (wcoutBuffer.tellp() != std::stringstream::pos_type(0)) {
            qDebug().noquote() << "[wcout]" << QString::fromStdWString(wcoutBuffer.str());
            wcoutBuffer.str(L"");
        }
        if (wcerrBuffer.tellp() != std::stringstream::pos_type(0)) {
            qDebug().noquote() << "[wcerr]" << QString::fromStdWString(wcerrBuffer.str());
            wcerrBuffer.str(L"");
        }
        Sleep(10);
    }
}