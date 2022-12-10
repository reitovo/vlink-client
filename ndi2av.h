#ifndef NDI2AV_H
#define NDI2AV_H

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

class ndi2av {
private:
    bool inited = false;
    int xres, yres, frameD, frameN, frameType, fourCC;

    AVPacket *packet = nullptr;
    AVCodecContext *ctx = nullptr;
    AVCodec* codec = nullptr;
    AVFrame* sw_frame = nullptr;
    AVFrame* hw_frame = nullptr;
    AVBufferRef *hw_device = nullptr;

    AVFormatContext* dbg = nullptr;
    AVStream* dout = nullptr;

    int pts = 0;
    std::function<void(uint8_t*, int)> onPacketReceived = nullptr;

public:
    bool isInited();
    std::optional<QString> init(int xres, int yres, int d, int n, int type, int cc);
    void setOnPacketReceived(std::function<void(uint8_t*, int)> f);
    std::optional<QString> process(NDIlib_video_frame_v2_t* ndi);
    void stop();
    void iterateCodec();
    void iterateCodecHwConstraint(AVBufferRef *dev, AVCodec* codec);
};

#endif // NDI2AV_H
