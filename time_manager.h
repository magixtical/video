// time_manager.h
#pragma once

#include <chrono>
#include <mutex>
#include <atomic>
extern "C" {
    #include <libavutil/rational.h>
    #include<libavutil/avutil.h>
    #include<libavcodec/avcodec.h>
}

class TimeManager {
public:
    static TimeManager& instance();
    
    // 录制控制
    void startRecording();
    void stopRecording();
    bool isRecording() const { return is_recording_; }
    
    // 时间戳获取
    int64_t getCurrentPts() const;  // 返回相对于开始时间的微秒数
    int64_t getVideoPts(int frame_count, int frame_rate) const;  // 基于帧率的视频PTS
    int64_t getAudioPts(int64_t samples_encoded, int sample_rate) const;  // 基于采样数的音频PTS
    
    // 时间基转换
    static int64_t convertTimebase(int64_t pts_microseconds, 
                                   const AVRational& target_timebase);
    static int64_t convertFromTimebase(int64_t pts, const AVRational& src_timebase);
    
    // 时间戳同步
    void updateLastVideoPts(int64_t pts);
    void updateLastAudioPts(int64_t pts);
    int64_t getSyncedVideoPts(int frame_count, int frame_rate) const;
    int64_t getSyncedAudioPts(int64_t samples_encoded, int sample_rate) const;
    
    // 帧间隔计算
    static int64_t calculateFrameDuration(int frame_rate);  // 返回微秒
    static int64_t calculateAudioDuration(int samples, int sample_rate);  // 返回微秒
    
    // 获取基础时间信息
    std::chrono::steady_clock::time_point getStartTime() const { 
        return start_time_; 
    }
    
private:
    TimeManager() = default;
    ~TimeManager() = default;
    
    // 禁用拷贝
    TimeManager(const TimeManager&) = delete;
    TimeManager& operator=(const TimeManager&) = delete;
    
    std::chrono::steady_clock::time_point start_time_;
    std::atomic<bool> is_recording_{false};
    mutable std::mutex mutex_;
    
    // 同步时间戳跟踪
    mutable std::mutex sync_mutex_;
    int64_t last_video_pts_{0};  // 最后一个视频PTS（微秒）
    int64_t last_audio_pts_{0};  // 最后一个音频PTS（微秒）
    
    // 内部时间基常量
    static const AVRational MICROSECOND_TIMEBASE;  // {1, 1000000}
};