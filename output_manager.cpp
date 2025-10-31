#include "output_manager.h"
#include<iostream>

OutputManager::OutputManager(){
    avformat_network_init();
    pts_counter_ = 0;
}

OutputManager::~OutputManager(){
    stop();
    if(file_fmt_ctx_&&recording_){
        av_write_trailer(file_fmt_ctx_);
        if(file_fmt_ctx_->pb){
            avio_closep(&file_fmt_ctx_->pb);
        }
        avformat_free_context(file_fmt_ctx_);
    }

    if(stream_fmt_ctx_&&streaming_){
        av_write_trailer(stream_fmt_ctx_);
        if(stream_fmt_ctx_->pb){
            avio_closep(&stream_fmt_ctx_->pb);
        }
        avformat_free_context(stream_fmt_ctx_);
    }
}

bool OutputManager::initializeFileOutput(const std::string& filename,const EncoderConfig& config){
    if (recording_) {
        stop();
    }

    filename_ = filename;
    config_ = config;

    // 确保之前的资源被清理
    if (file_fmt_ctx_) {
        if (file_fmt_ctx_->pb) {
            avio_closep(&file_fmt_ctx_->pb);
        }
        avformat_free_context(file_fmt_ctx_);
        file_fmt_ctx_ = nullptr;
        file_stream_ = nullptr;
    }

    if (encoder_) {
        return setupFileOutput();
    }
    else {
        return true;
    }
}

bool OutputManager::initializeStreamOutput(const std::string& rtmp_url,const EncoderConfig& config){
    if (streaming_ && stream_fmt_ctx_) {
        av_write_trailer(stream_fmt_ctx_);
        if (stream_fmt_ctx_->pb) {
            avio_closep(&stream_fmt_ctx_->pb);
        }
        avformat_free_context(stream_fmt_ctx_);
        stream_fmt_ctx_ = nullptr;
        stream_stream_ = nullptr;
        streaming_ = false;
    }

    rtmp_url_ = rtmp_url;
    config_ = config;

    std::cout << "Initializing stream output to: " << rtmp_url_ << std::endl;

    // 如果编码器已经设置，立即设置流输出
    if (encoder_) {
        return setupStreamOutput();
    }

    return true;
}

void OutputManager::setEncoder(std::shared_ptr<MultiEncoder> encoder){
    
    if (encoder_ == encoder) {
        std::cout << "Encoder already set, skipping..." << std::endl;
        return;
    }

    encoder_ = encoder;
    if (encoder_) {
        encoder_->addPacketCallback([this](AVPacket* packet) {
            onEncodedPacket(packet);
            });

        if (!filename_.empty() && !file_fmt_ctx_) {
            setupFileOutput();
        }

        if (!rtmp_url_.empty() && !stream_fmt_ctx_) {
            setupStreamOutput();
        }
    }
}

