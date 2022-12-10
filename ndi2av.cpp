#include "ndi2av.h"
#include "qdebug.h"
#include "qelapsedtimer.h"
#include <QStringList>
#include <libyuv.h>
#include <sstream>

static char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };
#undef av_err2str
#define av_err2str(errnum) av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)

bool ndi2av::isInited() {
    return inited;
}

std::optional<QString> ndi2av::init(int xres, int yres, int d, int n, int ft, int cc) {
    this->xres = xres;
    this->yres = yres;
    this->frameD = d;
    this->frameN = n;
    this->frameType = ft;
    this->fourCC = cc;

    iterateCodec();

    int err;

    qDebug() << "available hwaccels";
    AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
    QStringList hws;
    while((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE) {
        auto t = av_hwdevice_get_type_name(type);
        hws.append(t);
        qDebug() << t;
    }

    // Select preferred hwaccel
    codec = avcodec_find_encoder_by_name("h264_qsv");
    if (!codec) {
        qDebug("Cannot open codec\n");
        return "find av codec";
    }

    ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        qDebug("Could not allocate video codec context\n");
        return "codec alloc ctx";
    }

    ctx->bit_rate = 10000;
    ctx->width = xres;
    ctx->height = yres;
    ctx->time_base.den = ctx->framerate.num = frameD;
    ctx->time_base.num = ctx->framerate.den = frameN;
    ctx->gop_size = 10;
    ctx->max_b_frames = 1;
    ctx->pix_fmt = AV_PIX_FMT_QSV;
    //av_opt_set(ctx->priv_data, "preset", "slow", 0);

    type = AV_HWDEVICE_TYPE_QSV;
    if ((err = av_hwdevice_ctx_create(&hw_device, type,
                                      NULL, NULL, 0)) < 0) {
        qDebug("Failed to create specified HW device %d.\n", err);
        return "hw create error";
    }

    AVBufferRef *hw_frames_ref;
    AVHWFramesContext *frames_ctx = NULL;

    if (!(hw_frames_ref = av_hwframe_ctx_alloc(hw_device))) {
        fprintf(stderr, "Failed to create frame context.\n");
        return "create frame context";
    }
    frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
    frames_ctx->format    = AV_PIX_FMT_QSV;
    frames_ctx->sw_format = AV_PIX_FMT_YUVA420P;
    frames_ctx->width     = xres;
    frames_ctx->height    = yres;
    frames_ctx->initial_pool_size = 20;
    if ((err = av_hwframe_ctx_init(hw_frames_ref)) < 0) {
        fprintf(stderr, "Failed to initialize frame context."
                "Error code: %s\n",av_err2str(err));
        av_buffer_unref(&hw_frames_ref);
        return "hw frame init";
    }
    ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    av_buffer_unref(&hw_frames_ref);

//    iterateCodecHwConstraint(ctx->hw_device_ctx, codec);

//    for (int i = 0;; i++) {
//        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
//        if (!config) {
//            qDebug("Decoder %s does not support device type %s.\n",
//                    codec->name, av_hwdevice_get_type_name(type));
//            return "hw config error";
//        }
//        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
//            config->device_type == type) {
//            hw_pix_fmt = config->pix_fmt;
//            break;
//        }
//    }

    if ((err = avcodec_open2(ctx, codec, nullptr)) < 0) {
        qDebug("Could not open codec %s\n", av_err2str(err));
        return "open codec";
    }

    frame = av_frame_alloc();
    if (!frame) {
        qDebug("Could not alloc sw frame\n");
        return "frame alloc";
    }
    frame->format = AV_PIX_FMT_YUVA420P;
    frame->width = ctx->width;
    frame->height = ctx->height;
    if ((err = av_frame_get_buffer(frame, 0)) < 0) {
        qDebug("Could not alloc sw frame buffer %d\n", err);
        return "frame alloc";
    }

    hw_frame = av_frame_alloc();
    if (!hw_frame) {
        qDebug("Could not alloc hw frame\n");
        return "frame alloc";
    }
    if ((err = av_hwframe_get_buffer(ctx->hw_frames_ctx, hw_frame, 0)) < 0) {
        qDebug("Could not alloc hw frame buffer %d\n", err);
        return "frame alloc";
    }

    packet = av_packet_alloc();
    if (!packet) {
        qDebug("Failed to allocate AVPacket\n");
        return "av packet alloc";
    }

    avformat_alloc_output_context2(&dbg, nullptr, "flv", nullptr);
    avio_open2(&dbg->pb, "./dbg.flv", AVIO_FLAG_WRITE, nullptr, nullptr);
    dout = avformat_new_stream(dbg, nullptr);
    avcodec_parameters_from_context(dout->codecpar, ctx);
    avformat_write_header(dbg, nullptr);

    inited = true;
    return std::optional<QString>();
}

