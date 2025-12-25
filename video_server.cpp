/*
#include "config.h"
#include "hls_generator.h"
#include "HttpServer.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <filesystem>

std::atomic<bool> stop_signal{ false };

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    stop_signal = true;
}

int main() {
    try {
        // 设置信号处理
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        Config config;

        // 检查输入文件
        if (!std::filesystem::exists(config.VIDEO_PATH)) {
            std::cerr << "Error: Input video file not found: " << config.VIDEO_PATH << std::endl;
            return 1;
        }

        // 创建输出目录
        if (!std::filesystem::exists(config.HLS_DIR)) {
            std::filesystem::create_directories(config.HLS_DIR);
            std::cout << "Created HLS directory: " << config.HLS_DIR << std::endl;
        }

        std::cout << "=== HLS Streaming Server ===" << std::endl;
        std::cout << "Input video: " << config.VIDEO_PATH << std::endl;
        std::cout << "Output directory: " << config.HLS_DIR << std::endl;
        std::cout << "HTTP port: " << config.HTTP_PORT << std::endl;
        std::cout << "=============================" << std::endl;

        // 生成HLS流
        std::cout << "\nGenerating HLS stream..." << std::endl;
        HLSGenerator generator(config);
        generator.start();
        std::cout << "HLS generation completed!" << std::endl;

        // 启动HTTP服务器
        std::cout << "\nStarting HTTP server..." << std::endl;
        HttpServer server(config);
        server.start();

        std::cout << "\nServer is running!" << std::endl;
        std::cout << "Stream URL: http://localhost:" << config.HTTP_PORT
            << "/" << config.M3U8_FILENAME << std::endl;
        std::cout << "Press Ctrl+C to stop the server." << std::endl;

        // 等待停止信号
        while (!stop_signal && server.is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "Shutting down server..." << std::endl;
        server.stop();

    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Server stopped successfully." << std::endl;
    return 0;
}
*/

// main.cpp
#include "screen_recorder.h"
#include <iostream>
#include <conio.h>
#include <signal.h>

void test_basic_region_capture() {
    std::cout << "=== 测试基础区域捕获 ===" << std::endl;
    
    CaptureConfig config;
    config.capture_full_screen = false;
    config.capture_region = true;
    config.capture_rect = {100, 100, 800, 600};  // 100x100到800x600区域
    config.frame_rate = 30;
    config.region_quality = 1;  // 快速模式
    
    try {
        DXGICapture capture(config);
        std::cout << "✅ DXGICapture对象创建成功" << std::endl;
        
        // 测试设置区域
        if (capture.set_capture_region(200, 200, 400, 300)) {
            std::cout << "✅ 区域设置成功" << std::endl;
        } else {
            std::cout << "❌ 区域设置失败" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "❌ 初始化失败: " << e.what() << std::endl;
    }
}

void test_frame_capture() {
    std::cout << "\n=== 测试帧捕获 ===" << std::endl;
    
    CaptureConfig config;
    config.capture_full_screen = true;  // 先测试全屏捕获
    config.frame_rate = 60;
    
    try {
        DXGICapture capture(config);
        
        capture.set_frame_callback([](const VideoFrame& frame) {
            std::cout << "📷 捕获到帧: " << frame.width << "x" << frame.height 
                      << " 大小: " << frame.size << " bytes" << std::endl;
        });
        
        if (capture.start()) {
            std::cout << "✅ 捕获启动成功" << std::endl;
            
            // 捕获5秒
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            capture.stop();
            std::cout << "✅ 捕获停止" << std::endl;
        } else {
            std::cout << "❌ 捕获启动失败" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "❌ 测试失败: " << e.what() << std::endl;
    }
}

void test_region_quality_modes() {
    std::cout << "\n=== 测试不同质量模式 ===" << std::endl;
    
    int quality_modes[] = {0, 1, 2};  // 快速、均衡、高质量
    
    for (int quality : quality_modes) {
        std::cout << "测试质量模式: " << quality << std::endl;
        
        CaptureConfig config;
        config.capture_region = true;
        config.capture_rect = {100, 100, 500, 400};
        config.region_quality = quality;
        config.frame_rate = 30;
        
        try {
            DXGICapture capture(config);
            std::cout << "✅ 质量模式 " << quality << " 初始化成功" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "❌ 质量模式 " << quality << " 初始化失败: " << e.what() << std::endl;
        }
    }
}

std::unique_ptr<ScreenRecorder> recorder;

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", stopping recorder..." << std::endl;
    if (recorder) {
        recorder->stop();
    }
    exit(0);
}

int64_t main_test(){
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    try {
        RecordConfig config;
        
        // 捕获配置
        config.capture_config.capture_full_screen = true;
        config.capture_config.capture_region = false;
        //config.capture_config.capture_rect = {0, 0, 1920, 1080};  // 录制区域
        //config.capture_config.region_width = 1920;  // 区域宽度
        //config.capture_config.region_height = 1080;  // 区域高度

        config.capture_config.maintain_aspect_ratio = true;
        
        // 编码配置
        config.encoder_config.width = 2560;
        config.encoder_config.height = 1600;
        config.encoder_config.frame_rate = 60;
        config.encoder_config.video_bitrate = 8000000;  // 1Mbps
        config.encoder_config.video_codec_name = "libx264";
        config.encoder_config.preset = "medium";
        config.encoder_config.tune = "zerolatency";
        config.encoder_config.max_b_frames = 0;  // FLV 通常不支持 B-frames
        config.encoder_config.gop_size = 60;  // 
        config.encoder_config.pixel_format = AV_PIX_FMT_YUV420P;  // FLV 标准格式
        config.encoder_config.audio_bitrate = 0; 
        config.encoder_config.sample_rate = 0;
        config.encoder_config.channels = 0;
        config.encoder_config.channel_layout = AV_CHANNEL_LAYOUT_STEREO;
        config.encoder_config.sample_format = AV_SAMPLE_FMT_FLTP;
        config.encoder_config.audio_codec_name = "aac";
        
        // 输出配置
        config.record_to_file = true;
        config.output_directory="recording";
        config.output_filename = "screen_record.mp4";
        
        config.stream_to_rtmp = true;
        config.rtmp_url = "rtmp://127.0.0.1/live/livestream";
        
        recorder = std::make_unique<ScreenRecorder>();
        
        if (recorder->initialize(config)) {
            std::cout << "Press 's' to start, 'q' to stop, 'x' to exit" << std::endl;
            
            bool started = false;
            
            while (true) {
                if (_kbhit()) {
                    char ch = _getch();
                    
                    switch (ch) {
                        case 's':
                        case 'S':
                            if (!started && recorder->start()) {
                                started = true;
                                std::cout << "Recording and streaming started!" << std::endl;
                            }
                            break;
                            
                        case 'q':
                        case 'Q':
                            if (started) {
                                recorder->stop();
                                started = false;
                                std::cout << "Recording and streaming stopped" << std::endl;
                            }
                            break;
                            
                        case 'x':
                        case 'X':
                            std::cout << "Exiting..." << std::endl;
                            if (started) {
                                recorder->stop();
                            }
                            return 0;
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } else {
            std::cerr << "Failed to initialize screen recorder" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
int main() {
    main_test();
}