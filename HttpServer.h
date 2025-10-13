#pragma once
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "config.h"
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <fstream>
#include <boost/asio.hpp>

struct RequestData {
    std::shared_ptr<boost::asio::ip::tcp::socket> socket;
    boost::asio::streambuf buffer;
    std::string path;
    std::string file_path;
    std::string header;
    size_t file_size = 0;
    std::shared_ptr<std::ifstream> file_stream;
};

class HttpServer {
private:
    const Config& config_;
    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::atomic<bool> running_{ false };
    std::thread server_thread_;

    void start_accept();
    void handle_request(std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void process_request(std::shared_ptr<RequestData> request_data);
    void process_get_request(std::shared_ptr<RequestData> request_data);
    void send_file(std::shared_ptr<RequestData> request_data);
    void send_file_content(std::shared_ptr<RequestData> request_data, size_t bytes_sent);
    void send_response(std::shared_ptr<boost::asio::ip::tcp::socket> socket, const std::string& response);
    std::string get_content_type(const std::string& path);

public:
    HttpServer(const Config& config);
    ~HttpServer();

    void start();
    void stop();
    bool is_running() const { return running_; }
};

#endif // HTTP_SERVER_H