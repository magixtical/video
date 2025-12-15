#include "screen_recorder.h"
#include<iostream>
#include<chrono>
#include<filesystem>
#include<iomanip>
#include<sstream>

ScreenRecorder::ScreenRecorder()=default;
ScreenRecorder::~ScreenRecorder(){
    stop();
    if(av_frame_){
        av_frame_free(&av_frame_);
    }
    /*
    if(audio_frame_){
        av_frame_free(&audio_frame_);
    }
    */
}

bool ScreenRecorder::initialize(const RecordConfig& config) {
    config_ = config;

    try {
        // 初始化视频编码器
        encoder_ = std::make_shared<Encoder>();
        if (!encoder_->initialize(config.encoder_config)) {
            std::cerr << "Failed to initialize encoder" << std::endl;
            return false;
        }

        /*
        // 初始化音频编码器和捕获
        audio_encoder_ = std::make_shared<Encoder>();
        bool audio_initialized = false;

        if(audio_encoder_->initializeAudio(config.encoder_config)){
            audio_capture_ = std::make_unique<AudioCapture>();
            if(audio_capture_->initialize(config.encoder_config.sample_rate, config.encoder_config.channels)){
                // 简化音频回调，直接加入队列
                audio_capture_->setAudioCallback([this](const AudioCapture::AudioPacket& packet){
                    std::unique_lock<std::mutex> lock(audio_mutex_);
                    
                    if(audio_queue_.size() >= AUDIO_QUEUE_SIZE){
                        audio_queue_.pop_front();
                    }
                    
                    audio_queue_.push_back(packet.data);
                    audio_cv_.notify_one();
                });

                // 初始化音频帧
                auto* audio_codec_ctx = audio_encoder_->getAudioCodecContext();
                if (audio_codec_ctx) {
                    audio_frame_ = av_frame_alloc();
                    audio_frame_->format = audio_codec_ctx->sample_fmt;
                    audio_frame_->ch_layout = audio_codec_ctx->ch_layout;
                    audio_frame_->sample_rate = audio_codec_ctx->sample_rate;
                    audio_frame_->nb_samples = audio_codec_ctx->frame_size;

                    int ret = av_frame_get_buffer(audio_frame_, 0);
                    if(ret >= 0){
                        audio_initialized = true;
                        std::cout << "Audio system initialized successfully" << std::endl;
                        std::cout << "  Frame size: " << audio_frame_->nb_samples << " samples" << std::endl;
                        std::cout << "  Sample rate: " << audio_frame_->sample_rate << "Hz" << std::endl;
                    } else {
                        std::cerr << "Failed to allocate audio frame buffer" << std::endl;
                    }
                }
            } else {
                std::cerr << "Failed to initialize audio capture - continuing without audio" << std::endl;
            }
            
            // 设置音频编码器到输出管理器
            output_manager_.setAudioEncoder(audio_encoder_);
        } else {
            std::cerr << "Failed to initialize audio encoder - continuing without audio" << std::endl;
        }

        if (!audio_initialized) {
            audio_encoder_.reset();
            audio_capture_.reset();
            if (audio_frame_) {
                av_frame_free(&audio_frame_);
                audio_frame_ = nullptr;
            }
        }
        */
        // 初始化输出
        if (config.stream_to_rtmp) {
            if (!output_manager_.initializeStreamOutput(config.rtmp_url, config.encoder_config)) {
                std::cerr << "Failed to initialize stream output" << std::endl;
                return false;
            }
        }

        output_manager_.setEncoder(encoder_);

        // 初始化视频捕获
        capture_ = std::make_unique<DXGICapture>(config.capture_config);
        capture_->set_frame_callback([this](const VideoFrame& frame) {
            std::cout << "Capture callback: received frame " << frame.width << "x" << frame.height
                << ", data size: " << frame.data.size() << std::endl;

            std::unique_lock<std::mutex> lock(frame_mutex_);

            if (!running_) {
                std::cout << "Capture callback: recorder not running, ignoring frame" << std::endl;
                return;
            }

            if (frame_queue_.size() >= MAX_QUEUE_SIZE) {
                std::cout << "Capture callback: queue full, dropping oldest frame" << std::endl;
                frame_queue_.pop();
            }

            frame_queue_.push(frame);
            std::cout << "Capture callback: queued frame, queue size: " << frame_queue_.size() << std::endl;
            frame_cv_.notify_one();
        });

        // 初始化视频AVFrame
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

    // 启动TimeManager
    TimeManager::instance().startRecording();

    // 重置所有状态
    output_manager_.reset();

    /*
    
    // 清理队列
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        audio_queue_.clear();
    }
    audio_buffer_.clear();

    // 重新初始化音频捕获
    if (audio_capture_ && !audio_capture_->reinitialize()) {
        std::cerr << "Failed to reinitialize audio capture - continuing without audio" << std::endl;
        audio_capture_.reset();
        audio_encoder_.reset();
    }

    */

    // 重新初始化编码器
    if (encoder_) {
        if (!encoder_->reinitialize()) {
            std::cerr << "Failed to reinitialize encoder" << std::endl;
            return false;
        }
        std::cout << "Encoder reinitialized successfully" << std::endl;
    }

    /*
    if(audio_encoder_) {
        audio_encoder_->resetAudio();
    }
    */
    
    frame_count_ = 0;

    // 设置文件输出
    if (config_.record_to_file) {
        std::string new_output_path_ = getFilename(config_.output_filename);
        if (!config_.output_directory.empty()) {
            std::filesystem::create_directories(config_.output_directory);
            new_output_path_ = config_.output_directory + "/" + new_output_path_;
        }

        if (!output_manager_.initializeFileOutput(new_output_path_, config_.encoder_config)) {
            std::cerr << "Failed to initialize file output in start()" << std::endl;
            return false;
        }
    }

    // 设置流输出
    if (config_.stream_to_rtmp) {
        if (!output_manager_.initializeStreamOutput(config_.rtmp_url, config_.encoder_config)) {
            std::cerr << "Failed to re-initialize stream output" << std::endl;
            return false;
        }
    }

    // 重新关联编码器
    output_manager_.setEncoder(encoder_);
    //output_manager_.setAudioEncoder(audio_encoder_);

    if (!output_manager_.start()) {
        std::cerr << "Failed to start output manager" << std::endl;
        return false;
    }

    // 启动所有线程
    running_ = true;
    recording_ = config_.record_to_file;
    streaming_ = config_.stream_to_rtmp;

    capture_thread_ = std::thread(&ScreenRecorder::capture_loop, this);
    /*
    if (audio_capture_) {
        audio_capture_thread_ = std::thread(&ScreenRecorder::audio_capture_loop, this);
    }*/
    encode_thread_ = std::thread(&ScreenRecorder::encode_loop, this);
    /*
    if (audio_encoder_) { 
        audio_encode_thread_ = std::thread(&ScreenRecorder::audio_encode_loop, this);
    }
    */
    std::cout << "Screen recorder started successfully" << std::endl;
    return true;
}

void ScreenRecorder::stop(){
    if(!running_){
        return;
    }
    std::cout<<"Stopping screen recorder..."<<std::endl;

    running_ = false;
    frame_cv_.notify_all();
    //audio_cv_.notify_all();

    /*
    // 停止捕获
    if (audio_capture_) {
       audio_capture_->stop();
    }
    */
    if (capture_) {
        capture_->stop();
    }

    // 等待线程结束
    if(capture_thread_.joinable())
        capture_thread_.join();
    /*
    if(audio_capture_thread_.joinable())
        audio_capture_thread_.join();
    */
    if(encode_thread_.joinable())
        encode_thread_.join();
    /*
    if(audio_encode_thread_.joinable())
        audio_encode_thread_.join();
    */
    /*
    // 处理剩余的音频数据
    if (audio_encoder_ && audio_frame_ && !audio_buffer_.empty()) {
        std::cout << "Encoding remaining audio data: " << audio_buffer_.size() << " bytes" << std::endl;
        
        auto* audio_codec_ctx = audio_encoder_->getAudioCodecContext();
        int bytes_per_frame = audio_frame_->nb_samples * 2 * sizeof(float);
        
        // 如果缓冲区有数据但不够一帧，用静音填充
        if (audio_buffer_.size() < bytes_per_frame) {
            size_t silence_needed = bytes_per_frame - audio_buffer_.size();
            audio_buffer_.insert(audio_buffer_.end(), silence_needed, 0);
            std::cout << "Added " << silence_needed << " bytes of silence to complete frame" << std::endl;
        }
        
        // 编码最后一帧
        if (convertToAudioFrame(audio_buffer_.data(), bytes_per_frame, audio_frame_)) {
            audio_encoder_->encodeAudioFrame(audio_frame_);
        }
    }
    */

    // 刷新编码器
    if(encoder_)
        encoder_->flush();
    /*
    if(audio_encoder_)
        audio_encoder_->flush();
    */

    // 停止输出管理器
    output_manager_.stop();
    
    // 停止TimeManager
    TimeManager::instance().stopRecording();

    // 重置状态
    recording_ = false;
    streaming_ = false;
    
    // 清理缓冲区
    /*
    audio_buffer_.clear();
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        audio_queue_.clear();
    }
    */
    
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
/*
void ScreenRecorder::audio_capture_loop(){
    std::cout<<"Audio capture thread started"<<std::endl;
    if(!audio_capture_->start()){
        std::cerr<<"Failed to start audio capture"<<std::endl;
        return ;
    }
    while(running_){
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    audio_capture_->stop();
    std::cout<<"Audio capture thread stopped"<<std::endl;
}*/

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

/*
void ScreenRecorder::audio_encode_loop(){
    std::cout<<"Audio encode thread started"<<std::endl;

    if (!audio_encoder_ || !audio_frame_) {
        std::cerr << "Audio encoder or frame not initialized" << std::endl;
        return;
    }

    auto* audio_codec_ctx = audio_encoder_->getAudioCodecContext();
    const int samples_per_frame = audio_codec_ctx->frame_size;
    const int bytes_per_frame = samples_per_frame * 2 * sizeof(float);
    const int frame_duration_ms = (samples_per_frame * 1000) / audio_codec_ctx->sample_rate;

    std::cout << "Audio encode config:" << std::endl;
    std::cout << "  Samples per frame: " << samples_per_frame << std::endl;
    std::cout << "  Bytes per frame: " << bytes_per_frame << std::endl;
    std::cout << "  Frame duration: " << frame_duration_ms << "ms" << std::endl;

    while(running_){
        std::vector<uint8_t> audio_data;
        bool has_data = false;
        
        {
            std::unique_lock<std::mutex> lock(audio_mutex_);
            if (audio_cv_.wait_for(lock, std::chrono::milliseconds(frame_duration_ms), [this]() {
                return !audio_queue_.empty() || !running_;
            })) {
                if (!running_)
                    break;
                if (!audio_queue_.empty()) {
                    audio_data = audio_queue_.front();
                    audio_queue_.pop_front();
                    has_data = true;
                }
            }
        }
        
        if (!running_)
            break;

        if (has_data && !audio_data.empty()) {
            audio_buffer_.insert(audio_buffer_.end(), audio_data.begin(), audio_data.end());
            std::cout << "Audio buffer size after adding data: " << audio_buffer_.size() 
                      << " (needs " << bytes_per_frame << " for a frame)" << std::endl;
        }

        // 编码完整的音频帧
        while (audio_buffer_.size() >= bytes_per_frame && audio_frame_) {
            std::cout << "Audio encode: buffer has enough data for encoding ("
                      << audio_buffer_.size() << " >= " << bytes_per_frame << ")" << std::endl;
            
            if (convertToAudioFrame(audio_buffer_.data(), bytes_per_frame, audio_frame_)) {
                std::cout << "Audio encode: converted to frame, samples: " << audio_frame_->nb_samples << std::endl;
                
                if (audio_encoder_->encodeAudioFrame(audio_frame_)) {
                    std::cout << "Audio encode: successfully encoded frame" << std::endl;
                } else {
                    std::cerr << "Audio encode: failed to encode frame" << std::endl;
                }
            } else {
                std::cerr << "Audio encode: failed to convert to audio frame" << std::endl;
            }

            // 移除已处理的数据
            audio_buffer_.erase(audio_buffer_.begin(), audio_buffer_.begin() + bytes_per_frame);
            std::cout << "Audio buffer after encoding: " << audio_buffer_.size() << " bytes" << std::endl;
        }
    }

    std::cout << "Audio encode thread stopped" << std::endl;
}
*/
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

    dst->pts = frame_count_++;
    std::cout << "Frame converted successfully, pts: " << dst->pts << std::endl;

    return true;
}
/*
bool ScreenRecorder::convertToAudioFrame(const uint8_t* audio_data,size_t data_size,AVFrame* frame){
    if (!frame || !audio_data || data_size == 0) {
        std::cerr << "convertToAudioFrame: invalid parameters" << std::endl;
        return false;
    }

    auto* audio_codec_ctx = audio_encoder_->getAudioCodecContext();
    if (!audio_codec_ctx) {
        std::cerr << "convertToAudioFrame: audio codec context is null" << std::endl;
        return false;
    }

    // 计算样本数 (每个样本: 2通道 * 4字节)
    int samples = data_size / (2 * sizeof(float));
    
    if (samples <= 0) {
        std::cerr << "convertToAudioFrame: insufficient data, samples: " << samples << std::endl;
        return false;
    }

    // 检查帧大小是否匹配
    if (frame->nb_samples != samples) {
        std::cout << "Adjusting audio frame size from " << frame->nb_samples << " to " << samples << std::endl;

        av_frame_unref(frame);
        frame->nb_samples = samples;
        frame->format = audio_codec_ctx->sample_fmt;
        frame->ch_layout = audio_codec_ctx->ch_layout;
        frame->sample_rate = audio_codec_ctx->sample_rate;
        
        // 重新分配缓冲区
        int ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) {
            std::cerr << "Failed to reallocate audio frame buffer" << std::endl;
            return false;
        }
    }

    // 复制数据到 AVFrame
    if (frame->format == AV_SAMPLE_FMT_FLTP) {
        const float* interleaved_data = reinterpret_cast<const float*>(audio_data);
        float* planar_left = reinterpret_cast<float*>(frame->data[0]);
        float* planar_right = reinterpret_cast<float*>(frame->data[1]);
        
        for (int i = 0; i < samples; i++) {
            planar_left[i] = interleaved_data[i * 2];        // 左声道
            planar_right[i] = interleaved_data[i * 2 + 1];   // 右声道
        }
        
        std::cout << "convertToAudioFrame: converted " << samples << " samples (interleaved to planar)" << std::endl;
    } else {
        // 直接复制
        memcpy(frame->data[0], audio_data, data_size);
        std::cout << "convertToAudioFrame: copied " << data_size << " bytes directly" << std::endl;
    }
    
    frame->pts = AV_NOPTS_VALUE;

    std::cout << "Audio frame converted successfully" << std::endl;
    
    return true;
}
*/
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