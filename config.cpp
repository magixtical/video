#include"config.h"
extern "C" {
#include<libavcodec/codec.h>
}
const std::unordered_set<int> TRANSCODE_VIDEO_CODECS = {
		AV_CODEC_ID_HEVC, //h.265
		AV_CODEC_ID_MPEG4,//MPEG_4
		AV_CODEC_ID_VP9,//VP9
		AV_CODEC_ID_AV1,//AV1
		AV_CODEC_ID_WMV3,//WMV
		AV_CODEC_ID_FLV1//FLV
};

const std::unordered_set<int> TRANSCODE_AUDIO_CODECS = {
	AV_CODEC_ID_AC3,       // AC3
	AV_CODEC_ID_DTS,       // DTS
	AV_CODEC_ID_FLAC,      // FLAC
	AV_CODEC_ID_ALAC,      // ALAC
	AV_CODEC_ID_MP3,       // MP3
	AV_CODEC_ID_VORBIS     // Vorbis
};
