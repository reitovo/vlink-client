#include "frame_to_av.h"
#include "core/util.h"
#include "libavutil/hwcontext_d3d11va.h"
#include "libavutil/hwcontext_qsv.h"
#include "qdebug.h"
#include "qelapsedtimer.h"
#include <QStringList>
#include <sstream>
#include "core/vtslink.h"
#include "libyuv.h"
#include <qsettings.h>
#include "mfx/mfxcommon.h"

static char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };
#undef av_err2str
#define av_err2str(errnum) av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)

static QList<CodecOption> options = { 
	// Full Hardware Acceleration
	{"h264_nvenc", AV_CODEC_ID_H264, FRAME_TO_AV_MODE_DXFULL_D3D11, "NVENC Native (H.264)"}, // Least CPU usage, fastest approach
	{"h264_qsv", AV_CODEC_ID_H264, FRAME_TO_AV_MODE_DXFULL_QSV,"QSV Native (H.264)"}, // Best quality, slightly slower
	{"h264_amf", AV_CODEC_ID_H264, FRAME_TO_AV_MODE_DXFULL_D3D11, "AMF Native (H.264)"}, // Good

	// Try mapped, as app might run on different GPU, it might perform bad.
	{"h264_nvenc", AV_CODEC_ID_H264, FRAME_TO_AV_MODE_DXMAP, "NVENC Mapped (H.264)"},
	{"h264_amf", AV_CODEC_ID_H264, FRAME_TO_AV_MODE_DXMAP, "AMF Mapped (H.264)"},
	{"h264_qsv", AV_CODEC_ID_H264, FRAME_TO_AV_MODE_DXMAP, "QSV Mapped (H.264)"},

	// You don't have a GPU? 
	//{"libx264", AV_CODEC_ID_H264, FRAME_TO_AV_MODE_LIBYUV_UYVA, "X264 UYVA (H.264)"}, // Cost less CPU
	//{"libx264", AV_CODEC_ID_H264, FRAME_TO_AV_MODE_LIBYUV_BGRA, "X264 BGRA (H.264)"}, // Cost more CPU
};

FrameToAv::FrameToAv(std::function<void(std::shared_ptr<VtsMsg>)> cb) {
	qDebug() << "begin dx2av";
	onPacketReceived = cb;
}

FrameToAv::~FrameToAv()
{
	qDebug() << "end dx2av";
	stop();
	qDebug() << "end dx2av done";
}

QString FrameToAv::debugInfo()
{
	return QString("Frame->Av (Stream Encoder) %1 Codec: %2").arg(fps.stat()).arg(currentOption.readable);
}

bool FrameToAv::useUYVA()
{
	return (currentOption.mode & FRAME_TO_AV_FMT_UYVA) != 0;
}

bool FrameToAv::isInited() {
	return inited;
}

static AVPixelFormat getPixelFormatOf(FrameToAvMode mode) {
	switch (mode) {
	case FRAME_TO_AV_MODE_DXMAP:
	case FRAME_TO_AV_MODE_LIBYUV_BGRA:
	case FRAME_TO_AV_MODE_LIBYUV_UYVA:
		return AV_PIX_FMT_NV12;
	case FRAME_TO_AV_MODE_DXFULL_D3D11:
		return AV_PIX_FMT_D3D11;
	case FRAME_TO_AV_MODE_DXFULL_QSV:
		return AV_PIX_FMT_QSV;
	}
	return AV_PIX_FMT_NONE;
}

