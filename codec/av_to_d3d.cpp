#include "av_to_d3d.h"
#include "d3d_to_frame.h"
#include "qdebug.h"
#include "qelapsedtimer.h"
#include "core/vtslink.h"
#include <d3d11.h>
#include <QThread>
#include <QSettings>

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

AvToDx::AvToDx(int width, int height, float frameRate, std::shared_ptr<DxToFrame> d3d) : IDxToFrameSrc(d3d)
{
    qDebug() << "begin d3d2dx";
    d3d->registerSource(this);

    QSettings settings;
    enableBuffering = settings.value("enableBuffering", false).toBool();

    _width = width;
    _height = height;
    this->frameRate = frameRate;

    init();
}

AvToDx::~AvToDx()
{
    qDebug() << "end d3d2dx";
    d3d->unregisterSource(this);
    stop();
    qDebug() << "end d3d2dx done";
}

std::optional<QString> AvToDx::init()
{
    if (inited)
        stop();

    qDebug() << "av2d3d init";
    codecId = AV_CODEC_ID_H264;

    qDebug() << "av2d3d using codec" << avcodec_get_name(codecId);

    bgra = std::make_unique<Nv12ToBgra>(_width, _height);
    if (!bgra->init()) {
        qDebug() << "failed to init nv12bgra";
        return "init nv12 to bgra";
    }

    auto err = initCodec(codecId);
    if (err.has_value()) {
        qDebug() << "failed to init av2d3d rgb";
        return "init av2d3d rgb";
    }

    packet = av_packet_alloc();
    if (!packet) {
        qDebug("Failed to allocate AVPacket\n");
        return "av packet alloc";
    }

    processThreadRunning = true;
    processThread = std::unique_ptr<QThread>(QThread::create([this]() {
        processWorker();
    }));
    processThread->setObjectName("AvToDx Worker");
    processThread->start();

    inited = true;

    qDebug() << "av2d3d init done";

    return {};
}

std::optional<QString> AvToDx::initCodec(AVCodecID codec_id)
{
    int err;

    codec = avcodec_find_decoder(codec_id);
    if (!codec) {
        qDebug("Cannot open codec\n");
        return "find av codec";
    }

    ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        qDebug("Could not allocate video codec context\n");
        return "codec alloc ctx";
    }

    ctx->width = _width;
    ctx->height = _height * 2;
    ctx->time_base.den = ctx->framerate.num = 1;
    ctx->time_base.num = ctx->framerate.den = frameRate;
    ctx->pkt_timebase = ctx->time_base;
    ctx->pix_fmt = AV_PIX_FMT_D3D11;
    ctx->get_format = get_hw_format;

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

    ctx->hw_device_ctx = av_buffer_ref(hw2);
    av_hwdevice_ctx_init(ctx->hw_device_ctx);
    av_buffer_unref(&hw2);

    if ((err = avcodec_open2(ctx, codec, nullptr)) < 0) {
        qDebug("Could not open codec %s\n", av_err2str(err));
        return "open codec";
    }

    frame = av_frame_alloc();
    if (!frame) {
        qDebug("Could not alloc sw frame\n");
        return "frame alloc";
    }
    frame->format = AV_PIX_FMT_NV12;
    frame->width = ctx->width;
    frame->height = ctx->height;

    return std::optional<QString>();
}

void AvToDx::process(std::unique_ptr<vts::VtsMsg> m)
{
    auto packet_pts = m->avframe().pts();

    frameQueueLock.lock();
    if (packet_pts < pts + 1) {
        qDebug() << "discard rendered packet = " << packet_pts << " expect = " << (pts + 1);
    } else {
        // enqueue for reordering
        auto f = new UnorderedFrame;
        f->pts = packet_pts;
        f->data = std::move(m);
        frameQueue.push(f);
    }
    frameQueueLock.unlock();
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
    inited = false;

    processThreadRunning = false;
    terminateQThread(processThread); 

    pts = 0;

    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);

    bgra = nullptr;
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

void AvToDx::processWorker() {
    int64_t frameCount = 0;
    int64_t startTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    while (processThreadRunning) {

        auto err = processFrame();
        if (!err.has_value()) {
            if (!enableBuffering) {
                if (frameQueueSize() > 0) {
                    startTime -= 2000;
                }
            }
        }

        frameCount++;
        int64_t frameTime = frameCount * 1000000.0 / frameRate;
        int64_t nextTime = startTime + frameTime;
        int64_t currentTime = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        auto sleepTime = nextTime - currentTime;
        if (sleepTime > 0) {
            QThread::usleep(sleepTime);
        }
    }
}

std::optional<QString> AvToDx::processFrame() {
    frameQueueLock.lock();

    auto delay = frameDelay.delay();
    if (!enableBuffering)
        delay = 0;

    if (frameQueue.size() <= delay) {
        if (delay == 0) {
            pts = 0;
            frameDelay.failed();
        }
        frameQueueLock.unlock();
        return QString("buffering %1 %2").arg(delay).arg(frameQueue.size());
    }

    // We can't wait for an ordered frame in 1 seconds.
    if (frameQueue.size() > 30) {
        while(!frameQueue.empty()) {
            delete frameQueue.top();
            frameQueue.pop();
        }
        pts = 0;
        frameQueueLock.unlock();
        frameDelay.reset();
        return "resetting";
    }

    auto dd = frameQueue.top();
    auto newPts = dd->pts;

    if (pts != 0 && newPts > pts + 1) {
        frameDelay.failed();
        qDebug() << "misordered latest = " << newPts << " expect = " << (pts + 1) << frameQueue.size();
        frameQueueLock.unlock();
        return "misordered";
    }

    frameDelay.succeed();
    frameQueue.pop();
    frameQueueLock.unlock();

    auto mem = std::move(dd->data);
    delete dd;

    auto meta = mem->avframe();
    if (newPts == pts + 1 || pts == 0) {
        pts = newPts;
    }
    else {
        qDebug() << "discontinued" << pts;
        return "discontinued";
    }
    //qDebug() << "av2d3d pts" << meta.pts();

    if (!inited) {
        qDebug() << "not inited";
        return "not inited";
    }

    int ret;

    QElapsedTimer t;
    t.start();

    QStringList errList;

    for (auto& a : meta.packets()) {
        auto& d = a.data();
        packet->data = (uint8_t*) d.data();
        packet->size = d.size();
        packet->dts = a.dts();
        packet->pts = a.pts();
        ret = avcodec_send_packet(ctx, packet);
        if (ret < 0) {
            qDebug() << "error sending packet for decoding" << av_err2str(ret);
            errList.append("send packet");
        }
    }

    //qDebug() << "recv frame rgb";

    ret = avcodec_receive_frame(ctx, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret < 0) {
        qDebug() << "error while decoding rgb" << av_err2str(ret);
        errList.append("receive frame");
    }

    //qDebug() << "to bgra";

    //qDebug() << "rgb" << frame_rgb->pts << meta.rgbpackets_size() << "a" << frame_a->pts << meta.apackets_size();

    if (!errList.empty()) {
        auto e = errList.join(", ");
        qDebug() << "one or more error occoured" << e;
    }

    bgra->nv12ToBgra(frame);

    fps.add(t.nsecsElapsed());

    return {};
}
