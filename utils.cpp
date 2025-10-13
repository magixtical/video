#pragma once
#include "utils.h"
#include<iostream>
extern "C" {
	#include<libavutil/error.h>
}
int check_ffmpeg_error(int ret, const std::string& context){
	if (ret < 0) {
		char errbuf[1024];
		av_strerror(ret, errbuf, sizeof(errbuf));
		std::cerr << "[" << context << "] FFmpeg:" <<errbuf<<"(" <<ret<<")"<<std::endl;
		std::exit(EXIT_FAILURE);
	}
	return ret;
}