#pragma once
#ifndef CONFIG_H
#define	CONFIG_H
#include<string>
#include<unordered_set>
extern"C" {
	#include<libavcodec/avcodec.h>
	#include<libavutil/opt.h>
	#include<libavutil/imgutils.h>
	#include<libswscale/swscale.h>
	#include<libswresample/swresample.h>
}

extern const std::unordered_set<int> TRANSCODE_VIDEO_CODECS;
extern const std::unordered_set<int> TRANSCODE_AUDIO_CODECS;


struct Config {
	const std::unordered_set<int>& TRANSCODE_VIDEO_CODECS;
	const std::unordered_set<int>& TRANSCODE_AUDIO_CODECS;

	const std::string VIDEO_PATH = "local_video.mp4";
	const std::string HLS_DIR = "hls_stream";
	const std::string M3U8_FILENAME = "stream.m3u8";
	const uint64_t HTTP_PORT = 8080;
	const int HLS_SEGMENT_DURATION = 10;//切片时长
	const int VIDEO_BITRATE = 1000000;
	const int AUDIO_BITRATE = 128000;
	const int HTTP_THREADS = 4;
	const bool CLEAN_OLD_SEGMENTS = true;

	const bool FORCE_RECONVERT = false;
	const bool CHECK_HLS_INTEGRITY = true;
	const int MAX_RECONVERT_ATTENMPTS = 3;

	const std::unordered_set<std::string> SUPPORTED_FORMAT = {
		".mp4",".avi",".mov",".mkv",".flv",".wmv",".webm"
	};

	Config()
		: TRANSCODE_VIDEO_CODECS(::TRANSCODE_VIDEO_CODECS)
		, TRANSCODE_AUDIO_CODECS(::TRANSCODE_AUDIO_CODECS)
	{
	}
};
struct RTMPConfig{
	bool enabled= true;
	std::string url="rtmp://localhost/live/stream";
	int video_bitrate= 1000000;
	int audio_bitrate= 128000;
	std::string video_preset="veryfast";
	std::string tune="zerolatency";
	int gop_size= 10;
	int max_b_frames= 0;
	int frame_rate=60;
};

struct ScreenRecorderConfig{
	bool record_to_file=true;
	std::string output_directory="recording";
	std::string output_filename="screen_record.mp4";

	bool capture_full_screen=true;
	bool capture_region=false;
	int region_x=0;
	int region_y=0;
	int region_width=0;
	int region_height=0;
	bool maintain_aspect_ratio=true;

	int width=0;
	int height=0;
	std::string video_codec_name="libx264";
	std::string audio_codec_name="aac";
	int sample_rate=44100;
	int channels=2;
	AVSampleFormat sample_format=AV_SAMPLE_FMT_FLTP;
	AVPixelFormat pixel_format=AV_PIX_FMT_YUV420P;
	AVChannelLayout channel_layout=AV_CHANNEL_LAYOUT_STEREO;

};

#endif // 
