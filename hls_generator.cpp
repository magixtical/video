#include "hls_generator.h"
#include"ffmpeg_utils.h"
#include"utils.h"
#include<iostream>
#include<direct.h>
extern"C" {
#include<libavcodec/avcodec.h>
#include<libavformat/avformat.h>
#include<libavutil/avutil.h>
#include<libswscale/swscale.h>
#include<libavutil/opt.h>
#include<libswresample/swresample.h>
#include<libavutil/pixdesc.h>
}

HLSGenerator::~HLSGenerator() {
	if (output_ctx_) {
		if (!(output_ctx_->oformat->flags & AVFMT_NOFILE)) {
			avio_closep(&output_ctx_->pb);
		}
		avformat_free_context(output_ctx_);
	}
	if (input_ctx_) {
		avformat_close_input(&input_ctx_);
	}
}
bool HLSGenerator::should_reconvert() {
    std::string m3u8_path=config_.HLS_DIR+"/"+config_.M3U8_FILENAME;
    if(config_.FORCE_RECONVERT){
        std::cout<<"强制重新转化格式"<<std::endl;
        return true;
    }

    if(!std::filesystem::exists(m3u8_path)){
        std::cout<<"HLS文件不存在，重新转化格式"<<std::endl;
        return true;
    }
    auto input_time=std::filesystem::last_write_time(config_.VIDEO_PATH);
    auto output_time=std::filesystem::last_write_time(m3u8_path);
    if(input_time>output_time){
        std::cout<<"输入文件更新，重新转化格式"<<std::endl;
        return true;
    }
    if(config_.CHECK_HLS_INTEGRITY&&!check_hls_integrity()){
        std::cout<<"HLS文件损坏，重新转化格式"<<std::endl;
        return true;
    }
    std::cout<<"HLS文件未更新，不重新转化格式"<<std::endl;
    return false;
}
bool HLSGenerator::check_hls_integrity() {
    std::string m3u8_path=config_.HLS_DIR+"/"+config_.M3U8_FILENAME;
    try{
        std::ifstream m3u8_file(m3u8_path);
        if(!m3u8_file.is_open()){
            std::cerr<<"无法打开m3u8文件"<<m3u8_path<<std::endl;
            return false;
        }
        std::string line;
        int segment_count=0;
        while(std::getline(m3u8_file,line)){
            if(line.empty()||line[0]=='#')continue;
            std::string ts_path=config_.HLS_DIR+"/"+line;
            if(!std::filesystem::exists(ts_path)){
                std::cerr<<"无法找到TS文件"<<ts_path<<std::endl;
                return false;
            }
            auto file_size=std::filesystem::file_size(ts_path);
            if(file_size<1024){
                std::cerr<<"TS文件大小异常"<<ts_path<<std::endl;
                return false;
            }
            segment_count++;
        }
        if(segment_count==0){
            std::cerr<<"HLS文件中没有有效的TS片段"<<std::endl;
            return false;
        }
        std::cout<<"HLS文件完整，无需重新转化格式"<<std::endl;
        return true;
    }catch(std::exception& e){
        std::cerr<<"检查HLS文件完整性失败:"<<e.what()<<std::endl;
        return false;
    }
}


bool HLSGenerator::needs_transcoding(const AVCodecParameters* codecpar,bool is_video) {
    if(!codecpar)
    return false;
    if(is_video){
        if(codecpar->codec_id==AV_CODEC_ID_H264){
            if(codecpar->format==AV_PIX_FMT_YUV420P){
                std::cout<<"视频流兼容，无需转码"<<std::endl;
                return false;
            }
            std::cout<<"视频流为H264但像素格式不兼容"<<std::endl;
            return true;
        }

        if(config_.TRANSCODE_VIDEO_CODECS.find(codecpar->codec_id)!=config_.TRANSCODE_VIDEO_CODECS.end()){
            const char* codec_name=avcodec_get_name(codecpar->codec_id);
            std::cout<<"视频流为"<<codec_name<<"格式，需要转码"<<std::endl;
            return true;
        }   
        std::cout<<"视频编码格式未知"<<std::endl;
        return true;
    }else{
        if(codecpar->codec_id==AV_CODEC_ID_AAC){
            std::cout<<"音频流为AAC格式，无需转码"<<std::endl;
            return false;
        }
        if(config_.TRANSCODE_AUDIO_CODECS.find(codecpar->codec_id)!=config_.TRANSCODE_AUDIO_CODECS.end()){
            const char* codec_name=avcodec_get_name(codecpar->codec_id);
            std::cout<<"音频流为"<<codec_name<<"格式，需要转码"<<std::endl;
            return true;
        }
        std::cout<<"音频编码格式未知"<<std::endl;
        return true;
    }
}


