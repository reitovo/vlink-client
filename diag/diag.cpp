//
// Created by reito on 2023/5/25.
//

#include "diag.h"
#include "diag/amd/amf_cap.h"
#include "diag/amd/amf_enc.h"
#include "QDebug"

extern volatile bool ffmpegLogEnableDebug;

void diagnoseAll() {
    ffmpegLogEnableDebug = true;
    qDebug() << "diagnose AMD AMF Driver";
    diagnoseAmdAmf();
    qDebug() << "diagnose AMF Encoder";
    diagnoseAmfEnc();
    ffmpegLogEnableDebug = false;
}