std::optional<QString> FrameToAv::init(int xres, int yres, int d, int n, bool forceBgra) {
	this->xres = xres;
	this->yres = yres;
	this->frameD = d;
	this->frameN = n; 

	qDebug() << "dx2av init";

	nv12 = std::make_unique<BgraToNv12>();
	if (!nv12->init()) {
		qDebug() << "failed to init bgranv12";
		return "init bgra to nv12";
	}

	QList<CodecOption> opts = options;

	QSettings settings;
	auto forceEncoder = settings.value("forceEncoder", false).toBool();
	if (forceEncoder) {
		qDebug() << "forcing encoder";
		auto name = settings.value("forceEncoderName").toString();
		auto opt = std::find_if(options.begin(), options.end(), [&](const CodecOption& item) {
			return item.readable == name;
			});
		if (opt != options.end()) {
			opts.clear();
			opts.append(*opt);
			qDebug() << "forced encoder" << (*opt).readable;
		}
	}

	bool selected = false;
	std::optional<QString> err;
	for (auto& o : opts) {
		if (forceBgra && (o.mode & FRAME_TO_AV_FMT_UYVA))
			continue;

		auto& e = o.name;
		err = initCodec(o);
		if (err.has_value()) {
			qWarning() << "dx2av init encoder" << o.readable << "failed" << err.value();
			stop();
			continue;
		}
		qDebug() << "dx2av init encoder" << o.readable << "succeed";

		int code;
		if (o.mode & FRAME_TO_AV_TYPE_DXFULL) {
			if ((code = av_hwframe_get_buffer(ctx->hw_frames_ctx, frame, 0)) != 0) {
				qWarning("Could not get hw frame rgb %s\n", av_err2str(code));
				stop();
				continue;
			}
		}
		else if (o.mode & FRAME_TO_AV_TYPE_LIBYUV) {
			if ((code = av_frame_get_buffer(frame, 0)) < 0) {
				qDebug() << "alloc frame buffer rgb failed" << av_err2str(code);
				return "alloc buffer";
			}
			memset(frame->data[1], 128, xres * yres / 2 * 2);
		}

		qDebug() << "using" << o.readable;
		selected = true;
		currentOption = o;
		break;
	}

	if (!selected) {
		qDebug() << "none of codec is working";
		return "no valid encoder";
	}

	packet = av_packet_alloc();
	if (!packet) {
		qDebug("Failed to allocate AVPacket\n");
		return "av packet alloc";
	}

	inited = true;
	qDebug() << "dx2av init done";

	return {};
}

#define ASSERT_SUCCESS_RESULT(x) {auto e = x; if (e.has_value()) return e;}

std::optional<QString> FrameToAv::initCodec(const CodecOption& option)
{
	int err;

    codec = avcodec_find_encoder_by_name(option.name.toStdString().c_str());
	if (!codec) {
		qDebug("Cannot open codec\n");
		return "find av codec";
	}

    ctx = avcodec_alloc_context3(codec);
	if (!ctx) {
		qDebug("Could not allocate video codec context\n");
		return "codec alloc ctx";
	}

    ctx->width = xres;
    ctx->height = yres * 2;
    ctx->time_base.den = ctx->framerate.num = frameD;
    ctx->time_base.num = ctx->framerate.den = frameN;
    ctx->pkt_timebase = ctx->time_base;

	initEncodingParameter(option, ctx);
	ASSERT_SUCCESS_RESULT(initOptimalEncoder(option, ctx));

	if ((err = avcodec_open2(ctx, codec, nullptr)) < 0) {
		qDebug("Could not open codec %s\n", av_err2str(err));
		return "open codec";
	}

    frame = av_frame_alloc();
	if (!frame) {
		qDebug("Could not alloc sw frame\n");
		return "frame alloc";
	}
    frame->format = getPixelFormatOf(option.mode);
    frame->width = ctx->width;
    frame->height = ctx->height;

	return {};
}

