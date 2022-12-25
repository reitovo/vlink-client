#include "ndi_to_av.h"
#include "core/util.h"
#include "libavutil/hwcontext_d3d11va.h"
#include "qdebug.h"
#include "qelapsedtimer.h"
#include <QStringList>
#include <sstream>
#include "core/vtslink.h"
#include "libyuv.h"

static char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };
#undef av_err2str
#define av_err2str(errnum) av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)


NdiToAv::NdiToAv(std::function<void (std::shared_ptr<VtsMsg>)> cb) {
    qDebug() << "begin ndi2av";
    onPacketReceived = cb;
}

NdiToAv::~NdiToAv()
{
    qDebug() << "end ndi2av";
    stop();
    qDebug() << "end ndi2av done";
}

QString NdiToAv::debugInfo()
{
    return QString("Ndi->Av (Stream Encoder) %1 Codec: %2").arg(fps.stat()).arg(codec);
}

bool NdiToAv::isInited() {
    return inited;
}

std::optional<QString> NdiToAv::init(int xres, int yres, int d, int n, int ft, int cc) {
    this->xres = xres;
    this->yres = yres;
    this->frameD = d;
    this->frameN = n;
    this->frameType = ft;
    this->fourCC = cc;

    qDebug() << "ndi2av init";

    nv12 = std::make_unique<BgraToNv12>();
    if (!nv12->init()) {
        qDebug() << "failed to init bgranv12";
        return "init bgra to nv12";
    }

    QList<CodecOption> options = {
        {"h264_qsv", AV_CODEC_ID_H264, NDI_TO_AV_MODE_DXFULL}, // Best quality, slightly slower
        {"h264_nvenc", AV_CODEC_ID_H264, NDI_TO_AV_MODE_DXFULL}, // Least CPU usage, fastest approach
        {"h264_amf", AV_CODEC_ID_H264, NDI_TO_AV_MODE_DXFULL}, // Good
        {"libx264", AV_CODEC_ID_H264, NDI_TO_AV_MODE_DXMAP}, // Cost reasonable CPU
        {"libx264", AV_CODEC_ID_H264, NDI_TO_AV_MODE_LIBYUV}, // Cost massive CPU
    };
    AVOption a;
    std::optional<QString> err;
    for (auto& o : options) {
        auto e = o.name;
        err = initRgb(o);
        if (err.has_value()) {
            qWarning() << "ndi2av init rgb encoder" << e << "failed" << err.value();
            stop();
            continue;
        }
        qDebug() << "ndi2av init rgb encoder" << e << "succeed";
        err = initA(o);
        if (err.has_value()) {
            qWarning() << "ndi2av init a encoder" << e << "failed" << err.value();
            stop();
            continue;
        }
        qDebug() << "ndi2av init a encoder" << e << "succeed";

        codecId = o.codecId;
        codec = o.name;
        mode = o.mode;
        break;
    }

    qDebug() << "using" << codec << "mode" << mode;

    if (mode == NDI_TO_AV_MODE_DXFULL) {
        int e;
        if ((e = av_hwframe_get_buffer(ctx_rgb->hw_frames_ctx, frame_rgb, 0)) != 0) {
            qDebug("Could not get hw frame rgb %s\n", av_err2str(e));
            return "open codec";
        }
        if ((e = av_hwframe_get_buffer(ctx_a->hw_frames_ctx, frame_a, 0)) != 0) {
            qDebug("Could not get hw frame a %s\n", av_err2str(e));
            return "open codec";
        }
    }

    packet = av_packet_alloc();
    if (!packet) {
        qDebug("Failed to allocate AVPacket\n");
        return "av packet alloc";
    }

    inited = true;
    qDebug() << "ndi2av init done";

    return std::optional<QString>();
}

