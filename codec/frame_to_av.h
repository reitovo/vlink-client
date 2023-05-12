#ifndef FRAME_TO_AV_H
#define FRAME_TO_AV_H

#include "bgra_to_nv12.h"
#include "core/debug_center.h"
#include "core/util.h"
#include "d3d_to_frame.h"
#include <cstdint>
#include <QString>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libswscale/swscale.h"
#include "libavutil/hwcontext.h"
#include "libavutil/pixdesc.h"
}
#include "proto/vts.pb.h"

enum FrameToAvMode {
    FRAME_TO_AV_MODE_INVALID = 0,

    FRAME_TO_AV_TYPE_DXFULL = 0b001,
    FRAME_TO_AV_TYPE_DXMAP = 0b010,
    FRAME_TO_AV_TYPE_LIBYUV = 0b100,

    FRAME_TO_AV_FMT_BGRA = 0b01 << 3,
    FRAME_TO_AV_FMT_UYVA = 0b10 << 3,

    FRAME_TO_AV_HW_CPU = 0,
    FRAME_TO_AV_HW_D3D11 = 0b01 << 5,
    FRAME_TO_AV_HW_QSV = 0b10 << 5,

    // Use D3D11 based hardware acceleration which directly use ID3D11Texture2D as input
    FRAME_TO_AV_MODE_DXFULL_D3D11 = FRAME_TO_AV_TYPE_DXFULL | FRAME_TO_AV_FMT_BGRA | FRAME_TO_AV_HW_D3D11,
    // Use QSV based hardware acceleration which directly use ID3D11Texture2D as input
    FRAME_TO_AV_MODE_DXFULL_QSV = FRAME_TO_AV_TYPE_DXFULL | FRAME_TO_AV_FMT_BGRA | FRAME_TO_AV_HW_QSV,
    // Use hardware accelerated encoder but it can only use RAM as input, so we need to map the shader
    // based BGRA to NV12 convertion result to memory, which is an overhead.
    FRAME_TO_AV_MODE_DXMAP = FRAME_TO_AV_TYPE_DXMAP | FRAME_TO_AV_FMT_BGRA | FRAME_TO_AV_HW_CPU,
    // Don't use any hardware acceleration, use CPU to convert BGRA to NV12 as input
    FRAME_TO_AV_MODE_LIBYUV_BGRA = FRAME_TO_AV_TYPE_LIBYUV | FRAME_TO_AV_FMT_BGRA | FRAME_TO_AV_HW_CPU,
    // Don't use any hardware acceleration, use CPU to copy UYVA to NV12 as input, should be better than
    // BGRA to NV12 as it is pure copy. (Will it cost more on NDI side?)
    FRAME_TO_AV_MODE_LIBYUV_UYVA = FRAME_TO_AV_TYPE_LIBYUV | FRAME_TO_AV_FMT_UYVA | FRAME_TO_AV_HW_CPU
};

struct CodecOption {
    QString name;
    AVCodecID codecId;
    FrameToAvMode mode;
    QString readable;
};

// This is used by clients to encode NDI source which should comes from VTube Studio
// to two ffmpeg sources, Because alpha channel is essential, so there'll be 2 streams
// after encoded by ffmpeg the packet is send to remote server.
class FrameToAv : public IDebugCollectable {

private:
    std::unique_ptr<BgraToNv12> nv12;

private:
    bool inited = false;
    int _width, _height;
    float frameRate;
    int frameQuality;

    AVPacket *packet = nullptr;

    AVCodecContext *ctx = nullptr;
    const AVCodec* codec = nullptr;
    AVFrame* frame = nullptr;

    CodecOption currentOption;

    // If the client joins after the encoding began, we need to send another IDR frame.
    std::atomic_bool requestIdr = false;
    std::atomic_int requestIdrCoolDown = 0;
    int regularIdrCount = 0;
    int multipleIdrCount = 0;

    int64_t pts = 0;
    std::function<void(std::shared_ptr<vts::VtsMsg>)> onPacketReceived = nullptr;

    FpsCounter fps;

    std::optional<QString> processInternal();

public:
    FrameToAv(FrameQualityDesc q, std::function<void(std::shared_ptr<vts::VtsMsg>)> cb);
    ~FrameToAv();

    QString debugInfo();
    bool useUYVA();

    bool isInited();
    std::optional<QString> init(bool forceBgra);
    std::optional<QString> initCodec(const CodecOption& option);

    std::optional<QString> initOptimalEncoder(const CodecOption& option, AVCodecContext * ctx);
    void initEncodingParameter(const CodecOption& option, AVCodecContext * ctx);

    std::optional<QString> processFast(const std::shared_ptr<IDxCopyable>& fast);
    void stop();

    static const QList<CodecOption>& getEncoders();

    inline void forceIdr() {
        requestIdr = true;
    }
};

#endif // FRAME_TO_AV_H
