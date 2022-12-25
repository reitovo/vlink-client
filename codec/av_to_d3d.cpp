#include "av_to_d3d.h"
#include "d3d_to_ndi.h"
#include "qdebug.h"
#include "qelapsedtimer.h"
#include "core/vtslink.h"
#include <d3d11.h>
#include <QThread>

extern "C" {
#include "libavutil/error.h"
#include "libavutil/hwcontext_d3d11va.h"
}

static char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };
#undef av_err2str
#define av_err2str(errnum) av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)

static enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    const enum AVPixelFormat *p;

    for (p = pix_fmts; *p != -1; p++) {
        if (*p == AV_PIX_FMT_D3D11)
            return *p;
    }

    fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

AvToDx::AvToDx(std::shared_ptr<DxToNdi> d3d) : IDxSrc(d3d)
{
    qDebug() << "begin d3d2ndi";
    d3d->registerSource(this);
    init();
}

AvToDx::~AvToDx()
{
    qDebug() << "end d3d2ndi";
    d3d->unregisterSource(this);
    stop();
    qDebug() << "end d3d2ndi done";
}

std::optional<QString> AvToDx::init()
{
    if (inited)
        stop();

    qDebug() << "av2d3d init";

    xres = 1920;
    yres = 1080;
    frameD = 1001;
    frameN = 60000;
    codecId = AV_CODEC_ID_H264;

    qDebug() << "av2d3d using codec" << avcodec_get_name(codecId);

    bgra = std::make_unique<Nv12ToBgra>();
    if (!bgra->init()) {
        qDebug() << "failed to init nv12bgra";
        return "init nv12 to bgra";
    }

    auto err = initRgb(codecId);
    if (err.has_value()) {
        qDebug() << "failed to init av2d3d rgb";
        return "init av2d3d rgb";
    }
    err = initA(codecId);
    if (err.has_value()) {
        qDebug() << "failed to init av2d3d a";
        return "init av2d3d a";
    }

    packet = av_packet_alloc();
    if (!packet) {
        qDebug("Failed to allocate AVPacket\n");
        return "av packet alloc";
    }

    inited = true;

    qDebug() << "av2d3d init done";

    return std::optional<QString>();
}

std::optional<QString> AvToDx::initRgb(AVCodecID codec_id)
{
    int err;

    codec_rgb = avcodec_find_decoder(codec_id);
    if (!codec_rgb) {
        qDebug("Cannot open codec\n");
        return "find av codec";
    }

    ctx_rgb = avcodec_alloc_context3(codec_rgb);
    if (!ctx_rgb) {
        qDebug("Could not allocate video codec context\n");
        return "codec alloc ctx";
    }

    ctx_rgb->width = xres;
    ctx_rgb->height = yres;
    ctx_rgb->time_base.den = ctx_rgb->framerate.num = frameD;
    ctx_rgb->time_base.num = ctx_rgb->framerate.den = frameN;
    ctx_rgb->pkt_timebase = ctx_rgb->time_base;
    ctx_rgb->pix_fmt = AV_PIX_FMT_D3D11;
    ctx_rgb->get_format = get_hw_format;

//    AVBufferRef* hw;
//    if ((err = av_hwdevice_ctx_create(&hw, AV_HWDEVICE_TYPE_D3D11VA, NULL, NULL, 0)) < 0) {
//        fprintf(stderr, "Failed to create specified HW device.\n");
//        return "create hw";
//    }
//    ctx_rgb->hw_device_ctx = av_buffer_ref(hw);
//    av_buffer_unref(&hw);

    AVBufferRef* hw2 = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    AVHWDeviceContext* device_ctx = reinterpret_cast<AVHWDeviceContext*>(hw2->data);
    AVD3D11VADeviceContext* d3d11va_device_ctx = reinterpret_cast<AVD3D11VADeviceContext*>(device_ctx->hwctx);
    d3d11va_device_ctx->device = bgra->getDevice();
    if (d3d11va_device_ctx->device == nullptr) {
        qDebug("av2d3d d3d11 device null");
        return "open d3d11 device";
    }

    ctx_rgb->hw_device_ctx = av_buffer_ref(hw2);
    av_hwdevice_ctx_init(ctx_rgb->hw_device_ctx);
    av_buffer_unref(&hw2);

    if ((err = avcodec_open2(ctx_rgb, codec_rgb, nullptr)) < 0) {
        qDebug("Could not open codec %s\n", av_err2str(err));
        return "open codec";
    }

    frame_rgb = av_frame_alloc();
    if (!frame_rgb) {
        qDebug("Could not alloc sw frame\n");
        return "frame alloc";
    }
    frame_rgb->format = AV_PIX_FMT_NV12;
    frame_rgb->width = ctx_rgb->width;
    frame_rgb->height = ctx_rgb->height;

    return std::optional<QString>();
}