std::optional<QString> FrameToAv::initOptimalEncoder(const CodecOption& option, AVCodecContext* ctx)
{
	int err;

	if (option.mode & FRAME_TO_AV_TYPE_DXFULL) {
		// Yeahy, we can elinimate all copy!
		if (option.name.endsWith("nvenc") || option.name.endsWith("amf")) {

			if (option.name.endsWith("nvenc") && nv12->getDeviceVendor() != ADAPTER_VENDOR_NVIDIA) {
				return "adapter mismatch";
			}

			if (option.name.endsWith("amf") && nv12->getDeviceVendor() != ADAPTER_VENDOR_AMD) {
				return "adapter mismatch";
			}

			ctx->pix_fmt = AV_PIX_FMT_D3D11;
			ctx->sw_pix_fmt = AV_PIX_FMT_NV12;

			AVBufferRef* hw = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
			auto* device_ctx = reinterpret_cast<AVHWDeviceContext*>(hw->data);
			auto* d3d11va_device_ctx = reinterpret_cast<AVD3D11VADeviceContext*>(device_ctx->hwctx);
			d3d11va_device_ctx->device = nv12->getDevice();
			if (d3d11va_device_ctx->device == nullptr) {
				qDebug("dx2av d3d11 device null");
				av_buffer_unref(&hw);
				return "open d3d11 device";
			}

			if ((err = av_hwdevice_ctx_init(hw)) < 0) {
				qDebug() << "dx2av d3d11 hwdevice failed" << av_err2str(err);
				av_buffer_unref(&hw);
				return "init hwdevice ctx";
			}
			ctx->hw_device_ctx = av_buffer_ref(hw);

			AVBufferRef* hwf = av_hwframe_ctx_alloc(hw);
			auto* frame = reinterpret_cast<AVHWFramesContext*>(hwf->data);
			frame->format = AV_PIX_FMT_D3D11;
			frame->sw_format = AV_PIX_FMT_NV12;
			frame->width = ctx->width;
			frame->height = ctx->height;

			if ((err = av_hwframe_ctx_init(hwf)) < 0) {
				qDebug() << "dx2av child d3d11 hwframe failed" << av_err2str(err);
				av_buffer_unref(&hw);
				av_buffer_unref(&hwf);
				return "init hwframe ctx";
			}
			ctx->hw_frames_ctx = av_buffer_ref(hwf);

			av_buffer_unref(&hw);
			av_buffer_unref(&hwf);
		}
		else if (option.name.endsWith("qsv")) {
			if (nv12->getDeviceVendor() != ADAPTER_VENDOR_INTEL) {
				return "adapter mismatch";
			}

			ctx->pix_fmt = AV_PIX_FMT_QSV;
			ctx->sw_pix_fmt = AV_PIX_FMT_NV12;

			// Init d3d11 child device
			AVBufferRef* ch = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
			auto* ch_device_ctx = reinterpret_cast<AVHWDeviceContext*>(ch->data);
			auto* ch_d3d11va_device_ctx = reinterpret_cast<AVD3D11VADeviceContext*>(ch_device_ctx->hwctx);
			ch_d3d11va_device_ctx->device = nv12->getDevice();
			if (ch_d3d11va_device_ctx->device == nullptr) {
				qDebug("dx2av qsv d3d11 device null");
				av_buffer_unref(&ch);
				return "open child d3d11 device";
			}

			if ((err = av_hwdevice_ctx_init(ch)) < 0) {
				qDebug() << "dx2av qsv hwdevice failed" << av_err2str(err);
				av_buffer_unref(&ch);
				return "init child hwdevice ctx";
			}

			AVBufferRef* hw;
			if ((err = av_hwdevice_ctx_create_derived(&hw, AV_HWDEVICE_TYPE_QSV, av_buffer_ref(ch), 0)) < 0) {
				qDebug() << "dx2av qsv hwdevice failed" << av_err2str(err);
				av_buffer_unref(&ch);
				av_buffer_unref(&hw);
				return "init qsv hwdevice ctx";
			}

			ctx->hw_device_ctx = av_buffer_ref(hw);

			// Create child hwframe
			AVBufferRef* chf = av_hwframe_ctx_alloc(ch);
			auto* frame = reinterpret_cast<AVHWFramesContext*>(chf->data);
			frame->format = AV_PIX_FMT_D3D11;
			frame->sw_format = AV_PIX_FMT_NV12;
			frame->width = ctx->width;
			frame->height = ctx->height;
			frame->initial_pool_size = 2;

			if ((err = av_hwframe_ctx_init(chf)) < 0) {
				qDebug() << "dx2av child qsv hwframe failed" << av_err2str(err);
				av_buffer_unref(&ch);
				av_buffer_unref(&hw);
				av_buffer_unref(&chf);
				return "init child qsv hwframe ctx";
			}

			AVBufferRef* hwf;
			if ((err = av_hwframe_ctx_create_derived(&hwf, AV_PIX_FMT_QSV, hw, chf, 0)) < 0) {
				qDebug() << "dx2av qsv hwframe failed" << av_err2str(err);
				av_buffer_unref(&ch);
				av_buffer_unref(&hw);
				av_buffer_unref(&chf);
				av_buffer_unref(&hwf);
				return "init qsv hwframe ctx";
			}

			ctx->hw_frames_ctx = av_buffer_ref(hwf);

			av_buffer_unref(&ch);
			av_buffer_unref(&hw);
			av_buffer_unref(&chf);
			av_buffer_unref(&hwf);
		}
	}
	else {
		// Although very sad, it is not my fault.
		ctx->pix_fmt = AV_PIX_FMT_NV12;
	}

	return std::optional<QString>();
}

