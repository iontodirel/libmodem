// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// io.h
//
// MIT License
//
// Copyright (c) 2025 Ion Todirel
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <optional>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#ifndef LIBMODEM_NAMESPACE
#define LIBMODEM_NAMESPACE libmodem
#endif
#ifndef LIBMODEM_NAMESPACE_BEGIN
#define LIBMODEM_NAMESPACE_BEGIN namespace LIBMODEM_NAMESPACE {
#endif
#ifndef LIBMODEM_NAMESPACE_REFERENCE
#define LIBMODEM_NAMESPACE_REFERENCE libmodem :: 
#endif
#ifndef LIBMODEM_NAMESPACE_END
#define LIBMODEM_NAMESPACE_END }
#endif

LIBMODEM_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// serial_port_base                                                 //
//                                                                  //
//                                                                  //
// **************************************************************** //

class serial_port_base
{
public:
    virtual void rts(bool enable) = 0;
    virtual bool rts() = 0;
    virtual void dtr(bool enable) = 0;
    virtual bool dtr() = 0;
    virtual bool cts() = 0;
    virtual bool dsr() = 0;
    virtual bool dcd() = 0;

    virtual std::size_t write(const std::vector<uint8_t>& data) = 0;
    virtual std::size_t write(const std::string& data) = 0;
    virtual std::vector<uint8_t> read(std::size_t size) = 0;
    virtual std::vector<uint8_t> read_some(std::size_t max_size) = 0;
    virtual std::string read_until(const std::string& delimiter) = 0;

    virtual bool is_open() = 0;
    virtual std::size_t bytes_available() = 0;
    virtual void flush() = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// serial_port                                                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

enum class parity
{
    none,
    odd,
    even
};

enum class stop_bits
{
    one,
    onepointfive,
    two
};

enum class flow_control
{
    none,
    software,
    hardware
};

struct serial_port_impl;

class serial_port : public serial_port_base
{
public:
    serial_port();
    serial_port(const serial_port&) = delete;
    serial_port& operator=(const serial_port&) = delete;
    serial_port(serial_port&&) noexcept;
    serial_port& operator=(serial_port&&) noexcept;
    virtual ~serial_port();

    bool open(const std::string& port_name,
        unsigned int baud_rate = 9600,
        unsigned int data_bits = 8,
        parity parity = parity::none,
        stop_bits stop_bits = stop_bits::one,
        flow_control flow_control = flow_control::none);

    void close();

    void rts(bool enable) override;
    bool rts() override;
    void dtr(bool enable) override;
    bool dtr() override;
    bool cts() override;
    bool dsr() override;
    bool dcd() override;

    std::size_t write(const std::vector<uint8_t>& data) override;
    std::size_t write(const std::string& data) override;
    std::vector<uint8_t> read(std::size_t size) override;
    std::vector<uint8_t> read_some(std::size_t max_size) override;
    std::string read_until(const std::string& delimiter) override;
    bool is_open() override;
    std::size_t bytes_available() override;
    void flush() override;
    void timeout(unsigned int milliseconds);

private:
    std::unique_ptr<serial_port_impl> impl_;
    bool is_open_;
#ifdef WIN32
    bool rts_ = false;
    bool dtr_ = false;
#endif // WIN32
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_serial_port_client                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_serial_port_client_impl;

class tcp_serial_port_client : public serial_port_base
{
public:
    tcp_serial_port_client();
    tcp_serial_port_client& operator=(const tcp_serial_port_client&) = delete;
    tcp_serial_port_client(const tcp_serial_port_client&) = delete;
    tcp_serial_port_client& operator=(tcp_serial_port_client&&) noexcept;
    tcp_serial_port_client(tcp_serial_port_client&&) noexcept;
    ~tcp_serial_port_client();

    bool connect(const std::string& host, int port);
    void disconnect();

    bool connected() const;

    void rts(bool enable) override;
    bool rts() override;
    void dtr(bool enable) override;
    bool dtr() override;
    bool cts() override;
    bool dsr() override;
    bool dcd() override;

    std::size_t write(const std::vector<uint8_t>& data) override;
    std::size_t write(const std::string& data) override;
    std::vector<uint8_t> read(std::size_t size) override;
    std::vector<uint8_t> read_some(std::size_t max_size) override;
    std::string read_until(const std::string& delimiter) override;
    bool is_open() override;
    std::size_t bytes_available() override;
    void flush() override;

private:
    std::unique_ptr<tcp_serial_port_client_impl> impl_;
    std::optional<std::reference_wrapper<serial_port_base>> serial_port_;
    bool connected_ = false;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_serial_port_server                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_serial_port_server_impl;

class tcp_serial_port_server
{
public:
    tcp_serial_port_server();
    tcp_serial_port_server(serial_port_base&);
    tcp_serial_port_server& operator=(const tcp_serial_port_server&) = delete;
    tcp_serial_port_server(const tcp_serial_port_server&) = delete;
    tcp_serial_port_server& operator=(tcp_serial_port_server&&) noexcept;
    tcp_serial_port_server(tcp_serial_port_server&&) noexcept;
    ~tcp_serial_port_server();

    bool start(const std::string& host, int port);
    void stop();

private:
    void run();
    void run_internal();
    std::string handle_request(const std::string& data);

    std::optional<std::reference_wrapper<serial_port_base>> serial_port_;
    std::unique_ptr<tcp_serial_port_server_impl> impl_;
    std::jthread thread_;
    std::atomic<bool> running_ = false;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool ready_ = false;
};

LIBMODEM_NAMESPACE_END