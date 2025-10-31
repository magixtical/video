#pragma once
#include"dxgi_capture.h"
#include"encoder.h"
#include"output_manager.h"
#include<mutex>
#include<queue>
#include<thread>
#include<atomic>
#include<condition_variable>

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
    void encode_loop();
    bool convertToAVFrame(const VideoFrame&src,AVFrame* dst);

    std::string output_path_;
    std::string getFilename(const std::string& original_filename);

    RecordConfig config_;
    std::unique_ptr<DXGICapture> capture_;

    std::shared_ptr<MultiEncoder> encoder_;
    OutputManager output_manager_;

    std::atomic<bool> recording_{false};
    std::atomic<bool> streaming_{false};
    std::atomic<bool> running_{false};

    std::thread capture_thread_;
    std::thread encode_thread_;

    std::queue<VideoFrame> frame_queue_;
    std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
    static const int MAX_QUEUE_SIZE = 10;

    AVFrame* av_frame_ = nullptr;
    int64_t frame_count_ = 0;
};