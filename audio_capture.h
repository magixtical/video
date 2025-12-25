// audio_capture.h
#pragma once
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <chrono>
#include"time_manager.h"

class AudioCapture {
public:

    struct AudioPacket {
        std::vector<float> data;  // 音频数据
        int samples;                 // 样本数（每个通道）
        int sample_rate;             // 采样率
        int channels;                // 通道数
        int64_t timestamp;           // 时间戳
        WORD format_tag;             // 格式标签
        WORD bits_per_sample;        // 每个采样位数
        int64_t cumulative_samples;  // 累计样本数
        bool is_silent;              // 是否静音

        int duration_ms() const{
            return (samples*1000)/sample_rate;
        }
        size_t data_size()const{
            return data.size() * sizeof(float);
        }
    };
    
    using AudioCallback = std::function<void(const AudioPacket& packet)>;
    
    AudioCapture();
    ~AudioCapture();
    
    bool initialize(int sample_rate = 44100, int channels = 2);
    bool reinitialize();
    bool start();
    void stop();
    bool isCapturing() const { return capturing_; }
    
    void setAudioCallback(AudioCallback callback) { audio_callback_ = callback; }
    
    // 新增：获取当前音频配置
    int getSampleRate() const { return sample_rate_; }
    int getChannels() const { return channels_; }
    int getBytesPerSample() const { return bytes_per_sample_; }
    WORD getFormatTag() const { return format_tag_; }
    WORD getBitsPerSample() const { return bits_per_sample_; }
    
private:
    void captureThread();
    bool setupAudioClient();
    
    AudioCallback audio_callback_;
    std::atomic<bool> capturing_{false};
    std::thread capture_thread_;
    
    // COM interfaces
    IMMDeviceEnumerator* device_enumerator_ = nullptr;
    IMMDevice* audio_device_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioCaptureClient* capture_client_ = nullptr;
    
    int sample_rate_ = 48000;
    int channels_ = 2;
    int bytes_per_sample_ = 4; 
    WORD format_tag_ = WAVE_FORMAT_IEEE_FLOAT;
    WORD bits_per_sample_ = 0;
};