std::optional<QString> NdiToAv::initRgb(const CodecOption& option)
{
    int err;

    codec_rgb = avcodec_find_encoder_by_name(option.name.toStdString().c_str());
    if (!codec_rgb) {
        qDebug("Cannot open codec\n");
        return "find av codec";
    }

    ctx_rgb = avcodec_alloc_context3(codec_rgb);
    if (!ctx_rgb) {
        qDebug("Could not allocate video codec context\n");
        return "codec alloc ctx";
    }

    ctx_rgb->bit_rate = VTSLINK_CODEC_RGB_BITRATE;
    ctx_rgb->width = xres;
    ctx_rgb->height = yres;
    ctx_rgb->time_base.den = ctx_rgb->framerate.num = frameD;
    ctx_rgb->time_base.num = ctx_rgb->framerate.den = frameN;
    ctx_rgb->pkt_timebase = ctx_rgb->time_base;

    initEncodingParameter(option, ctx_rgb);
    initOptimalEncoder(option, ctx_rgb);

    if ((err = avcodec_open2(ctx_rgb, codec_rgb, nullptr)) < 0) {
        qDebug("Could not open codec %s\n", av_err2str(err));
        return "open codec";
    }

    frame_rgb = av_frame_alloc();
    if (!frame_rgb) {
        qDebug("Could not alloc sw frame\n");
        return "frame alloc";
    }
    frame_rgb->format = option.mode == NDI_TO_AV_MODE_DXFULL ? AV_PIX_FMT_D3D11 : AV_PIX_FMT_NV12;
    frame_rgb->width = ctx_rgb->width;
    frame_rgb->height = ctx_rgb->height;
    if (option.mode == NDI_TO_AV_MODE_LIBYUV) {
        if ((err = av_frame_get_buffer(frame_rgb, 0)) < 0) {
            qDebug() << "alloc frame buffer failed" << av_err2str(err);
            return "alloc buffer";
        }
    }

    return std::optional<QString>();
}

std::optional<QString> NdiToAv::initA(const CodecOption& option)
{
    int err;

    codec_a = avcodec_find_encoder_by_name(option.name.toStdString().c_str());
    if (!codec_a) {
        qDebug("Cannot open codec\n");
        return "find av codec";
    }

    ctx_a = avcodec_alloc_context3(codec_a);
    if (!ctx_a) {
        qDebug("Could not allocate video codec context\n");
        return "codec alloc ctx";
    }

    ctx_a->bit_rate = VTSLINK_CODEC_A_BITRATE;
    ctx_a->width = xres;
    ctx_a->height = yres;
    ctx_a->time_base.den = ctx_a->framerate.num = frameD;
    ctx_a->time_base.num = ctx_a->framerate.den = frameN;
    ctx_a->pkt_timebase = ctx_a->time_base;

    initEncodingParameter(option, ctx_a);
    initOptimalEncoder(option, ctx_a);

    if ((err = avcodec_open2(ctx_a, codec_a, nullptr)) < 0) {
        qDebug("Could not open codec %s\n", av_err2str(err));
        return "open codec";
    }

    frame_a = av_frame_alloc();
    if (!frame_a) {
        qDebug("Could not alloc sw frame\n");
        return "frame alloc";
    }
    frame_a->format = option.mode == NDI_TO_AV_MODE_DXFULL ? AV_PIX_FMT_D3D11 : AV_PIX_FMT_NV12;
    frame_a->width = ctx_rgb->width;
    frame_a->height = ctx_rgb->height;
    if (option.mode == NDI_TO_AV_MODE_LIBYUV) {
        if ((err = av_frame_get_buffer(frame_a, 0)) < 0) {
            qDebug() << "alloc frame buffer failed" << av_err2str(err);
            return "alloc buffer";
        }
        memset(frame_a->data[1], 128, xres * yres / 2);
    }

    return std::optional<QString>();
}

