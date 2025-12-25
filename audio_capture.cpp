#include "audio_capture.h"
#include <iostream>
#include <avrt.h>
#include <algorithm>
#pragma comment(lib,"avrt.lib")
#pragma comment(lib,"ole32.lib")

namespace{
    void convert_pcm16_to_float(const BYTE* src,float* dst,size_t num_samples){
        const int16_t* pcm_data=reinterpret_cast<const int16_t*>(src);
        for(size_t i=0;i<num_samples;i++){
            dst[i]=static_cast<float>(pcm_data[i])/32768.0f;
        }
    }

    void convert_pcm32_to_float(const BYTE* src,float* dst,size_t num_samples){
        const int32_t* pcm_data=reinterpret_cast<const int32_t*>(src);
        for(size_t i=0;i<num_samples;i++){
            dst[i]=static_cast<float>(pcm_data[i])/2147483648.0f;
        }
    }

    void copy_float_data(const BYTE* src,float* dst,size_t num_samples){
        const float* float_dat=reinterpret_cast<const float*>(src);
        std::memcpy(dst,float_dat,num_samples*sizeof(float));
    }
}

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

    WAVEFORMATEX* mix_format = nullptr;
    hr=audio_client_->GetMixFormat(&mix_format);
    if(FAILED(hr)){
        std::cerr<<"Failed to get mix format: "<<std::hex<<hr<<std::endl;
        return false;
    }

    format_tag_=mix_format->wFormatTag;
    bits_per_sample_=mix_format->wBitsPerSample;

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
    
    // 设置线程优先级
    task_handle = AvSetMmThreadCharacteristics(L"Audio", &task_index);
    
    UINT32 buffer_size = 0;
    audio_client_->GetBufferSize(&buffer_size);
    std::cout << "Audio capture thread started, buffer size: " << buffer_size << std::endl;
    
    int64_t cumulative_samples = 0;

    while (capturing_) {
        UINT32 next_packet_size;
        HRESULT hr = capture_client_->GetNextPacketSize(&next_packet_size);
        if (FAILED(hr)){
            std::cerr << "Failed to get next packet size: 0x" << std::hex << hr << std::dec << std::endl;
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
            std::cerr << "Failed to get buffer: 0x" << std::hex << hr << std::dec << std::endl;
            break;
        }
        
        if (num_frames > 0) {
            AudioPacket packet;
            packet.samples = num_frames;
            packet.sample_rate = sample_rate_;
            packet.channels=channels_;
            packet.format_tag=format_tag_;
            packet.bits_per_sample=bits_per_sample_;
            packet.timestamp=TimeManager::instance().getCurrentPts();
            packet.cumulative_samples=cumulative_samples;
            packet.is_silent=(flags&AUDCLNT_BUFFERFLAGS_SILENT)!=0;

            cumulative_samples+=num_frames;
            
            size_t total_samples=num_frames*channels_;
            packet.data.resize(total_samples);
            
            if((flags&AUDCLNT_BUFFERFLAGS_SILENT)!=0){
                std::fill(packet.data.begin(),packet.data.end(),0.0f);
            }else{
                switch(format_tag_){
                    case WAVE_FORMAT_IEEE_FLOAT:
                        copy_float_data(data,packet.data.data(),total_samples);
                        break;
                    case WAVE_FORMAT_PCM:
                        if(bits_per_sample_==16){
                            convert_pcm16_to_float(data,packet.data.data(),total_samples);
                        }else if(bits_per_sample_==32){
                            convert_pcm32_to_float(data,packet.data.data(),total_samples);
                        }else{
                            std::cerr<<"Unsupported PCM format: "<<bits_per_sample_<<std::endl;
                            std::fill(packet.data.begin(),packet.data.end(),0.0f);
                        }
                        break;
                    default:
                        std::cerr<<"Unsupported audio format:"<<std::hex<<format_tag_<<std::endl;
                        std::fill(packet.data.begin(),packet.data.end(),0.0f);
                        break;
                }
            }
            
            // 调试信息（包括静音标志）
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
                std::cout << "Audio captured [SILENCE]: " << num_frames << " frames" << std::endl;
            } 
            
            // 调用回调
            if (audio_callback_) {
                audio_callback_(packet);
            }
        }
        
        capture_client_->ReleaseBuffer(num_frames);
    }
    
    if (task_handle) {
        AvRevertMmThreadCharacteristics(task_handle);
    }
}