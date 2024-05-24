#ifndef AV_TO_D3D_H
#define AV_TO_D3D_H

#include "core/debug_center.h"
#include "core/util.h"
#include "i_d3d_src.h"
#include "nv12_to_bgra.h"
#include <cstdint>
#include <QString>
#include <SpoutDX/SpoutDX.h>

#include "qthread.h"
#include "proto/vts.pb.h"

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
    bool isKey;
    int64_t pts;
    std::unique_ptr<vts::VtsMsg> data;
};

struct FrameReorderer {
    bool operator()(const UnorderedFrame* l, const UnorderedFrame* r) const { return l->pts > r->pts; }
};

class FrameDelay {
    int _succeedCombo = 0;
    int _delay = 0;
    int _delayAccumulate = 0;
    std::uint64_t _lastDelayIncrease = 0;

public:
    inline void failed() {
        uint64_t time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (time - _lastDelayIncrease > 100000) {
            _lastDelayIncrease = time;
            _delay = _delayAccumulate;
            _delayAccumulate += 5;
            if (_delayAccumulate > 30)
                _delayAccumulate = 30;
        }
    }

    inline void succeed() {
        _delay = 0;
        if (_succeedCombo >= 60) {
            _delayAccumulate -= 5;
            if (_delayAccumulate < 0)
                _delayAccumulate = 1;
            _succeedCombo = 0;
        }
    }

    inline int delay() {
        return _delay;
    }

    inline void reset() {
        _delay = 0;
        _delayAccumulate = 0;
        _succeedCombo = 0;
        _lastDelayIncrease = 0;
    }
};

// This class is used by server to decode received ffmpeg packets
// Then use d3d11 accelerated nv12 to bgra converter to save the frame to a d3d11texture2d
// And the instance is a IDxSrc which will be registered to room's merger DxToNdi, which
// will combine all sources textures there.
class DxToFrame;

class AvToDx : public IDxToFrameSrc, public IDebugCollectable {
private:
    std::string peerId;
    std::unique_ptr<Nv12ToBgra> bgra;

    std::atomic_bool inited = false;
    int _width, _height;
    float frameRate;

    AVCodecID codecId;

    AVPacket* packet = nullptr;
    //rgb
    AVCodecContext* ctx = nullptr;
    const AVCodec* codec = nullptr;
    AVFrame* frame = nullptr;

    int64_t pts = 0;

    FpsCounter fps;

    std::optional<QString> initCodec(AVCodecID);

    std::atomic_bool enableBuffering;
    std::atomic_bool processThreadRunning = false;
    std::unique_ptr<QThread> processThread;
    std::priority_queue<UnorderedFrame*, std::vector<UnorderedFrame*>, FrameReorderer> frameQueue;
    std::atomic_int queueSize = 0;
    QMutex frameQueueLock;
    FrameDelay frameDelay;

    std::string nick;
    std::unique_ptr<spoutDX> spoutOutput;

    inline int frameQueueSize() {
        int ret = 0;
        frameQueueLock.lock();
        ret = frameQueue.size();
        frameQueueLock.unlock();
        return ret;
    }

    void processWorker();
    std::optional<QString> processFrame();

public:
    AvToDx(const std::string& peerId, const std::string& nick, FrameQualityDesc q,
           const std::shared_ptr<DxToFrame>& d3d, bool isServer);
    ~AvToDx();

    std::optional<QString> init();
    void process(std::unique_ptr<vts::VtsMsg> m);
    void reset();
    void stop();

    QString debugInfo();

    bool copyTo(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* dest) override;

    inline void setNick(const std::string& nick) {
        this->nick = nick;
        const auto suffix = nick.empty() ? " [Client]" : " (" + this->nick + ")";
        if (spoutOutput) {
            spoutOutput->SetSenderName(("VLink 联动" + suffix).c_str());
        }
    }
};

#endif // AV_TO_D3D_H
