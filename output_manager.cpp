#include "output_manager.h"
#include<iostream>

OutputManager::OutputManager(){
    avformat_network_init();
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
        file_video_stream_ = nullptr;
        file_audio_stream_=nullptr;
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
        stream_video_stream_ = nullptr;
        stream_audio_stream_ = nullptr;
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

void OutputManager::setEncoder(std::shared_ptr<Encoder> encoder){
    
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
/*
void OutputManager::setAudioEncoder(std::shared_ptr<Encoder> audio_encoder){
    std::cout << "=== OutputManager::setAudioEncoder ===" << std::endl;
    std::cout << "Current audio_encoder_: " << audio_encoder_.get() << std::endl;
    std::cout << "New audio_encoder: " << audio_encoder.get() << std::endl;

    if(audio_encoder_==audio_encoder){
        std::cout<<"Audio encoder already set, skipping..."<<std::endl;
        return;
    }
    audio_encoder_=audio_encoder;
    if(audio_encoder_){
        std::cout<<"Setting audio encoder callback..."<<std::endl;
        audio_encoder_->addAudioPacketCallback([this](AVPacket* packet){
            std::cout<<"====AUDIO CALLBACK TRIGGERED==="<<std::endl;
            onAudioEncodedPacket(packet);
        });
        std::cout<<"Audio encoder callback set"<<std::endl;

        if(!filename_.empty()&&!file_fmt_ctx_){
            setupFileOutput();
        }
        if(!rtmp_url_.empty()&&!stream_fmt_ctx_){
            setupStreamOutput();
        }
    }else{
        std::cout<<"Audio encoder is null,clearing callback"<<std::endl;
    }
    std::cout<<"===setAudioEncoder END==="<<std::endl;
}


*/
bool OutputManager::start() {
    if (!encoder_) {
        std::cerr << "Encoder not set" << std::endl;
        return false;
    }
    if(encoder_)
        encoder_->reset();
    /*
    if(audio_encoder_)
    {
        audio_encoder_->resetAudio();
    }
    */
    
    bool success = true;

    std::cout << "OutputManager::start() - file_fmt_ctx_: " << (file_fmt_ctx_ ? "valid" : "null")
        << ", stream_fmt_ctx_: " << (stream_fmt_ctx_ ? "valid" : "null") << std::endl;

    // 文件输出
    if (file_fmt_ctx_ && !recording_) {
        std::cout << "Writing header for file output..." << std::endl;

        if (encoder_ && file_video_stream_) {
            auto* codec_ctx = encoder_->getCodecContext();
            if (codec_ctx) {
                int ret = avcodec_parameters_from_context(file_video_stream_->codecpar, codec_ctx);
                if (ret < 0) {
                    std::cerr << "Failed to copy video codec parameters to file stream in start()" << std::endl;
                }
                else {
                    std::cout << "Successfully copied video codec parameters to file stream" << std::endl;
                }
                file_video_stream_->time_base = codec_ctx->time_base;
            }
        }
        /*
        if(audio_encoder_&&file_audio_stream_){
            auto* audio_codec_ctx = audio_encoder_->getAudioCodecContext();
            if (audio_codec_ctx) {
                int ret = avcodec_parameters_from_context(file_audio_stream_->codecpar, audio_codec_ctx);
                if (ret < 0) {
                    std::cerr << "Failed to copy codec parameters to file stream" << std::endl;
                }
                else {
                    std::cout << "Successfully copied audio codec parameters to file stream" << std::endl;
                }
                file_audio_stream_->time_base = audio_codec_ctx->time_base;
            }
        }
        */
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
            if (file_video_stream_) {
                std::cout << "Video stream time_base: " << file_video_stream_->time_base.num
                    << "/" << file_video_stream_->time_base.den << std::endl;
            }
            /*
            if (file_audio_stream_) {
                std::cout << "Audio stream time_base: " << file_audio_stream_->time_base.num
                    << "/" << file_audio_stream_->time_base.den << std::endl;
            }
            */
        }
    }

    // 流输出
    if (stream_fmt_ctx_ && !streaming_) {
        std::cout << "Writing header for stream output..." << std::endl;

        // 设置视频流参数
        if (encoder_ && stream_video_stream_) {
            auto* codec_ctx = encoder_->getCodecContext();
            if (codec_ctx) {
                int ret = avcodec_parameters_from_context(stream_video_stream_->codecpar, codec_ctx);
                if (ret < 0) {
                    std::cerr << "Failed to copy video codec parameters to stream in start()" << std::endl;
                }
                else {
                    std::cout << "Successfully copied video codec parameters to stream" << std::endl;
                }
                stream_video_stream_->time_base = codec_ctx->time_base;
            }
        }
        /*
        // 设置音频流参数
        if (audio_encoder_ && stream_audio_stream_) {
            auto* audio_codec_ctx = audio_encoder_->getAudioCodecContext();
            if (audio_codec_ctx) {
                int ret = avcodec_parameters_from_context(stream_audio_stream_->codecpar, audio_codec_ctx);
                if (ret < 0) {
                    std::cerr << "Failed to copy audio codec parameters to stream in start()" << std::endl;
                }
                else {
                    std::cout << "Successfully copied audio codec parameters to stream" << std::endl;
                }
                stream_audio_stream_->time_base = {1, audio_codec_ctx->sample_rate};
            }
        }
        */
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
            if (stream_video_stream_) {
                std::cout << "Video stream time_base after header: " << stream_video_stream_->time_base.num
                    << "/" << stream_video_stream_->time_base.den << std::endl;
            }
            /*
            if (stream_audio_stream_) {
                std::cout << "Audio stream time_base after header: " << stream_audio_stream_->time_base.num
                    << "/" << stream_audio_stream_->time_base.den << std::endl;
            }
            */
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
        file_video_stream_ = nullptr;
        file_audio_stream_ = nullptr;
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
        stream_video_stream_ = nullptr;
        stream_audio_stream_ = nullptr;
        streaming_ = false;
        std::cout << "Stop streaming" << std::endl;
    }
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

    if(encoder_){
        file_video_stream_ = avformat_new_stream(file_fmt_ctx_,nullptr);
        if(!file_video_stream_){
            std::cerr<<"Failed to create file stream"<<std::endl;
            return false;
        }
        file_video_stream_->id = file_fmt_ctx_->nb_streams-1;
        file_video_stream_->time_base={1,config_.frame_rate};


        auto* codec_ctx = encoder_->getCodecContext();
        if (codec_ctx) {
            ret = avcodec_parameters_from_context(file_video_stream_->codecpar, codec_ctx);
            if (ret < 0) {
                std::cerr << "Failed to copy codec parameters to file stream" << std::endl;
                return false;
            }
        }
    }
    /*
    if(audio_encoder_){
        file_audio_stream_ = avformat_new_stream(file_fmt_ctx_,nullptr);
        if(!file_audio_stream_){
            std::cerr<<"Failed to create file stream"<<std::endl;
            return false;
        }
        file_audio_stream_->id = file_fmt_ctx_->nb_streams-1;

        auto* audio_codec_ctx = audio_encoder_->getAudioCodecContext();
        if(audio_codec_ctx){
            ret=avcodec_parameters_from_context(file_audio_stream_->codecpar,audio_codec_ctx);
            if(ret<0){
                std::cerr<<"Failed to copy codec parameters to file stream"<<std::endl; 
                return false;
            }
            file_audio_stream_->time_base=audio_codec_ctx->time_base;
        }
    }
    */

    if(!(file_fmt_ctx_->oformat->flags&AVFMT_NOFILE)){
        ret=avio_open(&file_fmt_ctx_->pb,filename_.c_str(),AVIO_FLAG_WRITE);
        if(ret<0){
            std::cerr<<"Failed to open file for writing"<<std::endl;
            return false;
        }
    }

    std::cout<<"File output initialized:"<<filename_<<std::endl;

    if (file_video_stream_) {
        std::cout << "  Video stream: " << file_video_stream_->codecpar->width << "x" 
                  << file_video_stream_->codecpar->height << std::endl;
    }
    /*
    if (file_audio_stream_) {
        std::cout << "  Audio stream: " << file_audio_stream_->codecpar->sample_rate << "Hz, "
                  << file_audio_stream_->codecpar->ch_layout.nb_channels << " channels" << std::endl;
    }
    */
   
    return true;
}

bool OutputManager::setupStreamOutput() {
    std::cout << "=== SETUP STREAM OUTPUT DEBUG ===" << std::endl;
    std::cout << "RTMP URL: " << rtmp_url_ << std::endl;
    std::cout << "Encoder available: " << (encoder_ ? "YES" : "NO") << std::endl;
    
    if (!encoder_) {
        std::cerr << "ERROR: Encoder is not set before setupStreamOutput" << std::endl;
        return false;
    }
    
    // 分配输出上下文
    int ret = avformat_alloc_output_context2(&stream_fmt_ctx_, nullptr, "flv", rtmp_url_.c_str());
    if (ret < 0 || !stream_fmt_ctx_) {
        char error[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(error, sizeof(error), ret);
        std::cerr << "Failed to create stream output context: " << error << std::endl;
        return false;
    }

    // 创建流
    if(encoder_){
        stream_video_stream_ = avformat_new_stream(stream_fmt_ctx_, nullptr);
        if (!stream_video_stream_) {
            std::cerr << "Failed to create stream stream" << std::endl;
            return false;
        }

        stream_video_stream_->id = stream_fmt_ctx_->nb_streams - 1;

        auto* codec_ctx = encoder_->getCodecContext();
        if (codec_ctx) {
            // 复制编码器参数
            ret = avcodec_parameters_from_context(stream_video_stream_->codecpar, codec_ctx);
            if (ret < 0) {
                std::cerr << "Failed to copy codec parameters to stream" << std::endl;
                return false;
            }
            stream_video_stream_->time_base = {1,1000};
        }
    }

    /*
    if(audio_encoder_){
        stream_audio_stream_=avformat_new_stream(stream_fmt_ctx_,nullptr);
        if(!stream_audio_stream_){
            std::cerr<<"Failed to create stream stream"<<std::endl;
            return false;
        }
        stream_audio_stream_->id = stream_fmt_ctx_->nb_streams-1;

        auto* audio_codec_ctx=audio_encoder_->getAudioCodecContext();
        if(audio_codec_ctx){
            ret=avcodec_parameters_from_context(stream_audio_stream_->codecpar,audio_codec_ctx);
            if(ret<0){
                std::cerr<<"Failed to copy codec parameters to stream"<<std::endl;
                return false;
            }
            stream_audio_stream_->time_base = {1,audio_codec_ctx->sample_rate};
        }
    }
    */
    // 显示流信息
    std::cout << "Stream parameters:" << std::endl;
    if (stream_video_stream_) {
        std::cout << "  Video: " << stream_video_stream_->codecpar->width << "x" 
                  << stream_video_stream_->codecpar->height 
                  << ", time_base: " << stream_video_stream_->time_base.num 
                  << "/" << stream_video_stream_->time_base.den << std::endl;
    }
    /*
    if (stream_audio_stream_) {
        std::cout << "  Audio: " << stream_audio_stream_->codecpar->sample_rate << "Hz, "
                  << stream_audio_stream_->codecpar->ch_layout.nb_channels << " channels"
                  << ", time_base: " << stream_audio_stream_->time_base.num 
                  << "/" << stream_audio_stream_->time_base.den << std::endl;
    }
    */
    // 网络选项
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
            stream_video_stream_ = nullptr;
            stream_audio_stream_ = nullptr;
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

     // 添加包信息调试
    std::cout << "=== PACKET INFO ===" << std::endl;
    std::cout << "Packet size: " << packet->size << ", PTS: " << packet->pts 
              << ", DTS: " << packet->dts << ", Duration: " << packet->duration 
              << ", Keyframe: " << (packet->flags & AV_PKT_FLAG_KEY ? "YES" : "NO") << std::endl;

    // 为文件输出写入包
    if (recording_ && file_fmt_ctx_ && file_video_stream_) {
        AVPacket* file_packet = av_packet_clone(packet);
        if (file_packet) {
            if (writePacket(file_packet, file_fmt_ctx_, file_video_stream_)) {
                std::cout << "✓ Successfully wrote packet to file" << std::endl;
            }
            av_packet_free(&file_packet);
        }
    }

    // 为流输出写入包
    if (streaming_ && stream_fmt_ctx_ && stream_video_stream_) {
        AVPacket* stream_packet = av_packet_clone(packet);
        if (stream_packet) {
            if (writePacket(stream_packet, stream_fmt_ctx_, stream_video_stream_)) {
                std::cout << "✓ Successfully wrote packet to stream" << std::endl;
            }
            else {
                std::cerr << "✗ Failed to write packet to stream" << std::endl;
                std::cerr << "Streaming status: " << streaming_ << std::endl;
                std::cerr << "Stream format context: " << (stream_fmt_ctx_ ? "valid" : "null") << std::endl;
                std::cerr << "Video stream: " << (stream_video_stream_ ? "valid" : "null") << std::endl;
                std::cerr << "Stream write failed, but keeping streaming enabled for retry" << std::endl;
                //streaming_ = false;
            }
            av_packet_free(&stream_packet);
        }
    }
}
/*
void OutputManager::onAudioEncodedPacket(AVPacket* packet) {
    if(!packet || packet->size <= 0){
        std::cout << "OutputManager: received empty audio packet, ignoring" << std::endl;
        return;
    }

    // 详细的调试信息
    std::cout << "=== AUDIO PACKET RECEIVED ===" << std::endl;
    std::cout << "Size: " << packet->size << ", PTS: " << packet->pts 
              << ", DTS: " << packet->dts << ", Duration: " << packet->duration << std::endl;
    std::cout << "Recording: " << recording_ << ", FileCtx: " << (file_fmt_ctx_ ? "valid" : "null")
              << ", AudioStream: " << (file_audio_stream_ ? "valid" : "null") << std::endl;

    if(recording_&&file_fmt_ctx_&&file_audio_stream_){
        AVPacket* file_packet = av_packet_clone(packet);
        if(file_packet){
            if(writeAudioPacket(file_packet,file_fmt_ctx_,file_audio_stream_)){
                std::cout<<"✓ Successfully wrote audio packet to file"<<std::endl;
            }
            av_packet_free(&file_packet);
        }
        if (!recording_) std::cout << "OutputManager: not recording" << std::endl;
        if (!file_fmt_ctx_) std::cout << "OutputManager: file_fmt_ctx_ is null" << std::endl;
        if (!file_audio_stream_) std::cout << "OutputManager: file_audio_stream_ is null" << std::endl;
    }

    if(streaming_&&stream_fmt_ctx_&&stream_audio_stream_){
        AVPacket* stream_packet = av_packet_clone(packet);
        if(stream_packet){
            if(writeAudioPacket(stream_packet,stream_fmt_ctx_,stream_audio_stream_)){
                std::cout<<"✓ Successfully wrote audio packet to stream"<<std::endl;
            }
            else{
                std::cerr<<"Stream write failed, disabling streaming"<<std::endl;
            }
            av_packet_free(&stream_packet);
        }
    }
}

*/

bool OutputManager::writePacket(AVPacket* packet, AVFormatContext* fmt_ctx, AVStream* stream) {
    if (!fmt_ctx || !stream || !packet || packet->size <= 0) {
        return false;
    }

    AVPacket* pkt = av_packet_clone(packet);
    if (!pkt) {
        return false;
    }

    pkt->stream_index = stream->index;

    // 时间戳处理 - 从编码器时间基转换到输出流时间基
    auto* codec_ctx = encoder_->getCodecContext();
    if (codec_ctx && pkt->pts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE) {
        AVRational src_time_base = codec_ctx->time_base;
        AVRational dst_time_base = stream->time_base;

         std::cout << "Timebase conversion - SRC: " << src_time_base.num << "/" << src_time_base.den
                  << " -> DST: " << dst_time_base.num << "/" << dst_time_base.den << std::endl;
        std::cout << "Original PTS: " << pkt->pts << ", DTS: " << pkt->dts << std::endl;

        pkt->pts = av_rescale_q(pkt->pts, src_time_base, dst_time_base);
        pkt->dts = av_rescale_q(pkt->dts, src_time_base, dst_time_base);
        if (pkt->duration > 0) {
            pkt->duration = av_rescale_q(pkt->duration, src_time_base, dst_time_base);
        }
        std::cout << "Converted PTS: " << pkt->pts << ", DTS: " << pkt->dts << std::endl;
    }

    // 写入包
    int ret = av_interleaved_write_frame(fmt_ctx, pkt);
    av_packet_free(&pkt);

    if (ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(error, sizeof(error), ret);
        std::cerr << "Failed to write packet: Error number " << ret << " occurred" << std::endl;
        std::cerr << "Error details: " << error << std::endl;
        
        // 添加详细的错误分析
        if (ret == -10053) {
            std::cerr << "ERROR -10053: Connection reset by peer. This usually means:" << std::endl;
            std::cerr << "1. The RTMP server rejected the stream format" << std::endl;
            std::cerr << "2. There are incompatible codec parameters" << std::endl;
            std::cerr << "3. The server closed the connection due to invalid data" << std::endl;
        }
        return false;
    }

    return true;
}
/*
bool OutputManager::writeAudioPacket(AVPacket* packet,AVFormatContext* fmt_ctx,AVStream* stream) {
    if(!fmt_ctx || !stream || !packet || packet->size <= 0) {
        std::cerr << "writeAudioPacket: invalid parameters" << std::endl;
        return false;
    }
    
    AVPacket* pkt = av_packet_clone(packet);
    if (!pkt) {
        std::cerr << "writeAudioPacket: failed to clone packet" << std::endl;
        return false;
    }

    pkt->stream_index = stream->index;

    std::cout << "writeAudioPacket: stream_index=" << pkt->stream_index 
              << ", original PTS=" << pkt->pts << std::endl;

    // 时间戳处理 - 从编码器时间基转换到输出流时间基
    auto* audio_codec_ctx = audio_encoder_->getAudioCodecContext();
    if (audio_codec_ctx) {
        AVRational src_time_base = audio_codec_ctx->time_base;
        AVRational dst_time_base = stream->time_base;

        std::cout << "Timebase - SRC: " << src_time_base.num << "/" << src_time_base.den
                  << ", DST: " << dst_time_base.num << "/" << dst_time_base.den << std::endl;

        if (pkt->pts != AV_NOPTS_VALUE && pkt->pts >= 0) {
            pkt->pts = av_rescale_q(pkt->pts, src_time_base, dst_time_base);
        } else {
            std::cerr << "Invalid audio PTS in packet" << std::endl;
            av_packet_free(&pkt);
            return false;
        }

        if (pkt->dts == AV_NOPTS_VALUE || pkt->dts < 0) {
            pkt->dts = pkt->pts;
        } else {
            pkt->dts = av_rescale_q(pkt->dts, src_time_base, dst_time_base);
        }

        // 设置持续时间
        if (pkt->duration <= 0) {
            pkt->duration = av_rescale_q(audio_codec_ctx->frame_size,
                                        {1, audio_codec_ctx->sample_rate},
                                        dst_time_base);
        } else {
            pkt->duration = av_rescale_q(pkt->duration, src_time_base, dst_time_base);
        }
        
        std::cout << "Audio packet - PTS: " << pkt->pts 
                  << ", DTS: " << pkt->dts
                  << ", Duration: " << pkt->duration << std::endl;
    } else {
        std::cerr << "writeAudioPacket: audio codec context is null" << std::endl;
        av_packet_free(&pkt);
        return false;
    }

    // 写入包
    std::cout << "Calling av_interleaved_write_frame..." << std::endl;
    int ret = av_interleaved_write_frame(fmt_ctx, pkt);
    av_packet_free(&pkt);

    if(ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(error, sizeof(error), ret);
        std::cerr << "Failed to write audio packet: " << error << std::endl;
        return false;
    }

    std::cout << "✓ Successfully wrote audio packet" << std::endl;
    return true;
}
*/

void OutputManager::reset() {
    stop();

    filename_.clear();
    rtmp_url_.clear();
    recording_ = false;
    streaming_ = false;

    file_fmt_ctx_ = nullptr;
    file_video_stream_ = nullptr;
    file_audio_stream_ = nullptr;
    stream_fmt_ctx_ = nullptr;
    stream_video_stream_ = nullptr;
    stream_audio_stream_ = nullptr;

    std::cout << "OutputManager reset completed" << std::endl;
}



bool OutputManager::testRTMPConnection(const std::string& url) {
    std::cout << "Testing RTMP connection to: " << url << std::endl;

    AVFormatContext* fmt_ctx = nullptr;
    AVDictionary* options = nullptr;

    // 设置超时选项
    av_dict_set(&options, "rw_timeout", "5000000", 0); 
    av_dict_set(&options, "stimeout", "5000000", 0);  
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