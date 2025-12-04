#include "encoder.h"
#include<iostream>

Encoder::Encoder()=default ;
Encoder::~Encoder(){
    if(codec_ctx_){
        avcodec_free_context(&codec_ctx_);
    }
}

bool Encoder::initialize(const EncoderConfig& config){
    config_ = config;
    codec_=avcodec_find_encoder_by_name(config_.video_codec_name.c_str());
    if(!codec_)
    {
        std::cerr<<"Failed to find encoder: "<<config_.video_codec_name<<std::endl;
        return false;
    }
    codec_ctx_=avcodec_alloc_context3(codec_);

    if(!codec_ctx_){
        std::cerr<<"Failed to allocate codec context"<<std::endl;
        return false;
    }

    //parameters for video encoding
    codec_ctx_->width=config_.width;
    codec_ctx_->height=config_.height;
    codec_ctx_->time_base=AVRational{1,config_.frame_rate};
    codec_ctx_->framerate={config_.frame_rate,1};
    codec_ctx_->pix_fmt=config_.pixel_format;
    codec_ctx_->bit_rate=config_.video_bitrate;
    codec_ctx_->gop_size=config_.gop_size;
    codec_ctx_->max_b_frames=config_.max_b_frames;

    if(codec_->id==AV_CODEC_ID_H264){
        av_opt_set(codec_ctx_->priv_data,"preset","ultrafast",0);
        av_opt_set(codec_ctx_->priv_data,"tune","zerolatency",0);
    }

    int ret=avcodec_open2(codec_ctx_,codec_,nullptr);
    if(ret<0){
        char error[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(error,sizeof(error),ret);
        std::cerr<<"Failed to open codec: "<<error<<std::endl;
        return false;
    }
    std::cout<<"Encoder initialized successfully"<<std::endl;
    return true;
}

bool Encoder::initializeAudio(const EncoderConfig& config){
    audio_codec_=avcodec_find_encoder_by_name(config_.audio_codec_name.c_str());
    if(!audio_codec_){
        std::cerr<<"Failed to find audio encoder: "<<config_.audio_codec_name<<std::endl;
        return false;
    }


     
    audio_codec_ctx_=avcodec_alloc_context3(audio_codec_);
    if(!audio_codec_ctx_){
        std::cerr<<"Failed to allocate audio codec context"<<std::endl;
        return false;
    }
    
    audio_codec_ctx_->bit_rate=config_.audio_bitrate;
    audio_codec_ctx_->sample_rate=config_.sample_rate;

    audio_codec_ctx_->ch_layout=config_.channel_layout;
    audio_codec_ctx_->sample_fmt=config_.sample_format;
    audio_codec_ctx_->time_base={1,config_.sample_rate};

    if (audio_codec_->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) {
        audio_codec_ctx_->frame_size = 0; // 允许可变帧大小
    }
    else {
        audio_codec_ctx_->frame_size = 1024; // AAC通常使用1024
    }


    if(audio_codec_->id==AV_CODEC_ID_AAC){
        audio_codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    int ret=avcodec_open2(audio_codec_ctx_,audio_codec_,nullptr);
    if(ret<0){
        char error[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(error,sizeof(error),ret);
        std::cerr<<"Failed to open audio codec: "<<error<<std::endl;
        return false;
    }

    //重采样
    swr_ctx_=swr_alloc();
    if(!swr_ctx_){
        std::cerr<<"Failed to allocate swr context"<<std::endl;
        return false;
    }
    AVChannelLayout in_ch_layout=AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout out_ch_layout=config.channel_layout;

    ret = swr_alloc_set_opts2(&swr_ctx_,
                             &out_ch_layout,           // 输出通道布局
                             config.sample_format,     // 输出采样格式
                             config.sample_rate,       // 输出采样率
                             &in_ch_layout,            // 输入通道布局
                             AV_SAMPLE_FMT_FLT,        // 输入采样格式
                             48000,                    // 输入采样率
                             0, nullptr);
    if (ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(error, sizeof(error), ret);
        std::cerr << "Failed to set resampler options: " << error << std::endl;
        swr_free(&swr_ctx_);
        return false;
    }

    ret = swr_init(swr_ctx_);
    if (ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(error, sizeof(error), ret);
        std::cerr << "Failed to initialize resampler: " << error << std::endl;
        swr_free(&swr_ctx_);
        return false;
    }

    std::cout << "Audio encoder initialized successfully" << std::endl;
    std::cout << "  Sample rate: " << config.sample_rate << "Hz" << std::endl;
    std::cout << "  Channels: " << config.channel_layout.nb_channels << std::endl;
    std::cout << "  Format: " << av_get_sample_fmt_name(config.sample_format) << std::endl;
    
    return true;
}


bool Encoder::encodeFrame(AVFrame* frame) {
    if (!codec_ctx_ || !frame)
        return false;
    frame->pts = frame_count_++;

    std::cout << "Encoding frame #" << frame_count_ << ", pts: " << frame->pts << std::endl;

    int ret = avcodec_send_frame(codec_ctx_, frame);
    if (ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(error, sizeof(error), ret);
        std::cerr << "Failed to send frame to encoder: " << error << std::endl;
        return false;
    }

    AVPacket* packet = av_packet_alloc();
    bool success = false;
    int packet_count = 0;

    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx_, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        else if (ret < 0) {
            char error[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(error, sizeof(error), ret);
            std::cerr << "Error during encoding: " << error << std::endl;
            break;
        }
        // skip empty packets
        if (packet->size <= 0) {
            std::cout << "Skipping empty packet" << std::endl;
            av_packet_unref(packet);
            continue;
        }

        packet_count++;
        std::cout << "Encoded packet #" << packet_count << ", size: " << packet->size
            << ", keyframe: " << (packet->flags & AV_PKT_FLAG_KEY ? "YES" : "NO") << std::endl;

        if (packet->size > 0) {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            for (auto& callback : packet_callbacks_) {
                callback(packet);
            }
            success = true;
        }

        av_packet_unref(packet);
    }

    av_packet_free(&packet);

    if (packet_count > 0) {
        std::cout << "Successfully encoded " << packet_count << " packets for frame #" << frame_count_ << std::endl;
    }
    else {
        std::cout << "No packets encoded for frame #" << frame_count_ << std::endl;
    }

    return success;
}

bool Encoder::encodeAudioFrame(AVFrame* frame) {
    if(!audio_codec_ctx_ || !frame)
        return false;
    frame->pts=audio_samples_encoded_;
    audio_samples_encoded_+=frame->nb_samples;
    audio_frame_count_++;


    std::cout << "Audio encoding frame #" << frame->pts 
              << ", samples: " << frame->nb_samples 
              << " , pts: "<<frame->pts
              << ", format: " << frame->format << std::endl;

    int ret=avcodec_send_frame(audio_codec_ctx_,frame);
    if(ret<0){
        char error[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(error,sizeof(error),ret);
        std::cerr<<"Failed to send frame to audio encoder: "<<error<<std::endl;
        return false;
    }
    
    AVPacket* packet=av_packet_alloc();
    bool success=false;
    int packet_count=0;

    while(ret>=0){
        ret=avcodec_receive_packet(audio_codec_ctx_,packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            char error[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(error,sizeof(error),ret);
            std::cerr << "Error during audio encoding: " << error << std::endl;
            break;
        }
        packet_count++;
        std::cout << "Audio encoded packet #" << packet_count 
                  << ", size: " << packet->size 
                  << ", pts: " << packet->pts << std::endl;
        
        if (packet->size > 0) {
            std::lock_guard<std::mutex> lock(audio_callback_mutex_);
            
            for (auto& callback : audio_packet_callbacks_) {
                callback(packet);
            }
            success = true;
        }else{
            std::cout << "Skipping empty audio packet" << std::endl;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    if (packet_count > 0) {
        std::cout << "Successfully encoded " << packet_count << " audio packets for frame #" << audio_frame_count_ << std::endl;
    } else {
        std::cout << "No audio packets encoded for frame #" << audio_frame_count_ << std::endl;
    }
    return success;
}


bool Encoder::flush() {
    if (!codec_ctx_) return false;
    
    int ret = avcodec_send_frame(codec_ctx_, nullptr);
    if (ret < 0) return false;
    
    AVPacket* packet = av_packet_alloc();
    bool success = false;
    
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx_, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            break;
        }
        
        std::lock_guard<std::mutex> lock(callback_mutex_);
        for (auto& callback : packet_callbacks_) {
            callback(packet);
        }
        success = true;
        av_packet_unref(packet);
    }
    
    av_packet_free(&packet);
    return success;
}

bool Encoder::reinitialize() {
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }
    if(audio_codec_ctx_)
    {
        avcodec_free_context(&audio_codec_ctx_);
    }
    if(swr_ctx_)
    {
        swr_free(&swr_ctx_);
    }
    return initialize(config_)&&initializeAudio(config_);
}

void Encoder::addPacketCallback(PacketCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    packet_callbacks_.push_back(callback);
}

void Encoder::addAudioPacketCallback(PacketCallback callback) {
    std::lock_guard<std::mutex> lock(audio_callback_mutex_);
    audio_packet_callbacks_.push_back(callback);
}

