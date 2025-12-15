#pragma once
#include"dxgi_capture.h"
#include"audio_capture.h"
#include"encoder.h"
#include"output_manager.h"
#include"time_manager.h"
#include<mutex>
#include<queue>
#include<thread>
#include<atomic>
#include<condition_variable>
#include<deque>

struct RecordConfig{
    CaptureConfig capture_config;
    EncoderConfig encoder_config;
    std::string output_filename;
    std::string output_directory;
    std::string rtmp_url;

    bool record_to_file= true;
    bool stream_to_rtmp = true;

};

class ScreenRecorder{
    public:
    ScreenRecorder();
    ~ScreenRecorder();

    bool initialize(const RecordConfig& config);
    bool start();
    void stop();
    bool is_running()const {return recording_;}
    bool is_streaming()const {return streaming_;}
    
    private:

    void capture_loop();
    void audio_capture_loop();
    void encode_loop();
    void audio_encode_loop();
    bool convertToAVFrame(const VideoFrame&src,AVFrame* dst);
    bool convertToAudioFrame(const uint8_t* audio_data,size_t data_size,AVFrame* frame);
    
    std::string output_path_;
    std::string getFilename(const std::string& original_filename);

    RecordConfig config_;
    std::unique_ptr<DXGICapture> capture_;
    std::unique_ptr<AudioCapture> audio_capture_;

    std::shared_ptr<Encoder> encoder_;
    std::shared_ptr<Encoder> audio_encoder_;
    OutputManager output_manager_;

    std::atomic<bool> recording_{false};
    std::atomic<bool> streaming_{false};
    std::atomic<bool> running_{false};

    std::thread capture_thread_;
    std::thread audio_capture_thread_;
    std::thread encode_thread_;
    std::thread audio_encode_thread_;

    // 视频队列
    std::queue<VideoFrame> frame_queue_;
    std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
    static const int MAX_QUEUE_SIZE = 10;
    
    // 音频队列和缓冲区
    std::deque<std::vector<uint8_t>> audio_queue_;
    std::mutex audio_mutex_;
    std::condition_variable audio_cv_;
    static const int AUDIO_QUEUE_SIZE = 30;

    AVFrame* av_frame_ = nullptr;
    AVFrame* audio_frame_ = nullptr;
    int64_t frame_count_ = 0;

    std::vector<uint8_t> audio_buffer_;
    const size_t AUDIO_BUFFER_SIZE = 1024 * 8;
};