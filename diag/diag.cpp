//
// Created by reito on 2023/5/25.
//

#include "diag.h"
#include "diag/amd/amf_cap.h"
#include "diag/amd/amf_enc.h"
#include "QDebug"
#include "QThread"

extern volatile bool ffmpegLogEnableDebug;

void diagnoseWorker() {
    try {
        ffmpegLogEnableDebug = true;
        qDebug() << "diagnose AMD AMF Driver";
        diagnoseAmdAmf();
        qDebug() << "diagnose AMF Encoder";
        diagnoseAmfEnc();
        ffmpegLogEnableDebug = false;
    } catch (const std::exception &ex) {
        qCritical() << "error occored" << ex.what();
    } catch (...) {
        qCritical() << "unknown error occored";
    }
}

void diagnoseAll() {
    auto t = QThread::create(diagnoseWorker);
    t->start();
    t->wait(1000);
    if (!t->isFinished()) {
        qWarning() << "diag thread is not exited in 1000ms, terminate it";
        t->terminate();
        t->wait(500);
    }
    delete t;
}