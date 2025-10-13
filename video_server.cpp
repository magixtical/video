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