void HLSGenerator::setup_direct_stream_copy(int stream_idx){
    auto* in_stream = input_ctx_->streams[stream_idx];
    auto* out_stream = avformat_new_stream(output_ctx_, nullptr);

    if(!out_stream){
        throw std::runtime_error("Failed to create output stream for direct copy");
    }

    //复制参数
    int ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
    check_ffmpeg_error(ret, "Failed to copy codec parameters");

    out_stream->time_base=in_stream->time_base;
    std::cout<<"设置直接流复制：流索引"<<stream_idx<<std::endl;
}


void HLSGenerator::init_input() {
	
	int ret = avformat_open_input(&input_ctx_, config_.VIDEO_PATH.c_str(), nullptr, nullptr);
	check_ffmpeg_error(ret, "Failed to open input file:" + config_.VIDEO_PATH);

	ret = avformat_find_stream_info(input_ctx_, nullptr);
	check_ffmpeg_error(ret, "Failed to find stream info");

	for (unsigned int i = 0; i < input_ctx_->nb_streams; i++) {
		auto* stream = input_ctx_->streams[i];
		if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_idx_ == -1) {
			video_stream_idx_ = i;
			std::cout << "Find video stream:" << video_stream_idx_ << std::endl;
		}
		else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_idx_ == -1) {
			audio_stream_idx_ = i;
		std::cout << "Find audio stream:" << audio_stream_idx_ << std::endl;
		}
		av_dump_format(input_ctx_, i, config_.VIDEO_PATH.c_str(), 0);
	}

	if (video_stream_idx_ == -1) {
		throw std::runtime_error("No video stream found in input file");
	}

    //检查是否需要转码
    auto* video_stream=input_ctx_->streams[video_stream_idx_];
    need_video_transcode_=needs_transcoding(video_stream->codecpar,true);
    
    if(need_video_transcode_){

        const AVCodec* video_decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
        check_ffmpeg_error(video_decoder?0:-1,"Failed to find video decoder");
        video_decoder_ctx_ = std::make_unique<CodecContext>(video_decoder);

        ret=avcodec_parameters_to_context(video_decoder_ctx_->get(),video_stream->codecpar);
        check_ffmpeg_error(ret,"Failed to copy video decoder parameters");

        ret=avcodec_open2(video_decoder_ctx_->get(),video_decoder,nullptr);
        check_ffmpeg_error(ret,"Failed to open video decoder");
    }

	if (audio_stream_idx_ != -1) {
        auto* audio_stream=input_ctx_->streams[audio_stream_idx_];
        need_audio_transcode_=needs_transcoding(audio_stream->codecpar,false);

        if(need_audio_transcode_){
            const AVCodec* audio_decoder = avcodec_find_decoder(audio_stream->codecpar->codec_id);
            check_ffmpeg_error(audio_decoder ? 0 : -1, "Failed to find audio decoder");
            audio_decoder_ctx_ = std::make_unique<CodecContext>(audio_decoder);

            ret = avcodec_parameters_to_context(audio_decoder_ctx_->get(), audio_stream->codecpar);
            check_ffmpeg_error(ret, "Failed to copy audio decoder parameters");

            ret = avcodec_open2(audio_decoder_ctx_->get(), audio_decoder, nullptr);
            check_ffmpeg_error(ret, "Failed to open audio decoder");
        }
	}
}