bool OutputManager::start() {
    if (!encoder_) {
        std::cerr << "Encoder not set" << std::endl;
        return false;
    }
    encoder_->reset();
    bool success = true;

    std::cout << "OutputManager::start() - file_fmt_ctx_: " << (file_fmt_ctx_ ? "valid" : "null")
        << ", stream_fmt_ctx_: " << (stream_fmt_ctx_ ? "valid" : "null") << std::endl;

    // 文件输出
    if (file_fmt_ctx_ && !recording_) {
        std::cout << "Writing header for file output..." << std::endl;

        auto* codec_ctx = encoder_->getCodecContext();
        if (codec_ctx && file_stream_) {
            int ret = avcodec_parameters_from_context(file_stream_->codecpar, codec_ctx);
            if (ret < 0) {
                std::cerr << "Failed to copy codec parameters to file stream in start()" << std::endl;
            }
            else {
                std::cout << "Successfully copied codec parameters to file stream" << std::endl;
            }
            file_stream_->time_base = codec_ctx->time_base;
        }

        int ret = avformat_write_header(file_fmt_ctx_, nullptr);
        if (ret < 0) {
            char error[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(error, sizeof(error), ret);
            std::cerr << "Failed to write header for file output: " << error << std::endl;
            success = false;
        }
        else {
            recording_ = true;
            std::cout << "✓ Start recording to file: " << filename_ << std::endl;
            std::cout << "File stream time_base after header: " << file_stream_->time_base.num
                << "/" << file_stream_->time_base.den << std::endl;
        }
    }

    // 流输出
    if (stream_fmt_ctx_ && !streaming_) {
        std::cout << "Writing header for stream output..." << std::endl;

        auto* codec_ctx = encoder_->getCodecContext();
        if (codec_ctx && stream_stream_) {
            int ret = avcodec_parameters_from_context(stream_stream_->codecpar, codec_ctx);
            if (ret < 0) {
                std::cerr << "Failed to copy codec parameters to stream stream in start()" << std::endl;
            }
            else {
                std::cout << "Successfully copied codec parameters to stream stream" << std::endl;
            }

            // 记录写入头之前的时间基
            std::cout << "Stream time_base before header: " << stream_stream_->time_base.num
                << "/" << stream_stream_->time_base.den << std::endl;
        }

        int ret = avformat_write_header(stream_fmt_ctx_, nullptr);
        if (ret < 0) {
            char error[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(error, sizeof(error), ret);
            std::cerr << "Failed to write header for stream output: " << error << std::endl;
            success = false;
        }
        else {
            streaming_ = true;
            std::cout << "✓ Start streaming to: " << rtmp_url_ << std::endl;
            // 记录写入头之后的时间基（可能被 FFmpeg 修改）
            std::cout << "Stream time_base after header: " << stream_stream_->time_base.num
                << "/" << stream_stream_->time_base.den << std::endl;
        }
    }

    return success;
}

void OutputManager::stop(){
    if (recording_ && file_fmt_ctx_) {
        std::cout << "Writing trailer for file output..." << std::endl;
        av_write_trailer(file_fmt_ctx_);
        if (file_fmt_ctx_->pb) {
            avio_closep(&file_fmt_ctx_->pb);
        }
        avformat_free_context(file_fmt_ctx_);
        file_fmt_ctx_ = nullptr;
        file_stream_ = nullptr;
        recording_ = false;
        std::cout << "Stop recording" << std::endl;
    }

    if (streaming_ && stream_fmt_ctx_) {
        std::cout << "Writing trailer for stream output..." << std::endl;
        av_write_trailer(stream_fmt_ctx_);
        if (stream_fmt_ctx_->pb) {
            avio_closep(&stream_fmt_ctx_->pb);
        }
        avformat_free_context(stream_fmt_ctx_);
        stream_fmt_ctx_ = nullptr;
        stream_stream_ = nullptr;
        streaming_ = false;
        std::cout << "Stop streaming" << std::endl;
    }
    pts_counter_ = 0;
}

bool OutputManager::setupFileOutput(){

    std::cout << "Setting up file output: " << filename_ << std::endl;

    if (file_fmt_ctx_) {
        std::cout << "Warning: file_fmt_ctx_ already exists, cleaning up" << std::endl;
        avformat_free_context(file_fmt_ctx_);
        file_fmt_ctx_ = nullptr;
    }

    int ret=avformat_alloc_output_context2(&file_fmt_ctx_,nullptr,"mp4",filename_.c_str());
    if(ret<0||!file_fmt_ctx_){
        std::cerr<<"Failed to allocate output context for file output"<<std::endl;
        return false;
    }
    file_stream_ = avformat_new_stream(file_fmt_ctx_,nullptr);
    if(!file_stream_){
        std::cerr<<"Failed to create file stream"<<std::endl;
        return false;
    }
    file_stream_->id = file_fmt_ctx_->nb_streams-1;
    file_stream_->time_base={1,config_.frame_rate};

    if (encoder_) {
        auto* codec_ctx = encoder_->getCodecContext();
        if (codec_ctx) {
            ret = avcodec_parameters_from_context(file_stream_->codecpar, codec_ctx);
            if (ret < 0) {
                std::cerr << "Failed to copy codec parameters to file stream" << std::endl;
                return false;
            }
        }
    }

    if(!(file_fmt_ctx_->oformat->flags&AVFMT_NOFILE)){
        ret=avio_open(&file_fmt_ctx_->pb,filename_.c_str(),AVIO_FLAG_WRITE);
        if(ret<0){
            std::cerr<<"Failed to open file for writing"<<std::endl;
            return false;
        }
    }
    std::cout<<"File output initialized:"<<filename_<<std::endl;
    return true;
}

bool OutputManager::setupStreamOutput() {
    std::cout << "Setting up stream output: " << rtmp_url_ << std::endl;

    // 分配输出上下文
    int ret = avformat_alloc_output_context2(&stream_fmt_ctx_, nullptr, "flv", rtmp_url_.c_str());
    if (ret < 0 || !stream_fmt_ctx_) {
        char error[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(error, sizeof(error), ret);
        std::cerr << "Failed to create stream output context: " << error << std::endl;
        return false;
    }

    // 创建流
    stream_stream_ = avformat_new_stream(stream_fmt_ctx_, nullptr);
    if (!stream_stream_) {
        std::cerr << "Failed to create stream stream" << std::endl;
        return false;
    }

    stream_stream_->id = stream_fmt_ctx_->nb_streams - 1;

    // 设置编码器参数
    if (encoder_) {
        auto* codec_ctx = encoder_->getCodecContext();
        if (codec_ctx) {
            // 复制编码器参数
            ret = avcodec_parameters_from_context(stream_stream_->codecpar, codec_ctx);
            if (ret < 0) {
                std::cerr << "Failed to copy codec parameters to stream" << std::endl;
                return false;
            }

            // 对于 RTMP 流，使用合适的时间基
            stream_stream_->time_base = { 1, 1000 }; // 使用毫秒精度
            // 或者使用编码器的时间基：stream_stream_->time_base = codec_ctx->time_base;

            std::cout << "Set stream time_base: " << stream_stream_->time_base.num
                << "/" << stream_stream_->time_base.den << std::endl;
        }
    }

    // 显示流信息
    std::cout << "Stream parameters:" << std::endl;
    std::cout << "  Width: " << stream_stream_->codecpar->width << std::endl;
    std::cout << "  Height: " << stream_stream_->codecpar->height << std::endl;
    std::cout << "  Format: " << stream_stream_->codecpar->format << std::endl;
    std::cout << "  Time base: " << stream_stream_->time_base.num << "/" << stream_stream_->time_base.den << std::endl;

    // 网络选项 - 简化设置
    AVDictionary* options = nullptr;
    av_dict_set(&options, "rw_timeout", "10000000", 0);
    av_dict_set(&options, "stimeout", "10000000", 0);
    av_dict_set(&options, "buffer_size", "65536", 0);

    // 打开输出
    std::cout << "Opening stream output to: " << rtmp_url_ << std::endl;

    if (!(stream_fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open2(&stream_fmt_ctx_->pb, rtmp_url_.c_str(), AVIO_FLAG_WRITE, nullptr, &options);
        av_dict_free(&options);

        if (ret < 0) {
            char error[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(error, sizeof(error), ret);
            std::cerr << "Failed to open stream for writing: " << error << std::endl;

            // 清理资源
            avformat_free_context(stream_fmt_ctx_);
            stream_fmt_ctx_ = nullptr;
            stream_stream_ = nullptr;
            return false;
        }
    }

    std::cout << "✓ Stream output initialized successfully: " << rtmp_url_ << std::endl;
    return true;
}


void OutputManager::onEncodedPacket(AVPacket* packet) {
    if (!packet || packet->size <= 0) {
        return;
    }

    // 为文件输出写入包
    if (recording_ && file_fmt_ctx_ && file_stream_) {
        AVPacket* file_packet = av_packet_clone(packet);
        if (file_packet) {
            if (writePacket(file_packet, file_fmt_ctx_, file_stream_)) {
                std::cout << "✓ Successfully wrote packet to file" << std::endl;
            }
            av_packet_free(&file_packet);
        }
    }

    // 为流输出写入包
    if (streaming_ && stream_fmt_ctx_ && stream_stream_) {
        AVPacket* stream_packet = av_packet_clone(packet);
        if (stream_packet) {
            if (writePacket(stream_packet, stream_fmt_ctx_, stream_stream_)) {
                std::cout << "✓ Successfully wrote packet to stream" << std::endl;
            }
            else {
                // 流写入失败，禁用流输出但不清除上下文
                std::cerr << "Stream write failed, disabling streaming" << std::endl;
                streaming_ = false;
            }
            av_packet_free(&stream_packet);
        }
    }
}



bool OutputManager::writePacket(AVPacket* packet, AVFormatContext* fmt_ctx, AVStream* stream) {
    if (!fmt_ctx || !stream || !packet || packet->size <= 0) {
        return false;
    }

    AVPacket* pkt = av_packet_clone(packet);
    if (!pkt) {
        return false;
    }

    pkt->stream_index = stream->index;

    // 时间戳处理
    auto* codec_ctx = encoder_->getCodecContext();
    if (codec_ctx && pkt->pts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE) {
        AVRational src_time_base = codec_ctx->time_base;
        AVRational dst_time_base = stream->time_base;

        pkt->pts = av_rescale_q(pkt->pts, src_time_base, dst_time_base);
        pkt->dts = av_rescale_q(pkt->dts, src_time_base, dst_time_base);
    }

    // 写入包
    int ret = av_interleaved_write_frame(fmt_ctx, pkt);
    av_packet_free(&pkt);

    if (ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(error, sizeof(error), ret);
        std::cerr << "Failed to write packet: " << error << std::endl;
        return false;
    }

    pts_counter_++;
    return true;
}

void OutputManager::reset() {
    stop();

    filename_.clear();
    rtmp_url_.clear();
    recording_ = false;
    streaming_ = false;
    pts_counter_ = 0;


    file_fmt_ctx_ = nullptr;
    file_stream_ = nullptr;
    stream_fmt_ctx_ = nullptr;
    stream_stream_ = nullptr;

    std::cout << "OutputManager reset completed" << std::endl;
}

bool OutputManager::testRTMPConnection(const std::string& url) {
    std::cout << "Testing RTMP connection to: " << url << std::endl;

    // 使用 FFmpeg 风格的测试方法
    AVFormatContext* fmt_ctx = nullptr;
    AVDictionary* options = nullptr;

    // 设置超时选项
    av_dict_set(&options, "rw_timeout", "5000000", 0);  // 5秒
    av_dict_set(&options, "stimeout", "5000000", 0);    // 5秒
    av_dict_set(&options, "analyzeduration", "1000000", 0);

    int ret = avformat_open_input(&fmt_ctx, url.c_str(), nullptr, &options);
    av_dict_free(&options);

    if (ret >= 0) {
        std::cout << "✓ RTMP connection test successful" << std::endl;
        avformat_close_input(&fmt_ctx);
        return true;
    }
    else {
        char error[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(error, sizeof(error), ret);
        std::cerr << "✗ RTMP connection test failed: " << error << std::endl;
        return false;
    }
}