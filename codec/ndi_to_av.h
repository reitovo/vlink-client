#ifndef NDI_TO_AV_H
#define NDI_TO_AV_H

#include "bgra_to_nv12.h"
#include "core/debugcenter.h"
#include "core/util.h"
#include "d3d_to_ndi.h"
#include <cstdint>
#include <Processing.NDI.Lib.h>
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
#include "avframe.pb.h"
#include "vts.pb.h"

enum NdiToAvMode {
    NDI_TO_AV_MODE_INVALID,
    NDI_TO_AV_MODE_DXFULL,
    NDI_TO_AV_MODE_DXMAP,
    NDI_TO_AV_MODE_LIBYUV
};

struct CodecOption {
    QString name;
    AVCodecID codecId;
    NdiToAvMode mode;
};

// This is used by clients to encode NDI source which should comes from VTube Studio
// to two ffmpeg sources, Because alpha channel is essential, so there'll be 2 streams
// after encoded by ffmpeg the packet is send to remote server.
class NdiToAv : public IDebugCollectable {

private:
    std::unique_ptr<BgraToNv12> nv12;

private:
    bool inited = false;
    int xres, yres, frameD, frameN, frameType, fourCC;
    AVCodecID codecId;
    QString codec;

    AVPacket *packet = nullptr;
    //rgb
    AVCodecContext *ctx_rgb = nullptr;
    const AVCodec* codec_rgb = nullptr;
    AVFrame* frame_rgb = nullptr;
    //alpha
    AVCodecContext *ctx_a = nullptr;
    const AVCodec* codec_a = nullptr;
    AVFrame* frame_a = nullptr;

    NdiToAvMode mode = NDI_TO_AV_MODE_INVALID;

    int pts = 0;
    std::function<void(std::shared_ptr<VtsMsg>)> onPacketReceived = nullptr;

    FpsCounter fps;

public:
    NdiToAv(std::function<void(std::shared_ptr<VtsMsg>)> cb);
    ~NdiToAv();

    QString debugInfo();

    bool isInited();
    std::optional<QString> init(int xres, int yres, int d, int n, int type, int cc);
    std::optional<QString> initRgb(const CodecOption& option);
    std::optional<QString> initA(const CodecOption& option);

    std::optional<QString> initOptimalEncoder(const CodecOption& option, AVCodecContext * ctx);
    void initEncodingParameter(const CodecOption& option, AVCodecContext * ctx);

    std::optional<QString> process(NDIlib_video_frame_v2_t* ndi, std::shared_ptr<DxToNdi> fast = nullptr);
    void stop();
};

#endif // NDI_TO_AV_H
