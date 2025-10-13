#pragma once
#ifndef HLS_GENERATOR_H
#define HLS_GENERATOR_H
#include"config.h"
#include"ffmpeg_utils.h"
#include<memory>
#include<string>
#include<fstream>
#include<sstream>
#include<filesystem>

extern "C" {
	struct AVFormatContext;
	struct AVPacket;
	struct AVCodecParameters;
}

class HLSGenerator {
private:
	const Config& config_;
	AVFormatContext* input_ctx_=nullptr;
	AVFormatContext* output_ctx_=nullptr;
	std::unique_ptr<CodecContext> video_codec_ctx_;
	std::unique_ptr<CodecContext> audio_codec_ctx_;
	std::unique_ptr<FFmpegSwsContext> sws_ctx_;
	std::unique_ptr<FFmpegSwrContext> swr_ctx_;
	std::unique_ptr<CodecContext>video_decoder_ctx_;
	std::unique_ptr<CodecContext>audio_decoder_ctx_;

	int video_stream_idx_ = -1;
	int audio_stream_idx_ = -1;
	int output_video_stream_idx_ = -1;
	int output_audio_stream_idx_ = -1;

	bool need_video_transcode_=true;
	bool need_audio_transcode_=true;
	int input_video_codec_id=0;
	int input_audio_codec_id=0;

	void init_input();
	void init_output();
	bool should_reconvert();
	bool check_hls_integrity();
	bool needs_transcoding(const AVCodecParameters* codecpar,bool is_video);
	void setup_direct_stream_copy(int stream_index);
	void cleanup_output_context();


public:
	HLSGenerator(const Config& config):config_(config){}
	~HLSGenerator();

	void start();
	void process_packet(AVPacket* pkt);
};

#endif // !HLS_GENERATOR_H



