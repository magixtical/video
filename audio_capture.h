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
    using AudioCallback = std::function<void(const std::vector<uint8_t>& data, int samples, int64_t timestamp)>;
    
    AudioCapture();
    ~AudioCapture();
    
    bool initialize(int sample_rate = 44100, int channels = 2);
    bool reinitialize();
    bool start();
    void stop();
    bool isCapturing() const { return capturing_; }
    
    void setAudioCallback(AudioCallback callback) { audio_callback_ = callback; }
    
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