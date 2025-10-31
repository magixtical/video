#include "screen_recorder.h"
#include<iostream>
#include<chrono>
#include<filesystem>

ScreenRecorder::ScreenRecorder()=default;
ScreenRecorder::~ScreenRecorder(){
    stop();
    if(av_frame_){
        av_frame_free(&av_frame_);
    }
}

bool ScreenRecorder::initialize(const RecordConfig& config) {
    config_ = config;

    try {
        // 先初始化编码器
        encoder_ = std::make_shared<MultiEncoder>();
        if (!encoder_->initialize(config.encoder_config)) {
            std::cerr << "Failed to initialize encoder" << std::endl;
            return false;
        }

        if (config.stream_to_rtmp) {
            if (!output_manager_.initializeStreamOutput(config.rtmp_url, config.encoder_config)) {
                std::cerr << "Failed to initialize stream output" << std::endl;
                return false;
            }
        }

        output_manager_.setEncoder(encoder_);

        // 然后初始化捕获
        capture_ = std::make_unique<DXGICapture>(config.capture_config);
        capture_->set_frame_callback([this](const VideoFrame& frame) {
            std::cout << "Capture callback: received frame " << frame.width << "x" << frame.height
                << ", data size: " << frame.data.size() << std::endl;

            std::unique_lock<std::mutex> lock(frame_mutex_);

            // 检查是否正在运行
            if (!running_) {
                std::cout << "Capture callback: recorder not running, ignoring frame" << std::endl;
                return;
            }

            // 如果队列已满，丢弃最老的帧
            if (frame_queue_.size() >= MAX_QUEUE_SIZE) {
                std::cout << "Capture callback: queue full, dropping oldest frame" << std::endl;
                frame_queue_.pop();
            }

            frame_queue_.push(frame);
            std::cout << "Capture callback: queued frame, queue size: " << frame_queue_.size() << std::endl;
            frame_cv_.notify_one();
            });

        // AVFrame initialization
        auto* codec_ctx = encoder_->getCodecContext();
        av_frame_ = av_frame_alloc();
        av_frame_->width = codec_ctx->width;
        av_frame_->height = codec_ctx->height;
        av_frame_->format = codec_ctx->pix_fmt;

        int ret = av_frame_get_buffer(av_frame_, 0);
        if (ret < 0) {
            std::cerr << "Failed to allocate frame buffer" << std::endl;
            return false;
        }

        std::cout << "Screen recorder initialized successfully" << std::endl;
        return true;

    }
    catch (const std::exception& e) {
        std::cerr << "Failed to initialize screen recorder: " << e.what() << std::endl;
        return false;
    }
}

bool ScreenRecorder::start(){
    if(running_){
        return true;
    }

    std::cout<<"Starting screen recorder..."<<std::endl;

    output_manager_.reset();

    if (encoder_) {
        if (!encoder_->reinitialize()) {
            std::cerr << "Failed to reinitialize encoder" << std::endl;
            return false;
        }
        std::cout << "Encoder reinitialized successfully" << std::endl;
    }

    if (config_.record_to_file) {
        std::string new_output_path_ = getFilename(config_.output_filename);
        if (!config_.output_directory.empty()) {
            std::filesystem::create_directories(config_.output_directory);
            new_output_path_ = config_.output_directory + "/" + new_output_path_;
        }

        // 初始化文件输出
        if (!output_manager_.initializeFileOutput(new_output_path_, config_.encoder_config)) {
            std::cerr << "Failed to initialize file output in start()" << std::endl;
            return false;
        }
    }

    // 重新初始化流输出（如果需要推流）
    if (config_.stream_to_rtmp) {
        if (!output_manager_.initializeStreamOutput(config_.rtmp_url, config_.encoder_config)) {
            std::cerr << "Failed to re-initialize stream output" << std::endl;
            return false;
        }
    }

    // 重新关联编码器
    output_manager_.setEncoder(encoder_);

    if (!output_manager_.start()) {
        std::cerr << "Failed to start output manager" << std::endl;
        return false;
    }




    frame_count_ = 0;

    running_ = true;
    recording_ = config_.record_to_file;
    streaming_ = config_.stream_to_rtmp;

    capture_thread_ = std::thread(&ScreenRecorder::capture_loop, this);
    encode_thread_ = std::thread(&ScreenRecorder::encode_loop, this);

    std::cout << "Screen recorder started successfully" << std::endl;
    return true;
}

