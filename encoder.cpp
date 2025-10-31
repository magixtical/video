#include "encoder.h"
#include<iostream>

MultiEncoder::MultiEncoder()=default ;
MultiEncoder::~MultiEncoder(){
    if(codec_ctx_){
        avcodec_free_context(&codec_ctx_);
    }
}

bool MultiEncoder::initialize(const EncoderConfig& config){
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


bool MultiEncoder::encodeFrame(AVFrame* frame) {
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



bool MultiEncoder::flush() {
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

bool MultiEncoder::reinitialize() {
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }
    return initialize(config_);
}

void MultiEncoder::addPacketCallback(PacketCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    packet_callbacks_.push_back(callback);
}

