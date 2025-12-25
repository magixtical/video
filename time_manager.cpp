#include "time_manager.h"
#include <iostream>

// 初始化静态常量
const AVRational TimeManager::MICROSECOND_TIMEBASE = {1, 1000000};

TimeManager& TimeManager::instance() {
    static TimeManager instance;
    return instance;
}

void TimeManager::startRecording() {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!is_recording_){
        // 重置所有时间戳
        std::lock_guard<std::mutex> sync_lock(sync_mutex_);
        last_video_pts_ = 0;
        last_audio_pts_ = 0;
        
        start_time_=std::chrono::steady_clock::now();
        is_recording_=true;
        std::cout<<"Start recording at"<<start_time_.time_since_epoch().count()<<std::endl;
    }
}

void TimeManager::stopRecording() {
    std::lock_guard<std::mutex> lock(mutex_);
    is_recording_=false;
    std::cout<<"Stop recording at"<<std::chrono::steady_clock::now().time_since_epoch().count()<<std::endl;
}

int64_t TimeManager::getCurrentPts()const{
    if(!is_recording_){
        std::cerr<<"Not recording"<<std::endl;
        return 0;
    }
    auto now=std::chrono::steady_clock::now();
    auto duration=std::chrono::duration_cast<std::chrono::microseconds>(now-start_time_ );
    return duration.count();
}

int64_t TimeManager::getVideoPts(int frame_count, int frame_rate) const {
    if(!is_recording_ || frame_rate <= 0) {
        return 0;
    }
    
    // 基于帧率计算时间戳（微秒）
    return (int64_t)frame_count * 1000000LL / frame_rate;
}

int64_t TimeManager::getAudioPts(int64_t samples_encoded, int sample_rate) const {
    if(!is_recording_ || sample_rate == 0) {
        return 0;
    }
    
    // 基于采样数计算时间戳（微秒）
    return (samples_encoded * 1000000LL) / sample_rate;
}

int64_t TimeManager::convertTimebase(int64_t pts_microseconds, 
                                     const AVRational& target_timebase) {
    if (pts_microseconds < 0) {
        return AV_NOPTS_VALUE; 
    }
    
    return av_rescale_q(pts_microseconds, MICROSECOND_TIMEBASE, target_timebase);
}

int64_t TimeManager::convertFromTimebase(int64_t pts, const AVRational& src_timebase) {
    if (pts == AV_NOPTS_VALUE || pts < 0) {
        return -1;
    }
    
    return av_rescale_q(pts, src_timebase, MICROSECOND_TIMEBASE);
}

void TimeManager::updateLastVideoPts(int64_t pts) {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    if (pts > last_video_pts_) {
        last_video_pts_ = pts;
    }
}

void TimeManager::updateLastAudioPts(int64_t pts) {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    if (pts > last_audio_pts_) {
        last_audio_pts_ = pts;
    }
}

int64_t TimeManager::getSyncedVideoPts(int frame_count, int frame_rate) {
    if (!is_recording_) {
        return 0;
    }
    
    // 计算基于帧率的理想时间戳
    int64_t ideal_pts = getVideoPts(frame_count, frame_rate);
    
    std::lock_guard<std::mutex> lock(sync_mutex_);
    
    // 如果理想时间戳落后太多，使用最后记录的时间戳
    if (ideal_pts < last_video_pts_) {
        return last_video_pts_;
    }
    
    return ideal_pts;
}

int64_t TimeManager::getSyncedAudioPts(int64_t samples_encoded, int sample_rate)  {
    if(!is_recording_ || sample_rate == 0) {
        return 0;
    }
    
    int64_t current_time_us = getCurrentPts();
    
    int64_t ideal_time_us = (samples_encoded * 1000000LL) / sample_rate;
    
    const int64_t SYNC_THRESHOLD_US = 50000; // 50ms阈值
    
    if (std::abs(current_time_us - ideal_time_us) > SYNC_THRESHOLD_US) {
        std::lock_guard<std::mutex> lock(sync_mutex_);
        if (current_time_us > last_audio_pts_) {
            last_audio_pts_ = current_time_us;
        } else {
            last_audio_pts_ += (1000000LL / sample_rate) * (samples_encoded / 1024);
        }
        return last_audio_pts_;
    }
    
    return ideal_time_us;
}

int64_t TimeManager::calculateFrameDuration(int frame_rate) {
    if (frame_rate <= 0) {
        return 0;
    }
    return 1000000LL / frame_rate;
}

int64_t TimeManager::calculateAudioDuration(int samples, int sample_rate) {
    if (sample_rate <= 0) {
        return 0;
    }
    return (int64_t)samples * 1000000LL / sample_rate;
}