void HLSGenerator::init_output() {

	std::string output_path = config_.HLS_DIR + "/" + config_.M3U8_FILENAME;
	int ret = avformat_alloc_output_context2(&output_ctx_, nullptr, "hls", output_path.c_str());
	check_ffmpeg_error(ret, "Failed to create HLS output context");

	//设置HLS参数
	AVDictionary* options = nullptr;
	av_dict_set(&options, "hls_time", std::to_string(config_.HLS_SEGMENT_DURATION).c_str(), 0);
	av_dict_set(&options, "hls_list_size", "0", 0);
	if (config_.CLEAN_OLD_SEGMENTS) {
		av_dict_set(&options, "hls_flags", "delete_segments", 0);
	}

	//初始化视频流
	auto* in_video_stream = input_ctx_->streams[video_stream_idx_];

    if(!need_video_transcode_){
        setup_direct_stream_copy(video_stream_idx_);
        output_video_stream_idx_=output_ctx_->nb_streams-1;
        std::cout<<"视频流设置为直接复制模式"<<std::endl;
    }else{
        auto* out_video_stream = avformat_new_stream(output_ctx_, nullptr);
        check_ffmpeg_error(out_video_stream ? 0 : -1, "Failed to create output video stream");
        output_video_stream_idx_ = out_video_stream->index;

        const AVCodec* video_codec = avcodec_find_encoder_by_name("libopenh264");
        check_ffmpeg_error(video_codec ? 0 : -1, "Failed to find openh264 encoder");
        video_codec_ctx_ = std::make_unique<CodecContext>(video_codec);

        auto* vctx = video_codec_ctx_->get();
        vctx->bit_rate = config_.VIDEO_BITRATE;
        vctx->width = in_video_stream->codecpar->width;
        vctx->height = in_video_stream->codecpar->height;
        vctx->time_base = in_video_stream->time_base;
        vctx->framerate = av_inv_q(in_video_stream->time_base);
        vctx->gop_size = 30;
        vctx->max_b_frames = 0;
        vctx->pix_fmt = AV_PIX_FMT_YUV420P;
        vctx->profile = AV_PROFILE_H264_MAIN;
        vctx->level = 30;


        av_opt_set(vctx->priv_data, "preset", "ultrafast",0);
        av_opt_set(vctx->priv_data, "tune", "zerolatency", 0);

        AVDictionary* encoder_opts = nullptr;
        av_dict_set(&encoder_opts, "bEnableFrameSkip", "1", 0);
        
        ret = avcodec_open2(vctx, video_codec, &encoder_opts);
        check_ffmpeg_error(ret, "Failed to open video encoder");
        av_dict_free(&encoder_opts);

        ret = avcodec_parameters_from_context(out_video_stream->codecpar, vctx);
        check_ffmpeg_error(ret, "Failed to copy video parameters");
        out_video_stream->time_base = vctx->time_base;

        // 初始化视频格式转换器
        sws_ctx_ = std::make_unique<FFmpegSwsContext>(
            vctx->width, vctx->height, (AVPixelFormat)in_video_stream->codecpar->format,
            vctx->width, vctx->height, vctx->pix_fmt,
            SWS_BILINEAR
        );
    }
	// 初始化音频流（如果存在）
	if (audio_stream_idx_ != -1) {
		auto* in_audio_stream = input_ctx_->streams[audio_stream_idx_];

        if(!need_audio_transcode_){
            setup_direct_stream_copy(audio_stream_idx_);
            output_audio_stream_idx_=output_ctx_->nb_streams-1;
            std::cout<<"音频流设置为直接复制模式"<<std::endl;
        }else{

            auto* out_audio_stream = avformat_new_stream(output_ctx_, nullptr);
            check_ffmpeg_error(out_audio_stream ? 0 : -1, "Failed to create output audio stream");
            output_audio_stream_idx_ = out_audio_stream->index;

            const AVCodec* audio_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
            check_ffmpeg_error(audio_codec ? 0 : -1, "Failed to find AAC encoder");
            audio_codec_ctx_ = std::make_unique<CodecContext>(audio_codec);

            auto* actx = audio_codec_ctx_->get();
            actx->bit_rate = config_.AUDIO_BITRATE;
            actx->sample_rate = in_audio_stream->codecpar->sample_rate;
            actx->ch_layout = in_audio_stream->codecpar->ch_layout;

            actx->sample_fmt = AV_SAMPLE_FMT_FLTP;
            actx->time_base = av_make_q(1, actx->sample_rate);

            ret = avcodec_open2(actx, audio_codec, nullptr);
            check_ffmpeg_error(ret, "Failed to open audio encoder");

            ret = avcodec_parameters_from_context(out_audio_stream->codecpar, actx);
            check_ffmpeg_error(ret, "Failed to copy audio parameters");
            out_audio_stream->time_base = actx->time_base;

            // 初始化音频重采样器
            swr_ctx_ = std::make_unique<FFmpegSwrContext>(
                &actx->ch_layout, actx->sample_fmt, actx->sample_rate,
                &in_audio_stream->codecpar->ch_layout, (AVSampleFormat)in_audio_stream->codecpar->format,
                in_audio_stream->codecpar->sample_rate
            );
        }
    }

	// 打开输出文件
	if (!(output_ctx_->oformat->flags & AVFMT_NOFILE)) {
		ret = avio_open2(&output_ctx_->pb, output_path.c_str(), AVIO_FLAG_WRITE, nullptr, nullptr);
		check_ffmpeg_error(ret, "Failed to open output file: " + output_path);
	}

	// 写入HLS头
	ret = avformat_write_header(output_ctx_, &options);
	check_ffmpeg_error(ret, "Failed to write HLS header");
	av_dict_free(&options);
	av_dump_format(output_ctx_, 0, output_path.c_str(), 1);
}