void FrameToAv::initEncodingParameter(const CodecOption& option, AVCodecContext* ctx)
{
    QSettings settings;
    auto cqp = settings.value("avCQP", 32).toInt();
    if (cqp < 16) cqp = 16;
    else if (cqp > 36) cqp = 36;
    settings.setValue("avCQP", cqp);
    settings.sync();

    qDebug() << "use cqp" << cqp;

    ctx->rc_max_rate = 4000000;
    ctx->bit_rate = 2000000;
	ctx->max_b_frames = 0;
	ctx->gop_size = 45;

	auto encoder = option.name;
	if (encoder == "h264_nvenc" || encoder == "hevc_nvenc") {
        av_opt_set(ctx->priv_data, "preset", "p4", 0);
        av_opt_set(ctx->priv_data, "tune", "ull", 0);
		av_opt_set(ctx->priv_data, "profile", "main", 0);
		av_opt_set(ctx->priv_data, "rc", "constqp", 0);
		av_opt_set_int(ctx->priv_data, "qp", cqp, 0);
		av_opt_set_int(ctx->priv_data, "intra_refresh", 1, 0);
	}
	else if (encoder == "h264_amf" || encoder == "hevc_amf") {
		av_opt_set(ctx->priv_data, "usage", "ultralowlatency", 0);
		av_opt_set(ctx->priv_data, "profile", "main", 0);
		av_opt_set(ctx->priv_data, "quality", "speed", 0);
		av_opt_set_int(ctx->priv_data, "header_spacing", ctx->gop_size, 0);
		av_opt_set_int(ctx->priv_data, "frame_skipping", 0, 0);
		av_opt_set_int(ctx->priv_data, "intra_refresh_mb", ctx->gop_size, 0);

		av_opt_set(ctx->priv_data, "rc", "cqp", 0);
		av_opt_set_int(ctx->priv_data, "qp_i", cqp, 0);
		av_opt_set_int(ctx->priv_data, "qp_p", cqp, 0);
		av_opt_set_int(ctx->priv_data, "qp_b", cqp, 0);

		//av_opt_set(ctx->priv_data, "rc", "vbr_peak", 0);
		//ctx->rc_max_rate = ctx->bit_rate * 5;
		//ctx->rc_buffer_size = ctx->bit_rate * 3;

		//av_opt_set(ctx->priv_data, "rc", "cbr", 0);
		//av_opt_set_int(ctx->priv_data, "filler_data", 1, 0);

		//av_opt_set_int(ctx->priv_data, "log_to_dbg", 1, 0);
	}
	else if (encoder == "h264_qsv" || encoder == "hevc_qsv") {
		av_opt_set(ctx->priv_data, "preset", "fast", 0);
		av_opt_set_int(ctx->priv_data, "int_ref_type", 1, 0);
		av_opt_set_int(ctx->priv_data, "int_ref_cycle_size", ctx->gop_size, 0);
		//av_opt_set_int(ctx->priv_data, "idr_interval", 1, 0);

		ctx->flags |= AV_CODEC_FLAG_QSCALE;
		ctx->global_quality = cqp * FF_QP2LAMBDA;
	}
	else if (encoder == "libx264") {
		av_opt_set(ctx->priv_data, "preset", "veryfast", 0);
		av_opt_set(ctx->priv_data, "profile", "main", 0);
		av_opt_set_int(ctx->priv_data, "crf", cqp, 0);
		av_opt_set_int(ctx->priv_data, "qp", cqp, 0);
		av_opt_set_int(ctx->priv_data, "intra_refresh", 1, 0);
	}
}

