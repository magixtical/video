#include "HttpServer.h"
#include "utils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

using boost::asio::ip::tcp;

HttpServer::HttpServer(const Config& config)
    : config_(config)
    , acceptor_(io_context_, tcp::endpoint(tcp::v4(), config_.HTTP_PORT)) {
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    if (running_) return;

    running_ = true;
    server_thread_ = std::thread([this]() {
        std::cout << "Starting HTTP server on port " << config_.HTTP_PORT << std::endl;

        try {
            start_accept();
            io_context_.run();
            std::cout << "HTTP server started successfully!" << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "HTTP server runtime error: " << e.what() << std::endl;
            running_ = false;
        }
        });
}

void HttpServer::stop() {
    if (!running_) return;

    running_ = false;
    io_context_.stop();
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    std::cout << "HTTP server stopped" << std::endl;
}

void HttpServer::start_accept() {
    auto socket = std::make_shared<tcp::socket>(io_context_);

    acceptor_.async_accept(*socket,
        [this, socket](const boost::system::error_code& error) {
            if (!error) {
                // 使用带生命周期的请求处理
                handle_request(socket);
            }
            else {
                if (error != boost::asio::error::operation_aborted) {
                    std::cerr << "Accept error: " << error.message() << std::endl;
                }
            }

            if (running_) {
                start_accept();
            }
        });
}

void HttpServer::handle_request(std::shared_ptr<tcp::socket> socket) {
    auto request_data = std::make_shared<struct RequestData>();
    request_data->socket = socket;

    // 异步读取请求
    boost::asio::async_read_until(*socket, request_data->buffer, "\r\n\r\n",
        [this, request_data](const boost::system::error_code& error, std::size_t bytes_transferred) {
            if (error) {
                if (error == boost::asio::error::eof ||
                    error == boost::asio::error::connection_reset) {
                    // 正常断开连接
                    return;
                }
                std::cerr << "Read error: " << error.message() << std::endl;
                return;
            }

            // 处理请求
            process_request(request_data);
        });
}

void HttpServer::process_request(std::shared_ptr<RequestData> request_data) {
    try {
        std::istream stream(&request_data->buffer);
        std::string request_line;
        std::getline(stream, request_line);

        // 解析请求行
        std::istringstream iss(request_line);
        std::string method, path, version;
        iss >> method >> path >> version;

        std::cout << "Request: " << method << " " << path << std::endl;

        if (method == "GET") {
            // 确保path字符串的生命周期
            request_data->path = path;
            process_get_request(request_data);
        }
        else {
            send_response(request_data->socket, "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n");
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Request processing error: " << e.what() << std::endl;
    }
}

void HttpServer::process_get_request(std::shared_ptr<RequestData> request_data) {
    std::string clean_path = request_data->path;

    // 清理路径
    size_t query_pos = clean_path.find('?');
    if (query_pos != std::string::npos) {
        clean_path = clean_path.substr(0, query_pos);
    }

    // 默认路径
    if (clean_path == "/" || clean_path == "/index.html") {
        clean_path = "/" + config_.M3U8_FILENAME;
    }

    // 构建文件路径 - 确保字符串生命周期
    request_data->file_path = config_.HLS_DIR + clean_path;

    // 检查文件是否存在
    std::error_code ec;
    if (!std::filesystem::exists(request_data->file_path, ec)) {
        std::cout << "File not found: " << request_data->file_path << std::endl;
        send_response(request_data->socket, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
        return;
    }

    // 获取文件大小
    request_data->file_size = std::filesystem::file_size(request_data->file_path, ec);
    if (ec) {
        std::cerr << "Failed to get file size: " << ec.message() << std::endl;
        send_response(request_data->socket, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
        return;
    }

    // 发送文件
    send_file(request_data);
}

void HttpServer::send_file(std::shared_ptr<RequestData> request_data) {
    // 打开文件
    request_data->file_stream = std::make_shared<std::ifstream>(
        request_data->file_path, std::ios::binary);

    if (!request_data->file_stream->is_open()) {
        std::cerr << "Failed to open file: " << request_data->file_path << std::endl;
        send_response(request_data->socket, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
        return;
    }

    // 构建HTTP头 - 确保字符串生命周期
    std::string content_type = get_content_type(request_data->file_path);
    std::ostringstream header;
    header << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << request_data->file_size << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Cache-Control: no-cache\r\n"
        << "Connection: close\r\n\r\n";

    request_data->header = header.str();

    // 发送HTTP头
    boost::asio::async_write(*request_data->socket,
        boost::asio::buffer(request_data->header),
        [this, request_data](const boost::system::error_code& error, std::size_t /*bytes_transferred*/) {
            if (!error) {
                std::cout << "Sending file: " << request_data->file_path
                    << " (" << request_data->file_size << " bytes)" << std::endl;
                // 发送文件内容
                send_file_content(request_data, 0);
            }
            else {
                std::cerr << "Write header error: " << error.message() << std::endl;
            }
        });
}

void HttpServer::send_file_content(std::shared_ptr<RequestData> request_data, size_t bytes_sent) {
    if (bytes_sent >= request_data->file_size) {
        std::cout << "File sent completely: " << request_data->file_path << std::endl;
        return;
    }

    // 读取文件块
    auto buffer = std::make_shared<std::vector<char>>(8192);
    request_data->file_stream->read(buffer->data(), buffer->size());
    std::streamsize bytes_read = request_data->file_stream->gcount();

    if (bytes_read > 0) {
        // 发送文件块
        boost::asio::async_write(*request_data->socket,
            boost::asio::buffer(buffer->data(), bytes_read),
            [this, request_data, bytes_sent, bytes_read, buffer]
            (const boost::system::error_code& error, std::size_t /*bytes_transferred*/) {
                if (!error) {
                    // 递归发送下一块
                    send_file_content(request_data, bytes_sent + bytes_read);
                }
                else {
                    std::cerr << "Write file content error: " << error.message() << std::endl;
                }
            });
    }
    else {
        // 文件读取完成
        std::cout << "File send completed: " << request_data->file_path << std::endl;
    }
}

void HttpServer::send_response(std::shared_ptr<tcp::socket> socket, const std::string& response) {
    // 确保响应字符串的生命周期
    auto response_copy = std::make_shared<std::string>(response);

    boost::asio::async_write(*socket, boost::asio::buffer(*response_copy),
        [socket, response_copy](const boost::system::error_code& error, std::size_t /*bytes_transferred*/) {
            if (error) {
                std::cerr << "Write response error: " << error.message() << std::endl;
            }
        });
}

std::string HttpServer::get_content_type(const std::string& path) {
    if (path.ends_with(".m3u8")) {
        return "application/vnd.apple.mpegurl";
    }
    else if (path.ends_with(".ts")) {
        return "video/mp2t";
    }
    else if (path.ends_with(".mp4")) {
        return "video/mp4";
    }
    else {
        return "application/octet-stream";
    }
}