void HLSGenerator::start(){
    if(!should_reconvert()){
        std::cout<<"跳过HLS转换，直接启动HTTP服务器"<<std::endl;
        return ;
    }
    std::cout<<"开始HLS转换"<<std::endl;
	init_input();
	init_output();

    AVPacket* pkt=av_packet_alloc();
    if (!pkt) {
        throw std::runtime_error("Failed to allocate AVPacket");
    }

    while (av_read_frame(input_ctx_, pkt) >= 0) {
        // 处理当前帧
        process_packet(pkt);
        av_packet_unref(pkt);
    }

    // 处理编码器中剩余的帧（冲刷编码器）
    std::cout << "输入帧读取完成，冲刷编码器剩余数据..." << std::endl;
    AVPacket* flush_pkt = av_packet_alloc();
    process_packet(flush_pkt);  // 发送空包触发冲刷
    av_packet_free(&flush_pkt);

    // 写入HLS尾（点播场景必需，标记播放结束）
    av_write_trailer(output_ctx_);
    std::cout << "HLS生成完成！" << std::endl;

    av_packet_free(&pkt);

}

void HLSGenerator::process_packet(AVPacket* pkt) {
    AVPacket* filtered_pkt = av_packet_clone(pkt);  // 复制数据包
    if (!filtered_pkt) {
        throw std::runtime_error("Failed to clone AVPacket");
    }

    // 1. 区分视频流和音频流
    if (pkt->stream_index == video_stream_idx_) {
        if (!need_video_transcode_) {
            // 直接复制视频包
            filtered_pkt->stream_index = output_video_stream_idx_;
            av_packet_rescale_ts(filtered_pkt,
                input_ctx_->streams[video_stream_idx_]->time_base,
                output_ctx_->streams[output_video_stream_idx_]->time_base);
            
            int ret = av_interleaved_write_frame(output_ctx_, filtered_pkt);
            if (ret < 0) {
                av_packet_free(&filtered_pkt);
                check_ffmpeg_error(ret, "Failed to write video packet to HLS");
            }
        }else{
            // ------------------- 视频流处理：解码→转换→编码→写入 -------------------
            AVFrame* dec_frame = av_frame_alloc();
            check_ffmpeg_error(dec_frame ? 0 : -1, "Failed to allocate decode frame");

            // 1.1 解码HEVC输入帧
            int ret = avcodec_send_packet(video_decoder_ctx_->get(), pkt);
            if (ret < 0) {
                // 如果是EAGAIN，先尝试接收已解码的帧，再重试发送
                if (ret == AVERROR(EAGAIN)) {
                    // 先接收解码器中已有的帧
                    AVFrame* temp_frame = av_frame_alloc();
                    while (avcodec_receive_frame(video_decoder_ctx_->get(), temp_frame) >= 0) {
                        av_frame_free(&temp_frame);
                        temp_frame = av_frame_alloc();
                    }
                    av_frame_free(&temp_frame);
                    // 重试发送数据包
                    ret = avcodec_send_packet(video_decoder_ctx_->get(), pkt);
                }
                if (ret < 0) {
                    av_frame_free(&dec_frame);
                    check_ffmpeg_error(ret, "Failed to send packet to video decoder");
                }
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(video_decoder_ctx_->get(), dec_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                else if (ret < 0) {
                    av_frame_free(&dec_frame);
                    check_ffmpeg_error(ret, "Failed to receive frame from video decoder");
                }

                // 1.2 格式转换：输入帧（HEVC解码后的YUV）→ 编码器要求的YUV420P
                AVFrame* enc_frame = av_frame_alloc();
                check_ffmpeg_error(enc_frame ? 0 : -1, "Failed to allocate encode frame");
                enc_frame->width = video_codec_ctx_->get()->width;
                enc_frame->height = video_codec_ctx_->get()->height;
                enc_frame->format = video_codec_ctx_->get()->pix_fmt;
                enc_frame->pts = dec_frame->pts;  // 传递时间戳

                // 分配帧数据缓冲区
                ret = av_frame_get_buffer(enc_frame, 0);
                check_ffmpeg_error(ret, "Failed to get buffer for encode frame");

                // 调用sws_scale转换格式（FFmpegSwsContext的get()返回原生SwsContext*）
                sws_scale(sws_ctx_->get(),
                    dec_frame->data, dec_frame->linesize, 0, dec_frame->height,
                    enc_frame->data, enc_frame->linesize);

                // 1.3 编码为H.264
                ret = avcodec_send_frame(video_codec_ctx_->get(), enc_frame);
                if (ret < 0) {
                    av_frame_free(&enc_frame);
                    av_frame_free(&dec_frame);
                    check_ffmpeg_error(ret, "Failed to send frame to video encoder");
                }

                AVPacket* enc_pkt=av_packet_alloc();
                if (!enc_pkt) {
                    av_packet_free(&enc_pkt);
                    throw std::runtime_error("Failed to allocate encode AVPacket");
                }

                while (ret >= 0) {
                    ret = avcodec_receive_packet(video_codec_ctx_->get(), enc_pkt);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    }
                    else if (ret < 0) {
                        av_packet_unref(enc_pkt);
                        av_frame_free(&enc_frame);
                        av_frame_free(&dec_frame);
                        check_ffmpeg_error(ret, "Failed to receive packet from video encoder");
                    }
                    if (enc_pkt->stream_index == output_video_stream_idx_) {
                        // 从输出视频流获取时间基，根据帧率计算单帧时长
                        AVStream* out_stream = output_ctx_->streams[output_video_stream_idx_];
                        // 假设输入帧率为30fps，若需动态适配可从输入流获取（in_video_stream->r_frame_rate）
                        AVRational frame_rate = av_make_q(30, 1);
                        enc_pkt->duration = av_rescale_q(1, av_inv_q(frame_rate), out_stream->time_base);
                    }

                    // 1.4 设置输出流索引，转换时间戳
                    enc_pkt->stream_index = output_video_stream_idx_;
                    av_packet_rescale_ts(enc_pkt,
                        video_codec_ctx_->get()->time_base,
                        output_ctx_->streams[output_video_stream_idx_]->time_base);
                    enc_pkt->pos = -1;  // HLS不需要定位信息

                    // 1.5 写入HLS切片
                    ret = av_interleaved_write_frame(output_ctx_, enc_pkt);
                    if (ret < 0) {
                        av_packet_unref(enc_pkt);
                        av_frame_free(&enc_frame);
                        av_frame_free(&dec_frame);
                        check_ffmpeg_error(ret, "Failed to write video packet to HLS");
                    }
                    av_packet_unref(enc_pkt);
                }

                av_frame_free(&enc_frame);
            }

            av_frame_free(&dec_frame);
            // ---------------------------------------------------------------------
        }
    }
    else if (pkt->stream_index == audio_stream_idx_) {
        if(!need_audio_transcode_){
            // 直接复制音频包
            filtered_pkt->stream_index = output_audio_stream_idx_;
            av_packet_rescale_ts(filtered_pkt,
                input_ctx_->streams[audio_stream_idx_]->time_base,
                output_ctx_->streams[output_audio_stream_idx_]->time_base);

            int ret=av_interleaved_write_frame(output_ctx_, filtered_pkt);
            if (ret < 0) {
                av_packet_free(&filtered_pkt);
                check_ffmpeg_error(ret, "Failed to write audio packet to HLS");
            }
        }else{
            // ------------------- 音频流处理：解码→重采样→编码→写入 -------------------
            AVFrame* dec_frame = av_frame_alloc();
            check_ffmpeg_error(dec_frame ? 0 : -1, "Failed to allocate audio decode frame");

            int ret = avcodec_send_packet(audio_decoder_ctx_->get(), pkt);
            if (ret < 0) {
                av_frame_free(&dec_frame);
                check_ffmpeg_error(ret, "Failed to send packet to audio decoder");
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(audio_decoder_ctx_->get(), dec_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                else if (ret < 0) {
                    av_frame_free(&dec_frame);
                    check_ffmpeg_error(ret, "Failed to receive frame from audio decoder");
                }

                // 重采样：输入音频格式→编码器要求的FLTP
                AVFrame* enc_frame = av_frame_alloc();
                check_ffmpeg_error(enc_frame ? 0 : -1, "Failed to allocate audio encode frame");
                enc_frame->format = audio_codec_ctx_->get()->sample_fmt;
                enc_frame->ch_layout = audio_codec_ctx_->get()->ch_layout;
                enc_frame->sample_rate = audio_codec_ctx_->get()->sample_rate;
                enc_frame->pts = dec_frame->pts;

                ret = av_frame_get_buffer(enc_frame, 0);
                check_ffmpeg_error(ret, "Failed to get buffer for audio encode frame");

                // 调用swr_convert重采样（FFmpegSwrContext的get()返回原生SwrContext*）
                int samples_written = swr_convert(swr_ctx_->get(),
                    enc_frame->data, enc_frame->nb_samples,
                    (const uint8_t**)dec_frame->data, dec_frame->nb_samples);
                if (samples_written < 0) {
                    av_frame_free(&enc_frame);
                    av_frame_free(&dec_frame);
                    check_ffmpeg_error(samples_written, "Failed to resample audio");
                }
                enc_frame->nb_samples = samples_written;

                // 编码为AAC
                ret = avcodec_send_frame(audio_codec_ctx_->get(), enc_frame);
                if (ret < 0) {
                    av_frame_free(&enc_frame);
                    av_frame_free(&dec_frame);
                    check_ffmpeg_error(ret, "Failed to send frame to audio encoder");
                }

                AVPacket* enc_pkt=av_packet_alloc();
                if (!enc_pkt) {
                    av_packet_free(&enc_pkt);
                    throw std::runtime_error("Failed to allocate encoder");
                }

                while (ret >= 0) {
                    ret = avcodec_receive_packet(audio_codec_ctx_->get(), enc_pkt);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    }
                    else if (ret < 0) {
                        av_packet_unref(enc_pkt);
                        av_frame_free(&enc_frame);
                        av_frame_free(&dec_frame);
                        check_ffmpeg_error(ret, "Failed to receive packet from audio encoder");
                    }

                    // 设置输出流索引，转换时间戳
                    enc_pkt->stream_index = output_audio_stream_idx_;
                    av_packet_rescale_ts(enc_pkt,
                        audio_codec_ctx_->get()->time_base,
                        output_ctx_->streams[output_audio_stream_idx_]->time_base);
                    enc_pkt->pos = -1;

                    // 写入HLS
                    ret = av_interleaved_write_frame(output_ctx_, enc_pkt);
                    if (ret < 0) {
                        av_packet_unref(enc_pkt);
                        av_frame_free(&enc_frame);
                        av_frame_free(&dec_frame);
                        check_ffmpeg_error(ret, "Failed to write audio packet to HLS");
                    }
                    av_packet_unref(enc_pkt);
                }

                av_frame_free(&enc_frame);
            }

            av_frame_free(&dec_frame);
            // ---------------------------------------------------------------------
        }
    }
    av_packet_free(&filtered_pkt);
}