std::optional<QString> FrameToAv::processFast(const std::shared_ptr<IDxCopyable>& fast) {
    int ret;

    QElapsedTimer t;
    t.start();

    if (currentOption.mode == FRAME_TO_AV_MODE_DXFULL_D3D11) {
        nv12->bgraToNv12Fast(fast);
        nv12->copyFrameD3D11(frame);
    }
    else if (currentOption.mode == FRAME_TO_AV_MODE_DXFULL_QSV) {
        nv12->bgraToNv12Fast(fast);
        nv12->copyFrameQSV(frame);
    }

    auto r = processInternal();

    fps.add(t.nsecsElapsed());

    return r;
}

void FrameToAv::stop()
{
	if (!inited)
		return;
	inited = false;

	auto msg = std::make_shared<VtsMsg>();
	msg->set_type(VTS_MSG_AVSTOP);
	onPacketReceived(std::move(msg));

	pts = 0;
	nv12 = nullptr;
	av_packet_free(&packet);
	av_frame_free(&frame);
	avcodec_free_context(&ctx);
}

const QList<CodecOption>& FrameToAv::getEncoders() {
	return options;
}

std::optional<QString> FrameToAv::processInternal() {
    int ret;

    pts++;
    frame->pts = pts;
    auto msg = std::make_shared<VtsMsg>();
    msg->set_type(VTS_MSG_AVFRAME);
    auto avFrame = msg->mutable_avframe();
    avFrame->set_pts(pts);

    QStringList err;

//    auto* device_ctx = reinterpret_cast<AVHWDeviceContext*>(ctx->hw_device_ctx->data);
//    auto* d3d11va_device_ctx = reinterpret_cast<AVD3D11VADeviceContext*>(device_ctx->hwctx);
//    saveTextureToFile(d3d11va_device_ctx->device_context, (ID3D11Texture2D*) frame->data[0], "./bgra_nv12_output.png");

    //Elapsed e2("encode");
    ret = avcodec_send_frame(ctx, frame);
    if (ret < 0) {
        qDebug() << "error sending frame for rgb encoding" << av_err2str(ret);
        err.append("send frame");
    }

    while (ret >= 0) {
        packet->stream_index = 0;
        ret = avcodec_receive_packet(ctx, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            qDebug() << "error recving packet for rgb encoding";
            err.append("recv packet");
        }

        //qDebug() << "rgb packet" << packet->size;
        auto d = avFrame->add_packets();
        d->mutable_data()->assign((const char*)packet->data, packet->size);
        d->set_dts(packet->dts);
        d->set_pts(packet->pts);

        av_packet_unref(packet);
    }

    if (!err.empty()) {
        auto ee = err.join(", ");
        qDebug() << "one or more error occoured" << ee;
        return ee;
    }

    //e.end();
    onPacketReceived(std::move(msg));

    return {};
}
