#pragma once
#include"utils.h"
#include<memory>
#include<mutex>
#include<vector>
#include<functional>

extern"C" {
#include<libavcodec/avcodec.h>
#include<libavutil/opt.h>
#include<libavutil/imgutils.h>
#include<libswscale/swscale.h>
#include<libswresample/swresample.h>
}


struct EncoderConfig{

    //video parameters
    int width=1920;
    int height=1080;
    int frame_rate=30;
    int64_t video_bitrate=4000000;//4Mbps
    AVPixelFormat pixel_format=AV_PIX_FMT_YUV420P;
    int gop_size=30;
    int max_b_frames=0;
    std::string preset="medium";
    std::string tune="zerolatency";

    //audio parameters
    int sample_rate=48000;
    int channels=2;
    AVChannelLayout channel_layout=AV_CHANNEL_LAYOUT_STEREO;
    int64_t audio_bitrate=128000;//128kps
    AVSampleFormat sample_format=AV_SAMPLE_FMT_FLTP;

    std::string video_codec_name="libx264";
    std::string audio_codec_name="aac";

    std::vector<std::pair<std::string,std::string>> codec_options;
};

class Encoder {
public:
    using PacketCallback = std::function<void(AVPacket* packet)>;
    
    Encoder();
    ~Encoder();
    
    bool initialize(const EncoderConfig& config);
    bool initializeAudio(const EncoderConfig& config);
    bool encodeFrame(AVFrame* frame);
    bool encodeAudioFrame(AVFrame* frame);
    bool flush();
    bool reinitialize();
    void reset() { frame_count_ = 0; audio_frame_count_ = 0; }
    void resetAudio() { audio_frame_count_=0;audio_samples_encoded_ = 0; }

    
    void addPacketCallback(PacketCallback callback);
    void addAudioPacketCallback(PacketCallback callback);
    AVCodecContext* getCodecContext() const { return codec_ctx_; }
    AVCodecContext* getAudioCodecContext() const { return audio_codec_ctx_; }
    const EncoderConfig& getConfig() const { return config_; }

    
private:
    
    AVCodecContext* codec_ctx_ = nullptr;
    AVCodecContext* audio_codec_ctx_ = nullptr;
    const AVCodec* codec_ = nullptr;
    const AVCodec* audio_codec_ = nullptr;
    SwsContext* sws_ctx_ = nullptr;
    SwrContext* swr_ctx_ = nullptr;
    EncoderConfig config_;
    
    std::vector<PacketCallback> packet_callbacks_;
    std::vector<PacketCallback> audio_packet_callbacks_;
    std::mutex callback_mutex_;
    std::mutex audio_callback_mutex_;
    
    int64_t frame_count_ = 0;
    int64_t audio_frame_count_ = 0;
    int64_t audio_samples_encoded_ = 0;
};