std::optional<QString> NdiToAv::initOptimalEncoder(const CodecOption& option, AVCodecContext *ctx)
{
    int err;

    if (option.mode == NDI_TO_AV_MODE_DXFULL) {
        // Yeahy, we can optimize
        ctx->pix_fmt = AV_PIX_FMT_D3D11;
        ctx->sw_pix_fmt = AV_PIX_FMT_NV12;

        AVBufferRef* hw = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        AVHWDeviceContext* device_ctx = reinterpret_cast<AVHWDeviceContext*>(hw->data);
        AVD3D11VADeviceContext* d3d11va_device_ctx = reinterpret_cast<AVD3D11VADeviceContext*>(device_ctx->hwctx);
        d3d11va_device_ctx->device = nv12->getDevice();
        if (d3d11va_device_ctx->device == nullptr) {
            qDebug("ndi2av d3d11 device null");
            return "open d3d11 device";
        }

        if ((err = av_hwdevice_ctx_init(hw)) < 0) {
            qDebug() << "ndi2av d3d11 hwdevice failed" << av_err2str(err);
            av_buffer_unref(&hw);
            return "init hwdevice ctx";
        }
        ctx->hw_device_ctx = av_buffer_ref(hw);

        AVBufferRef* hwf = av_hwframe_ctx_alloc(hw);
        AVHWFramesContext* frame = reinterpret_cast<AVHWFramesContext*>(hwf->data);
        frame->format = AV_PIX_FMT_D3D11;
        frame->sw_format = AV_PIX_FMT_NV12;
        frame->width = ctx->width;
        frame->height = ctx->height;
        frame->initial_pool_size = 0;

        if ((err = av_hwframe_ctx_init(hwf)) < 0) {
            qDebug() << "ndi2av d3d11 hwframe failed" << av_err2str(err);
            av_buffer_unref(&hwf);
            return "init hwframe ctx";
        }
        ctx->hw_frames_ctx = av_buffer_ref(hwf);

        av_buffer_unref(&hw);
        av_buffer_unref(&hwf);
    } else {
        ctx->pix_fmt = AV_PIX_FMT_NV12;
    }

    return std::optional<QString>();
}

void NdiToAv::initEncodingParameter(const CodecOption& option, AVCodecContext *ctx)
{
    ctx->max_b_frames = 1;
    ctx->gop_size = 30;
    auto encoder = option.name;
    if (encoder == "h264_nvenc" || encoder == "hevc_nvenc") {
        av_opt_set(ctx->priv_data, "preset", "p4", 0); 
        av_opt_set(ctx->priv_data, "profile", "main", 0);
        av_opt_set(ctx->priv_data, "rc", "cbr", 0);

    } else if (encoder == "h264_amf" || encoder == "hevc_amf") {
        av_opt_set(ctx->priv_data, "usage", "transcoding", 0);
        av_opt_set(ctx->priv_data, "profile", "main", 0);
        av_opt_set(ctx->priv_data, "quality", "balanced", 0);
        av_opt_set_int(ctx->priv_data, "header_spacing", ctx->gop_size, 0);
        av_opt_set_int(ctx->priv_data, "frame_skipping", 0, 0);
        
        av_opt_set(ctx->priv_data, "rc", "vbr_peak", 0);
        ctx->rc_max_rate = ctx->bit_rate * 5;
        ctx->rc_buffer_size = ctx->bit_rate * 3;

        //av_opt_set(ctx->priv_data, "rc", "cqp", 0);
        //av_opt_set_int(ctx->priv_data, "qp_i", 30, 0);
        //av_opt_set_int(ctx->priv_data, "qp_p", 30, 0);
        //av_opt_set_int(ctx->priv_data, "qp_b", 30, 0);

        //av_opt_set(ctx->priv_data, "rc", "cbr", 0);
        //av_opt_set_int(ctx->priv_data, "filler_data", 1, 0); 

        //av_opt_set_int(ctx->priv_data, "log_to_dbg", 1, 0);
    } else if (encoder == "h264_qsv" || encoder == "hevc_qsv") {
        av_opt_set(ctx->priv_data, "preset", "fast", 0);  
    }
    else {
        av_opt_set(ctx->priv_data, "preset", "fast", 0);
    }
}

