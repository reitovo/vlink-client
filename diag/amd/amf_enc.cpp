//
// Created by reito on 2023/5/25.
//

#include "amf_enc.h"
#include "libavutil/hwcontext_d3d11va.h"
#include "bgra_to_nv12.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libswscale/swscale.h"
#include "libavutil/hwcontext.h"
#include "libavutil/pixdesc.h"
}

#include <QDebug>
#include <QSettings>

static char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };
#undef av_err2str
#define av_err2str(errnum) av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)
#define THROW_IF_ERROR(x) { int _rr = 0; if (( _rr = (x)) != 0) { qDebug() << "return" << _rr << #x; return; }}

void createCodec(bool intraRefresh) {
    auto _width = 1920;
    auto _height = 1080;

    auto nv12 = std::make_unique<BgraToNv12>(_width, _height);
    if (!nv12->init()) {
        qDebug() << "failed to init bgranv12";
        return;
    }

    const AVCodec* codec = nullptr;
    AVCodecContext* ctx = nullptr;
    AVFrame* frame = nullptr;
    AVBufferRef* hw = nullptr;
    AVBufferRef* hwf = nullptr;
    AVHWDeviceContext* device_ctx = nullptr;
    AVD3D11VADeviceContext* d3d11va_device_ctx = nullptr;
    AVHWFramesContext* hwFrame = nullptr;

    QSettings settings;
    auto enableAmfCompatible = settings.value("enableAmfCompatible", false).toBool();

    auto err = 0;
    auto cqp = 24;
    auto gopSize = intraRefresh ? 300 : 60;

    codec = avcodec_find_encoder_by_name("h264_amf");
    if (!codec) {
        qDebug("Cannot open codec\n");
        goto clean;
    }

    ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        qDebug("Could not allocate video codec context\n");
        goto clean;
    }

    ctx->width = _width;
    ctx->height = _height * 2;
    ctx->time_base.den = ctx->framerate.num = 1;
    ctx->time_base.num = ctx->framerate.den = 60;
    ctx->pkt_timebase = ctx->time_base;

    ctx->rc_max_rate = 4000000;
    ctx->bit_rate = 2000000;
    ctx->max_b_frames = 0;
    ctx->slices = 1;

    ctx->gop_size = intraRefresh ? 0 : gopSize;

    THROW_IF_ERROR(av_opt_set(ctx->priv_data, "usage", enableAmfCompatible ? "lowlatency" : "ultralowlatency", 0));
    THROW_IF_ERROR(av_opt_set(ctx->priv_data, "profile", "main", 0));
    THROW_IF_ERROR(av_opt_set(ctx->priv_data, "quality", "speed", 0));
    THROW_IF_ERROR(av_opt_set_int(ctx->priv_data, "frame_skipping", 0, 0));
    THROW_IF_ERROR(av_opt_set_int(ctx->priv_data, "header_spacing", gopSize, 0));
    THROW_IF_ERROR(av_opt_set_int(ctx->priv_data, "intra_refresh_mb", intraRefresh ? 255 : 0, 0));

    THROW_IF_ERROR(av_opt_set(ctx->priv_data, "rc", "cqp", 0));
    THROW_IF_ERROR(av_opt_set_int(ctx->priv_data, "qp_i", cqp, 0));
    THROW_IF_ERROR(av_opt_set_int(ctx->priv_data, "qp_p", cqp, 0));
    THROW_IF_ERROR(av_opt_set_int(ctx->priv_data, "qp_b", cqp, 0));

    ctx->pix_fmt = AV_PIX_FMT_D3D11;
    ctx->sw_pix_fmt = AV_PIX_FMT_NV12;

    hw = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    device_ctx = reinterpret_cast<AVHWDeviceContext*>(hw->data);
    d3d11va_device_ctx = reinterpret_cast<AVD3D11VADeviceContext*>(device_ctx->hwctx);
    d3d11va_device_ctx->device = nv12->getDevice();
    if (d3d11va_device_ctx->device == nullptr) {
        qDebug("dx2av d3d11 device null");
        goto clean;
    }

    if ((err = av_hwdevice_ctx_init(hw)) < 0) {
        qDebug() << "dx2av d3d11 hwdevice failed" << av_err2str(err);
        goto clean;
    }
    ctx->hw_device_ctx = av_buffer_ref(hw);

    hwf = av_hwframe_ctx_alloc(hw);
    hwFrame = reinterpret_cast<AVHWFramesContext*>(hwf->data);
    hwFrame->format = AV_PIX_FMT_D3D11;
    hwFrame->sw_format = AV_PIX_FMT_NV12;
    hwFrame->width = ctx->width;
    hwFrame->height = ctx->height;

    if ((err = av_hwframe_ctx_init(hwf)) < 0) {
        qDebug() << "dx2av child d3d11 hwframe failed" << av_err2str(err);
        goto clean;
    }
    ctx->hw_frames_ctx = av_buffer_ref(hwf);

    if ((err = avcodec_open2(ctx, codec, nullptr)) < 0) {
        qDebug("Could not open codec %s\n", av_err2str(err));
        goto clean;
    }

    frame = av_frame_alloc();
    if (!frame) {
        qDebug("Could not alloc sw frame\n");
        goto clean;
    }
    frame->format = AV_PIX_FMT_D3D11;
    frame->width = ctx->width;
    frame->height = ctx->height;

    if ((err = av_hwframe_get_buffer(ctx->hw_frames_ctx, frame, 0)) != 0) {
        qWarning("Could not get hw frame rgb %s\n", av_err2str(err));
        goto clean;
    }

clean:
    av_buffer_unref(&hw);
    av_buffer_unref(&hwf);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
}

int diagnoseAmfEnc() {
    qDebug() << "diagnose AMF Encoder intraRefresh = 0";
    createCodec(false);
    qDebug() << "diagnose AMF Encoder intraRefresh = 1";
    createCodec(true);
    return 0;
}