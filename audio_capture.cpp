#include"audio_capture.h"
#include <iostream>
#include <avrt.h>
#pragma comment(lib,"avrt.lib")
#pragma comment(lib,"ole32.lib")

AudioCapture::AudioCapture()=default;

AudioCapture::~AudioCapture(){
    stop();
    if (capture_client_) capture_client_->Release();
    if (audio_client_) audio_client_->Release();
    if (audio_device_) audio_device_->Release();
    if (device_enumerator_) device_enumerator_->Release();
    
    CoUninitialize();
}

bool AudioCapture::initialize(int sample_rate, int channels){
    sample_rate_ = sample_rate;
    channels_=channels;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize COM library: " << std::hex << hr << std::endl;
        return false;
    }

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                         CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                         (void**)&device_enumerator_);
    if (FAILED(hr)) return false;
    
    hr = device_enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &audio_device_);
    if (FAILED(hr)) return false;
    
    return setupAudioClient();
}

bool AudioCapture::reinitialize() {
    stop();
     // 重新初始化 COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "Failed to reinitialize COM library: " << std::hex << hr << std::endl;
        return false;
    }

    // 重新创建设备枚举器
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                        (void**)&device_enumerator_);
    if (FAILED(hr)) return false;
        
    hr = device_enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &audio_device_);
    if (FAILED(hr)) return false;
        
    return setupAudioClient();
}


bool AudioCapture::setupAudioClient() {
    HRESULT hr = audio_device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                        nullptr, (void**)&audio_client_);
    if (FAILED(hr)) {
        std::cerr<<"Failed to activate Audio Device: "<<std::hex<<hr<<std::endl;
        return false;
    }

    // 获取设备信息
    LPWSTR device_id = nullptr;
    hr = audio_device_->GetId(&device_id);
    if (SUCCEEDED(hr)) {
        std::wcout << L"Audio device ID: " << device_id << std::endl;
        CoTaskMemFree(device_id);
    }

    WAVEFORMATEX* mix_format = nullptr;
    hr=audio_client_->GetMixFormat(&mix_format);
    if(FAILED(hr)){
        std::cerr<<"Failed to get mix format: "<<std::hex<<hr<<std::endl;
        return false;
    }

    std::cout << "Audio mix format: " << mix_format->nSamplesPerSec << "Hz, " 
              << mix_format->nChannels << " channels, " 
              << mix_format->wBitsPerSample << " bits, format tag: 0x" 
              << std::hex << mix_format->wFormatTag << std::dec << std::endl;

    hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        10000000, 0, mix_format, nullptr);

    if (FAILED(hr)) {
        std::cerr << "Failed to initialize audio client with mix format: 0x" << std::hex << hr << std::dec << std::endl;
        CoTaskMemFree(mix_format);
        return false;
    }

    hr = audio_client_->GetService(__uuidof(IAudioCaptureClient),
        (void**)&capture_client_);
    if (FAILED(hr)) {
        std::cerr << "Failed to get capture client: 0x" << std::hex << hr << std::dec << std::endl;
        CoTaskMemFree(mix_format);
        return false;
    }

    // 保存实际的音频格式信息
    sample_rate_ = mix_format->nSamplesPerSec;
    channels_ = mix_format->nChannels;
    bytes_per_sample_ = mix_format->wBitsPerSample / 8;

    std::cout << "Audio capture initialized successfully: " << sample_rate_ << "Hz, "
        << channels_ << " channels, " << bytes_per_sample_ << " bytes per sample" << std::endl;

    CoTaskMemFree(mix_format);
    return true;
}

bool AudioCapture::start() {
    if (capturing_) return true;

    if (!audio_client_) {
        std::cerr << "Audio client is null, reinitializing..." << std::endl;
        if (!setupAudioClient()) {
            std::cerr << "Failed to reinitialize audio client" << std::endl;
            return false;
        }
    }
    
    HRESULT hr = audio_client_->Start();
    if (FAILED(hr)) return false;
    
    capturing_ = true;
    capture_thread_ = std::thread(&AudioCapture::captureThread, this);
    
    return true;
}

void AudioCapture::stop() {
    if (!capturing_) return;
    
    capturing_ = false;
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    
    if (audio_client_) {
        audio_client_->Stop();
    }
    
    if (capture_client_) {
        capture_client_->Release();
        capture_client_ = nullptr;
    }
}

void AudioCapture::captureThread() {
    HANDLE task_handle = nullptr;
    DWORD task_index = 0;
    
    // Set thread priority for audio capture
    task_handle = AvSetMmThreadCharacteristics(L"Audio", &task_index);
    
    const UINT32 buffer_duration = 100000; // 100ms buffer
    UINT32 packet_size = 0;
    audio_client_->GetBufferSize(&packet_size);
    std::cout<<"Audio capture thread start,buffer size: "<<packet_size<<std::endl;
    
    int capture_count=0;

    while (capturing_) {
        UINT32 next_packet_size;
        HRESULT hr = capture_client_->GetNextPacketSize(&next_packet_size);
        if (FAILED(hr)){
            std::cerr<<"Failed to get next packet size: "<<std::hex<<hr<<std::endl;
            break;
        }
        
        if (next_packet_size == 0) {
            Sleep(1);
            continue;
        }
        
        BYTE* data;
        UINT32 num_frames;
        DWORD flags;
        UINT64 device_position, qpc_position;
        
        hr = capture_client_->GetBuffer(&data, &num_frames, &flags,
                                       &device_position, &qpc_position);
        if (FAILED(hr)) {
            std::cerr<<"Failed to get buffer: "<<std::hex<<hr<<std::endl;
            break;
        }
        
        if (num_frames > 0) {
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                std::cout << "Audio buffer is silent, frames: " << num_frames << std::endl;
            } else {
                size_t data_size = num_frames * channels_ * sizeof(float);
                std::vector<uint8_t> audio_data(data_size);
                memcpy(audio_data.data(), data, data_size);
                
                // 检查音频数据是否非零（有声音）
                bool has_audio = false;
                const float* audio_float = reinterpret_cast<const float*>(audio_data.data());
                for (size_t i = 0; i < num_frames * channels_; i++) {
                    if (std::abs(audio_float[i]) > 0.001f) {
                        has_audio = true;
                        break;
                    }
                }
                
                if (has_audio) {
                    std::cout << "Audio captured: " << num_frames << " frames, " 
                              << data_size << " bytes (contains audio)" << std::endl;
                } else {
                    std::cout << "Audio captured: " << num_frames << " frames, " 
                              << data_size << " bytes (silent)" << std::endl;
                }
                
                if (audio_callback_) {
                    int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    audio_callback_(audio_data, num_frames, timestamp);
                }
                
                capture_count++;
                if (capture_count % 100 == 0) { // 每100次打印一次统计
                    std::cout << "Audio capture statistics: " << capture_count << " buffers processed" << std::endl;
                }
            }
        }
        
        capture_client_->ReleaseBuffer(num_frames);
    }
    
    if (task_handle) {
        AvRevertMmThreadCharacteristics(task_handle);
    }
}