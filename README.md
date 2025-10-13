# HLS Streaming Server

一个基于FFmpeg和Boost.Asio的HLS流媒体服务器，可将本地视频文件转换为HLS流并通过HTTP提供访问。

## 功能说明
- 自动将视频文件转码为HLS格式（.m3u8索引文件+*.ts切片）
- 支持视频/音频编码检测与选择性转码
- 内置HTTP服务器提供HLS流访问
- 支持配置切片时长、码率、端口等参数

## 依赖项
- FFmpeg（libavcodec、libavformat等）
- Boost.Asio（用于HTTP服务器）
- C++17及以上编译器

## 项目结构

| 文件/目录         | 说明                                                                 |
|-------------------|----------------------------------------------------------------------|
| `config.h`/.cpp   | 项目配置定义（视频路径、HLS参数、转码编码格式等）                     |
| `hls_generator.h`/.cpp | HLS流生成核心逻辑（视频转码、切片处理、完整性检查等）                 |
| `HttpServer.h`    | HTTP服务器实现（基于Boost.Asio，提供HLS文件的HTTP访问）              |
| `ffmpeg_utils.h`/.cpp | FFmpeg工具类封装（编解码器上下文、像素格式转换、音频重采样等）         |
| `utils.h`/.cpp    | 通用工具函数（FFmpeg错误处理等）                                      |
| `video_server.cpp` | 程序入口（信号处理、初始化配置、启动HLS生成器和HTTP服务器）           |
| `resource.h`      | 资源定义文件（用于编译系统，如Windows资源）                           |

## 配置参数
主要可配置项（在`config.h`中定义）：
- `VIDEO_PATH`：输入视频文件路径（默认：local_video.mp4）
- `HLS_DIR`：HLS输出目录（默认：hls_stream）
- `HTTP_PORT`：HTTP服务端口（默认：8080）
- `HLS_SEGMENT_DURATION`：切片时长（秒，默认：10）
- 转码相关：视频/音频码率、支持的转码编码格式等

## 使用方法
1. 配置`config.h`中的视频路径和参数
2. 编译项目（需链接FFmpeg和Boost库）
3. 运行可执行文件，访问 `http://localhost:8080/stream.m3u8` 查看流

## 转码规则
- 视频：H.264（YUV420P）无需转码，其他编码（HEVC、VP9等）自动转码为H.264
- 音频：AAC无需转码，其他编码（AC3、DTS等）自动转码为AAC