void ndi2av::setOnPacketReceived(std::function<void (uint8_t *, int)> f) {
    onPacketReceived = f;
}

std::optional<QString> ndi2av::process(NDIlib_video_frame_v2_t* ndi)
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

    libyuv::ARGBToI420(ndi->p_data, ndi->line_stride_in_bytes,
            frame->data[0], xres,
            frame->data[1], (xres + 1) >> 1,
            frame->data[2], (xres + 1) >> 1,
            xres, yres);
    auto pixels = xres * yres;
    auto alpha_ptr = ndi->p_data + 3;
    auto dst_ptr = frame->data[3];
    for (int c = 0; c < pixels; ++c) {
        dst_ptr[c] = *alpha_ptr;
        alpha_ptr += 4;
    }

    if ((ret = av_hwframe_transfer_data(hw_frame, frame, 0)) < 0) {
        qDebug() << "error transfer data" << av_err2str(ret);
        return "transfer data";
    }

    ret = avcodec_send_frame(ctx, hw_frame);
    if (ret < 0) {
        qDebug() << "error sending frame for encoding" << av_err2str(ret);
        return "send frame";
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(ctx, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            qDebug() << "error recving packet for encoding";
            return "recv packet";
        }

        //packet->pos = -1;
        //packet->stream_index = 0;
        packet->dts = packet->pts = pts++;
        ret = av_interleaved_write_frame(dbg, packet);
        if (ret < 0) {
            qDebug() << "error write debug" << av_err2str(ret);
        }
        //if (onPacketReceived != nullptr)
            //onPacketReceived(packet->data, packet->size);

        qDebug() << "size" << packet->size;
        av_packet_unref(packet);
    }

    auto ee = t.nsecsElapsed();
    qDebug() << "elapsed" << ee ;

    return std::optional<QString>();
}

void ndi2av::stop()
{
    if (!inited)
        return;
    avcodec_free_context(&ctx);
    av_frame_free(&frame);
    av_packet_free(&packet);
    av_buffer_unref(&hw_device);

    av_write_trailer(dbg);
    avio_closep(&dbg->pb);
    avformat_close_input(&dbg);
}

void ndi2av::iterateCodec()
{
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

void ndi2av::iterateCodecHwConstraint(AVBufferRef *dev, AVCodec *codec)
{
    std::stringstream ss;
    ss << codec->name << " hw:" << std::endl;
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config) {
            break;
        }
        auto name = av_get_pix_fmt_name(config->pix_fmt);
        ss << (name == nullptr ? "none" : name) << " " << config->methods << " " << config->device_type << std::endl;
        auto cs = av_hwdevice_get_hwframe_constraints(dev, config);
        ss << "valid hw formats" << std::endl;
        AVPixelFormat fmt;
        int idx = 0;
        while (true) {
            fmt = cs->valid_hw_formats[idx++];
            if (fmt == AV_PIX_FMT_NONE)
                break;
            ss << av_get_pix_fmt_name(fmt) << " ";
        }
        ss << std::endl << "valid sw formats" << std::endl;
        idx = 0;
        while (true) {
            fmt = cs->valid_sw_formats[idx++];
            if (fmt == AV_PIX_FMT_NONE)
                break;
            ss << av_get_pix_fmt_name(fmt) << " ";
        }
        ss << std::endl;
    }
    qDebug() << ss.str().c_str();
}
