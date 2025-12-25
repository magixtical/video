#pragma once
#include"encoder.h"
#include"time_manager.h"
#include<string>
#include<atomic>
#include<memory>

extern"C"{
    #include<libavformat/avformat.h>
}

class OutputManager {
    public:
    OutputManager();
    ~OutputManager();

    bool initializeFileOutput(const std::string& filename,const EncoderConfig& config);
    bool initializeStreamOutput(const std::string& rtmp_url,const EncoderConfig& config);

    void setEncoder(std::shared_ptr<Encoder> encoder);
    void setAudioEncoder(std::shared_ptr<Encoder> encoder);

    bool start();
    void stop();
    void reset();
    bool isRecording()const{return recording_;}
    bool isStreaming()const{return streaming_;}

    private:
    void onEncodedPacket(AVPacket* packet);
    void onAudioEncodedPacket(AVPacket* packet);
    bool setupFileOutput();
    bool setupStreamOutput();
    bool writePacket(AVPacket* packet,AVFormatContext* fmt_ctx_,AVStream* stream);
    bool writeAudioPacket(AVPacket* packet,AVFormatContext* fmt_ctx_,AVStream* stream);
    
    bool testRTMPConnection(const std::string& url);

    std::shared_ptr<Encoder> encoder_;
    std::shared_ptr<Encoder> audio_encoder_;
    // 文件输出
    AVFormatContext* file_fmt_ctx_ = nullptr;
    AVStream* file_video_stream_ = nullptr;
    AVStream* file_audio_stream_ = nullptr;
    std::string filename_;
    std::atomic<bool> recording_{false};
    
    // 流输出
    AVFormatContext* stream_fmt_ctx_ = nullptr;
    AVStream* stream_video_stream_ = nullptr;
    AVStream* stream_audio_stream_ = nullptr;
    std::string rtmp_url_;
    std::atomic<bool> streaming_{false};
    
    EncoderConfig config_;
};