void ScreenRecorder::stop(){
    if(!running_){
        return;
    }
    std::cout<<"Stopping screen recorder..."<<std::endl;
    running_=false;
    frame_cv_.notify_all();
    if(capture_thread_.joinable())
    capture_thread_.join();
    if(encode_thread_.joinable())
    encode_thread_.join();

    capture_->stop();
    output_manager_.stop();
    if(encoder_)
    encoder_->flush();
    recording_=false;
    streaming_=false;
    std::cout<<"Screen recorder stopped successfully"<<std::endl;
}

void ScreenRecorder::capture_loop(){
    std::cout<<"Capture thread started"<<std::endl;
    if(!capture_->start()){
        std::cerr<<"Failed to start capture"<<std::endl;
        return;
    }
    while(running_){
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    capture_->stop();
    std::cout<<"Capture thread stopped"<<std::endl;
}

void ScreenRecorder::encode_loop(){
    std::cout<<"Encode thread started"<<std::endl;
    const auto frame_interval=std::chrono::milliseconds(1000/config_.encoder_config.frame_rate);
    while(running_){
        VideoFrame frame;
        {
            std::unique_lock<std::mutex> lock(frame_mutex_);
            if(frame_cv_.wait_for(lock,frame_interval,[this](){
                return !frame_queue_.empty()|| !running_;
            })){
                if(!running_)break;
                if (frame_queue_.empty()) {
                    continue;
                }
                frame=frame_queue_.front();
                frame_queue_.pop();
            }
            else{
                continue;
            }
        }
        if(convertToAVFrame(frame,av_frame_)){
            encoder_->encodeFrame(av_frame_);
        }
    }
    std::cout<<"Encode thread stopped"<<std::endl;
}

bool ScreenRecorder::convertToAVFrame(const VideoFrame& src, AVFrame* dst) {
    if (!dst) {
        std::cerr << "AVFrame is null in convertToAVFrame" << std::endl;
        return false;
    }

    if (src.width != dst->width || src.height != dst->height) {
        std::cerr << "Frame size mismatch in convertToAVFrame: "
            << src.width << "x" << src.height << " vs "
            << dst->width << "x" << dst->height << std::endl;
        return false;
    }

    if (src.data.empty()) {
        std::cerr << "Source frame data is empty" << std::endl;
        return false;
    }

    std::cout << "Converting frame #" << frame_count_ << ", size: " << src.width << "x" << src.height << std::endl;

    size_t y_size = src.width * src.height;
    size_t uv_size = y_size / 4;

    // 检查数据大小
    if (src.data.size() < y_size * 3 / 2) {
        std::cerr << "Source data too small: " << src.data.size() << " < " << (y_size * 3 / 2) << std::endl;
        return false;
    }

    // 复制Y平面
    for (int y = 0; y < src.height; y++) {
        memcpy(dst->data[0] + y * dst->linesize[0],
            src.data.data() + y * src.width,
            src.width);
    }

    // 复制U平面
    for (int y = 0; y < src.height / 2; y++) {
        memcpy(dst->data[1] + y * dst->linesize[1],
            src.data.data() + y_size + y * (src.width / 2),
            src.width / 2);
    }

    // 复制V平面
    for (int y = 0; y < src.height / 2; y++) {
        memcpy(dst->data[2] + y * dst->linesize[2],
            src.data.data() + y_size + uv_size + y * (src.width / 2),
            src.width / 2);
    }

    // 使用递增的时间戳
    dst->pts = frame_count_++;
    std::cout << "Frame converted successfully, pts: " << dst->pts << std::endl;

    return true;
}

std::string ScreenRecorder::getFilename(const std::string& original_filename){
    auto now=std::chrono::system_clock::now();
    auto time_t=std::chrono::system_clock::to_time_t(now);
    auto milliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()%1000;
    std::tm tm;
    #ifdef _WIN32
    localtime_s(&tm,&time_t);
    #else
    localtime_r(&time_t,&tm);
    #endif
    std::ostringstream oss;
    oss<<std::put_time(&tm,"%Y%m%d_%H%M%S");
    std::string timestamp=oss.str();

    std::ostringstream ms_oss;
    ms_oss << timestamp << "_" << std::setw(3) << std::setfill('0') << milliseconds;
    timestamp = ms_oss.str();

    size_t dot_pos = original_filename.find_last_of('.');
        
    if (dot_pos != std::string::npos) {

        std::string base_name = original_filename.substr(0, dot_pos);
        std::string extension = original_filename.substr(dot_pos);
        return base_name + "_" + timestamp + extension;
    } else {
        return original_filename + "_" + timestamp;
    }
}

