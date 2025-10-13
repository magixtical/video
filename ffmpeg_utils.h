#pragma once
#ifndef FFMPEG_UTILS_H
#define FFMPEG_UTILS_H
#include<memory>
extern "C" {
	struct AVCodec;
	struct AVCodecContext;
	struct SwsContext;
	struct SwrContext;
	struct AVFrame;
	struct AVChannelLayout;
	enum AVPixelFormat;
	enum AVSampleFormat;
}

class CodecContext {
private:
	AVCodecContext* ctx = nullptr;
public:
	CodecContext(const AVCodec* codec);
	~CodecContext();
	CodecContext(const CodecContext&) = delete;
	CodecContext& operator=(const CodecContext&) = delete;
	CodecContext(CodecContext&&) noexcept;
	CodecContext& operator=(CodecContext&&) noexcept;

	AVCodecContext* get() {
		return ctx;
	};
	operator AVCodecContext* ()const { return ctx; }
};


class FFmpegSwsContext {
private:
	::SwsContext* ctx=nullptr;
public:
	FFmpegSwsContext(int src_w, int src_h, AVPixelFormat src_fmt,
		int dst_w, int dst_h, AVPixelFormat dst_fmt,
		int flag = 0);
	~FFmpegSwsContext();
	FFmpegSwsContext(const FFmpegSwsContext&) = delete;
	FFmpegSwsContext& operator=(const FFmpegSwsContext&) = delete;
	
	::SwsContext* get() const { return ctx; }
};

class FFmpegSwrContext {
	::SwrContext* ctx = nullptr;
public:
	FFmpegSwrContext(const AVChannelLayout* out_ch_layout, AVSampleFormat out_fmt, int out_sample_rate,
		const AVChannelLayout* in_ch_layout, AVSampleFormat in_fmt, int in_sample_fmt);
	~FFmpegSwrContext();
	FFmpegSwrContext(const FFmpegSwrContext&) = delete;
	FFmpegSwrContext& operator=(const FFmpegSwrContext&) = delete;

	::SwrContext* get() const { return ctx; }

};

class Frame {
private:
	AVFrame* frame = nullptr;
public:
	Frame(int width = 0, int height = 0, AVPixelFormat fmt = (AVPixelFormat)-1);
	~Frame();
	Frame(const Frame&) = delete;
	Frame& operator=(const Frame&) = delete;
	AVFrame* get()const { return frame; }
	operator AVFrame* ()const { return frame; }
	void alloc_buffer(int align = 32);
};

#endif // !FFMPEG_UTILS_H



