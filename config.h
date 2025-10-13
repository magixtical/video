#pragma once
#ifndef CONFIG_H
#define	CONFIG_H
#include<string>
#include<unordered_set>

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

#endif // 
