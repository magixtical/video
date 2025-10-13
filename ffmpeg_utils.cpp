// ffmpeg_utils.cpp
#include "ffmpeg_utils.h"
#include "utils.h"
#include <stdexcept>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/frame.h>
}

// CodecContext实现
CodecContext::CodecContext(const AVCodec* codec) {
    ctx = avcodec_alloc_context3(codec);
    if (!ctx) throw std::runtime_error("Failed to allocate codec context");
}

CodecContext::~CodecContext() {
    if (ctx) avcodec_free_context(&ctx);
}

CodecContext::CodecContext(CodecContext&& other) noexcept : ctx(other.ctx) {
    other.ctx = nullptr;
}

CodecContext& CodecContext::operator=(CodecContext&& other) noexcept {
    if (this != &other) {
        if (ctx) avcodec_free_context(&ctx);
        ctx = other.ctx;
        other.ctx = nullptr;
    }
    return *this;
}

// SwsContext实现
FFmpegSwsContext::FFmpegSwsContext(int src_w, int src_h, AVPixelFormat src_fmt,
    int dst_w, int dst_h, AVPixelFormat dst_fmt, int flags) {
    ctx = sws_getContext(src_w, src_h, src_fmt,
        dst_w, dst_h, dst_fmt,
        flags, nullptr, nullptr, nullptr);
    if (!ctx) throw std::runtime_error("Failed to allocate SwsContext");
}

FFmpegSwsContext::~FFmpegSwsContext() {
    if (ctx) sws_freeContext(ctx);
}

// SwrContext实现
FFmpegSwrContext::FFmpegSwrContext(const AVChannelLayout* out_ch_layout, AVSampleFormat out_fmt, int out_sample_rate,
    const AVChannelLayout* in_ch_layout, AVSampleFormat in_fmt, int in_sample_rate) {
    ctx = swr_alloc();
    if (!ctx) throw std::runtime_error("Failed to allocate SwrContext");

    int ret = swr_alloc_set_opts2(&ctx,
        out_ch_layout, out_fmt, out_sample_rate,
        in_ch_layout, in_fmt, in_sample_rate,
        0, nullptr);
    check_ffmpeg_error(ret, "Failed to set SwrContext options");

    ret = swr_init(ctx);
    if (ret < 0) {
        swr_free(&ctx);
        check_ffmpeg_error(ret, "Failed to initialize SwrContext");
    }
}

FFmpegSwrContext::~FFmpegSwrContext() {
    if (ctx) swr_free(&ctx);
}

// Frame实现
Frame::Frame(int width, int height, AVPixelFormat fmt) {
    frame = av_frame_alloc();
    if (!frame) throw std::runtime_error("Failed to allocate AVFrame");
    // 初始化帧属性（如果传入参数）
    if (width > 0 && height > 0 && fmt != (AVPixelFormat)-1) {
        frame->width = width;
        frame->height = height;
        frame->format = fmt;
    }
}

Frame::~Frame() {
    if (frame) av_frame_free(&frame);
}

void Frame::alloc_buffer(int align) {
    check_ffmpeg_error(av_frame_get_buffer(frame, align), "Failed to alloc frame buffer");
}