std::optional<QString> NdiToAv::process(NDIlib_video_frame_v2_t* ndi, std::shared_ptr<DxToNdi> fast)
{
    int ret;

    if (ndi->xres != xres || ndi->yres != yres ||
            ndi->FourCC != fourCC || ndi->frame_format_type != frameType ||
            ndi->frame_rate_D != frameD || ndi->frame_rate_N != frameN) {
        qDebug() << "source format changed";
        return "frame change error";
    }

    QElapsedTimer t;
    t.start();

    //Elapsed e1("convert");
    if (mode == NDI_TO_AV_MODE_DXFULL) {
        nv12->bgraToNv12(ndi, fast);
        nv12->copyFrame(frame_rgb, frame_a);
    } else if (mode == NDI_TO_AV_MODE_DXMAP) {
        nv12->bgraToNv12(ndi); //~ 340000ns
        nv12->mapFrame(frame_rgb, frame_a);
    } else if (mode == NDI_TO_AV_MODE_LIBYUV) {
        libyuv::ARGBToNV12(ndi->p_data, ndi->line_stride_in_bytes,
                frame_rgb->data[0], frame_rgb->linesize[0],
                frame_rgb->data[1], frame_rgb->linesize[1],
                ndi->xres, ndi->yres);
        libyuv::ARGBExtractAlpha(ndi->p_data, ndi->line_stride_in_bytes,
                frame_a->data[0], frame_a->linesize[0],
                ndi->xres, ndi->yres);
    }
    //e1.end();

    pts++;
    frame_rgb->pts = frame_a->pts = pts;
    auto msg = std::make_shared<VtsMsg>();
    msg->set_version(1);
    msg->set_type(VTS_MSG_AVFRAME);
    auto avFrame = msg->mutable_avframe();
    avFrame->set_version(1);
    avFrame->set_pts(pts);

    QStringList err;

    //Elapsed e2("encode"); 
    ret = avcodec_send_frame(ctx_rgb, frame_rgb);
    if (ret < 0) {
        qDebug() << "error sending frame for rgb encoding" << av_err2str(ret);
        err.append("send frame");
    }

    while (ret >= 0) {
        packet->stream_index = 0;
        ret = avcodec_receive_packet(ctx_rgb, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            qDebug() << "error recving packet for rgb encoding";
            err.append("recv packet");
        }

        //qDebug() << "rgb packet" << packet->size;
        avFrame->add_rgbpackets(packet->data, packet->size);

        av_packet_unref(packet);
    }
     
    ret = avcodec_send_frame(ctx_a, frame_a);
    if (ret < 0) {
        qDebug() << "error sending frame for a encoding" << av_err2str(ret);
        err.append("send frame");
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(ctx_a, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            qDebug() << "error recving packet for a encoding";
            err.append("recv packet");
        }

        //qDebug() << "a packet" << packet->size;
        avFrame->add_apackets(packet->data, packet->size);

        av_packet_unref(packet);
    }
    //e2.end();

    if (mode == NDI_TO_AV_MODE_DXMAP) {
        nv12->unmapFrame();
    }

    fps.add(t.nsecsElapsed());

    if (!err.empty()) {
        auto ee = err.join(", ");
        qDebug() << "one or more error occoured" << ee;
        return ee;
    }

    //e.end(); 
    onPacketReceived(std::move(msg)); 

    return std::optional<QString>();
}

void NdiToAv::stop()
{
    if (!inited)
        return;
    inited = false;

    auto msg = std::make_shared<VtsMsg>();
    msg->set_version(1);
    msg->set_type(VTS_MSG_AVSTOP);
    onPacketReceived(std::move(msg));

    pts = 0;
    nv12 = nullptr;
    av_packet_free(&packet);
    av_frame_free(&frame_rgb);
    av_frame_free(&frame_a); 
    avcodec_free_context(&ctx_rgb);
    avcodec_free_context(&ctx_a);
}
