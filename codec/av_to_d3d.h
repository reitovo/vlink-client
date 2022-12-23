#ifndef AV_TO_D3D_H
#define AV_TO_D3D_H

#include "core/debugcenter.h"
#include "core/util.h"
#include "i_d3d_src.h"
#include "nv12_to_bgra.h"
#include <cstdint>
#include <Processing.NDI.Lib.h>
#include <QString>

#include "avframe.pb.h"
#include "qthread.h"
#include "vts.pb.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libswscale/swscale.h"
#include "libavutil/hwcontext.h"
#include "libavutil/pixdesc.h"
}

#include <queue>

struct UnorderedFrame {
    int pts;
    std::unique_ptr<VtsMsg> data;
};

struct FrameReorderer {
    bool operator() (const UnorderedFrame* l, const UnorderedFrame* r) const { return l->pts > r->pts; }
};

// This class is used by server to decode received ffmpeg packets
// Then use d3d11 accelerated nv12 to bgra converter to save the frame to a d3d11texture2d
// And the instance is a IDxSrc which will be registered to room's merger DxToNdi, which
// will combine all sources textures there.
class DxToNdi;
class AvToDx : public IDxSrc, public IDebugCollectable {

private:
    std::unique_ptr<Nv12ToBgra> bgra;

    std::atomic_bool inited = false;
    int xres, yres, frameD, frameN;
    AVCodecID codecId;

    AVPacket *packet = nullptr;
    //rgb
    AVCodecContext *ctx_rgb = nullptr;
    const AVCodec* codec_rgb = nullptr;
    AVFrame* frame_rgb = nullptr;
    //alpha
    AVCodecContext *ctx_a = nullptr;
    const AVCodec* codec_a = nullptr;
    AVFrame* frame_a = nullptr;

    int pts = 0;

    FpsCounter fps;

    std::optional<QString> initRgb(AVCodecID);
    std::optional<QString> initA(AVCodecID);

    QThread processThread;

    std::priority_queue<UnorderedFrame*, std::vector<UnorderedFrame*>, FrameReorderer> frameQueue;

public:
    AvToDx(std::shared_ptr<DxToNdi> d3d);
    ~AvToDx();

    bool isInited();
    std::optional<QString> init();
    std::optional<QString> process(std::unique_ptr<VtsMsg> m);
    void reset();
    void stop();

    QString debugInfo();

    bool copyTo(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D *dest);

    void iterateCodec();
    void iterateHwAccels();
    void iterateCodecHwConstraint(AVBufferRef *dev, AVCodec* codec);
};

#endif // AV_TO_D3D_H
