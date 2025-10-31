#pragma once
#include"encoder.h"
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

    void setEncoder(std::shared_ptr<MultiEncoder> encoder);

    bool start();
    void stop();
    void reset();
    bool isRecording()const{return recording_;}
    bool isStreaming()const{return streaming_;}

    private:
    void onEncodedPacket(AVPacket* packet);
    bool setupFileOutput();
    bool setupStreamOutput();
    bool writePacket(AVPacket* packet,AVFormatContext* fmt_ctx_,AVStream* stream);
    bool testRTMPConnection(const std::string& url);

    std::shared_ptr<MultiEncoder> encoder_;
    // 文件输出
    AVFormatContext* file_fmt_ctx_ = nullptr;
    AVStream* file_stream_ = nullptr;
    std::string filename_;
    std::atomic<bool> recording_{false};
    
    // 流输出
    AVFormatContext* stream_fmt_ctx_ = nullptr;
    AVStream* stream_stream_ = nullptr;
    std::string rtmp_url_;
    std::atomic<bool> streaming_{false};
    
    EncoderConfig config_;
    int64_t start_time_ = 0;
    int64_t pts_counter_= 0;
};