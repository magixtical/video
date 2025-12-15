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

class AudioCapture {
public:
    // 简化：音频包只包含必要信息
    struct AudioPacket {
        std::vector<uint8_t> data;  // 音频数据（float格式，交错立体声）
        int samples;                 // 样本数（每个通道）
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
};