std::optional<QString> AvToDx::initA(AVCodecID codec_id)
{
    int err;

    codec_a = avcodec_find_decoder(codec_id);
    if (!codec_a) {
        qDebug("Cannot open codec\n");
        return "find av codec";
    }

    ctx_a = avcodec_alloc_context3(codec_a);
    if (!ctx_a) {
        qDebug("Could not allocate video codec context\n");
        return "codec alloc ctx";
    }

    ctx_a->width = xres;
    ctx_a->height = yres;
    ctx_a->time_base.den = ctx_a->framerate.num = frameD;
    ctx_a->time_base.num = ctx_a->framerate.den = frameN;
    ctx_a->pkt_timebase = ctx_a->time_base;
    ctx_a->pix_fmt = AV_PIX_FMT_D3D11;
    ctx_a->get_format = get_hw_format;

//    AVBufferRef* hw;
//    if ((err = av_hwdevice_ctx_create(&hw, AV_HWDEVICE_TYPE_D3D11VA, NULL, NULL, 0)) < 0) {
//        fprintf(stderr, "Failed to create specified HW device.\n");
//        return "create hw";
//    }
//    ctx_a->hw_device_ctx = av_buffer_ref(hw);
//    av_buffer_unref(&hw);

    AVBufferRef* hw2 = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    AVHWDeviceContext* device_ctx = reinterpret_cast<AVHWDeviceContext*>(hw2->data);
    AVD3D11VADeviceContext* d3d11va_device_ctx = reinterpret_cast<AVD3D11VADeviceContext*>(device_ctx->hwctx);
    d3d11va_device_ctx->device = bgra->getDevice();
    if (d3d11va_device_ctx->device == nullptr) {
        qDebug("av2d3d d3d11 device null");
        return "open d3d11 device";
    }

    ctx_a->hw_device_ctx = av_buffer_ref(hw2);
    av_hwdevice_ctx_init(ctx_a->hw_device_ctx);
    av_buffer_unref(&hw2);

    if ((err = avcodec_open2(ctx_a, codec_a, nullptr)) < 0) {
        qDebug("Could not open codec %s\n", av_err2str(err));
        return "open codec";
    }

    frame_a = av_frame_alloc();
    if (!frame_a) {
        qDebug("Could not alloc sw frame\n");
        return "frame alloc";
    }
    frame_a->format = AV_PIX_FMT_NV12;
    frame_a->width = ctx_a->width;
    frame_a->height = ctx_a->height;

    return std::optional<QString>();
}

std::optional<QString> AvToDx::process(std::unique_ptr<VtsMsg> m)
{
    // enqueue for reordering
    UnorderedFrame* f = new UnorderedFrame;
    f->pts = m->avframe().pts();
    f->data = std::move(m);
    frameQueue.push(f);

    if (frameQueue.size() < 30) {
        return "buffering";
    }

    auto dd = frameQueue.top();
    frameQueue.pop();

    auto mem = std::move(dd->data);
    auto newPts = dd->pts;
    delete dd;

    if (newPts <= pts) {
        qDebug() << "misordered" << newPts << pts;  
    }

    auto meta = mem->avframe();
    //qDebug() << "av2d3d pts" << meta.pts();
    pts = meta.pts(); 

    if (!inited) {
        qDebug() << "not inited";
        return "not inited";
    }

    int ret;

    QElapsedTimer t;
    t.start();

    QStringList errList; 

    for (auto& a : meta.rgbpackets()) {
        auto& d = a.data();
        packet->data = (uint8_t*) d.data();
        packet->size = d.size();
        packet->dts = a.dts();
        packet->pts = a.pts();
        ret = avcodec_send_packet(ctx_rgb, packet);
        if (ret < 0) {
            qDebug() << "error sending packet for rgb decoding" << av_err2str(ret);
            errList.append("send packet");
        }
    }

    //qDebug() << "recv frame rgb";
     
    ret = avcodec_receive_frame(ctx_rgb, frame_rgb);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret < 0) {
        qDebug() << "error while decoding rgb" << av_err2str(ret);
        errList.append("receive frame");
    }
    else {
        bgra->enqueueRgb(frame_rgb);
    }

    for (auto& a : meta.apackets()) {
        auto& d = a.data();
        packet->data = (uint8_t*)d.data();
        packet->size = d.size();
        packet->dts = a.dts();
        packet->pts = a.pts();
        ret = avcodec_send_packet(ctx_a, packet);
        if (ret < 0) {
            qDebug() << "error sending packet for a decoding" << av_err2str(ret);
            errList.append("send packet");
        }
    }

    //qDebug() << "recv frame a";
     
    ret = avcodec_receive_frame(ctx_a, frame_a);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret < 0) {
        qDebug() << "error while decoding a" << av_err2str(ret);
        errList.append("receive frame"); 
    }
    else {
        bgra->enqueueA(frame_a);
    }

    //qDebug() << "to bgra";

    //qDebug() << "rgb" << frame_rgb->coded_picture_number << meta.rgbpackets_size() << "a" << frame_a->coded_picture_number << meta.apackets_size();

    if (!errList.empty()) {
        auto e = errList.join(", ");
        qDebug() << "one or more error occoured" << e;   
    } 
      
    bgra->nv12ToBgra(); 

    fps.add(t.nsecsElapsed()); 

//    auto ee = t.nsecsElapsed();
//    qDebug() << "av2d3d elapsed" << ee;
    return std::optional<QString>();
}

void AvToDx::reset()
{
    qDebug() << "av2d3d reset";
    pts = 0;
}

void AvToDx::stop()
{
    if (!inited)
        return;

    qDebug() << "av2d3d stop";
    pts = 0;
    inited = false;
    bgra = nullptr;

    av_packet_free(&packet);
    av_frame_free(&frame_rgb);
    av_frame_free(&frame_a);  
    avcodec_free_context(&ctx_rgb);
    avcodec_free_context(&ctx_a);
}

QString AvToDx::debugInfo()
{
    return QString("Av->Dx (Stream Decoder) %1").arg(fps.stat());
}

bool AvToDx::copyTo(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D *dest)
{
    if (!inited)
        return false;
    return bgra->copyTo(dev, ctx, dest);
}
