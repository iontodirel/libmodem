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
#include <unordered_set>

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

#ifndef LIBMODEM_INLINE
#define LIBMODEM_INLINE inline
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
    virtual ~serial_port_base() = default;

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
// tcp_client_connection                                            //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_client_connection
{
    std::string remote_address;
    int remote_port = 0;
    std::size_t id = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_server_base                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_server_base_impl;
struct tcp_client_connection_impl;

class tcp_server_base
{
public:
    tcp_server_base();
    tcp_server_base& operator=(const tcp_server_base&) = delete;
    tcp_server_base(const tcp_server_base&) = delete;
    tcp_server_base& operator=(tcp_server_base&&) noexcept;
    tcp_server_base(tcp_server_base&&) noexcept;
    virtual ~tcp_server_base();

    virtual bool start(const std::string& host, int port);
    virtual void stop();

    void thread_count(std::size_t size);
    std::size_t thread_count() const;

    void no_delay(bool enable);
    bool no_delay() const;
    void keep_alive(bool enable);
    bool keep_alive() const;
#ifdef __linux__
    void keep_alive_idle(int seconds);
    int keep_alive_idle() const;
    void keep_alive_interval(int seconds);
    int keep_alive_interval() const;
    void keep_alive_count(int count);
    int keep_alive_count() const;
#endif
    void linger(bool enable);
    bool linger() const;
    void linger_time(int seconds);
    int linger_time() const;

    bool running() const;

    void flush();

    bool faulted();
    void throw_if_faulted();

protected:
    virtual void on_data_received(const tcp_client_connection& connection, const std::vector<uint8_t>& data) = 0;
    void broadcast(const std::vector<uint8_t>& data);
    void send(const tcp_client_connection& connection, std::vector<uint8_t> data);
    virtual void on_client_disconnected(const tcp_client_connection& connection) = 0;

private:
    void run();
    void accept_async();
    void read_async(std::shared_ptr<tcp_client_connection_impl> connection);
    void write_async(std::shared_ptr<tcp_client_connection_impl> connection, std::vector<uint8_t> response);
    void on_data_received(std::shared_ptr<tcp_client_connection_impl> connection, const std::vector<uint8_t>& data);
    void on_client_disconnected(std::shared_ptr<tcp_client_connection_impl> connection);

    std::unique_ptr<tcp_server_base_impl> impl_;
    std::vector<std::jthread> threads_;
    std::size_t thread_count_ = 1;
    std::atomic<bool> running_ = false;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool ready_ = false;
    std::exception_ptr exception_;
    std::mutex connections_mutex_;
    std::unordered_map<std::size_t, std::shared_ptr<tcp_client_connection_impl>> connections_;
    std::size_t next_connection_id_ = 0;
    bool no_delay_ = true;
    bool keep_alive_ = true;
#ifdef __linux__
    int keep_alive_idle_ = 30;
    int keep_alive_interval_ = 10;
    int keep_alive_count_ = 5;
#endif
    bool linger_ = false;
    int linger_time_ = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_serial_port_server                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

class tcp_serial_port_server : public tcp_server_base
{
public:
    tcp_serial_port_server();
    tcp_serial_port_server(serial_port_base&);
    tcp_serial_port_server& operator=(const tcp_serial_port_server&) = delete;
    tcp_serial_port_server(const tcp_serial_port_server&) = delete;
    tcp_serial_port_server& operator=(tcp_serial_port_server&&) noexcept;
    tcp_serial_port_server(tcp_serial_port_server&&) noexcept;
    virtual ~tcp_serial_port_server();

    virtual bool start(const std::string& host, int port) override;

protected:
    void on_data_received(const tcp_client_connection& connection, const std::vector<uint8_t>& data) override;
    void on_client_disconnected(const tcp_client_connection& connection) override;

private:
    std::vector<uint8_t> handle_request(const std::vector<uint8_t>& data);

    std::optional<std::reference_wrapper<serial_port_base>> serial_port_;
    std::mutex serial_port_mutex_;
    std::mutex buffers_mutex_;
    std::unordered_map<std::size_t, std::vector<uint8_t>> buffers_;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// ptt_control_library                                              //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct ptt_control_library_impl;

class ptt_control_library
{
public:
    ptt_control_library();
    ptt_control_library(const ptt_control_library&) = delete;
    ptt_control_library& operator=(const ptt_control_library&) = delete;
    ptt_control_library(ptt_control_library&&) noexcept;
    ptt_control_library& operator=(ptt_control_library&&) noexcept;
    ~ptt_control_library();

    void load(const std::string& library_path);
    void load(const std::string& library_path, void* context);
    void unload();

    void uninit();

    void ptt(bool enable);
    bool ptt();

    explicit operator bool() const;

private:
    typedef int (*set_ptt_fptr)(int);
    typedef int (*get_ptt_fptr)(int*);
    typedef int (*init_fptr)(void*);
    typedef int (*uninit_fptr)();

    std::unique_ptr<ptt_control_library_impl> pimpl_;
    set_ptt_fptr set_ptt_fptr_ = nullptr;
    get_ptt_fptr get_ptt_fptr_ = nullptr;
    init_fptr init_fptr_ = nullptr;
    uninit_fptr uninit_fptr_ = nullptr;
    bool loaded_ = false;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_ptt_control_client                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_ptt_control_client_impl;

class tcp_ptt_control_client
{
public:
    tcp_ptt_control_client();
    tcp_ptt_control_client& operator=(const tcp_ptt_control_client&) = delete;
    tcp_ptt_control_client(const tcp_ptt_control_client&) = delete;
    tcp_ptt_control_client& operator=(tcp_ptt_control_client&&) noexcept;
    tcp_ptt_control_client(tcp_ptt_control_client&&) noexcept;
    ~tcp_ptt_control_client();

    bool connect(const std::string& host, int port);
    void disconnect();

    bool connected() const;

    void ptt(bool ptt_state);
    bool ptt();

private:
    std::unique_ptr<tcp_ptt_control_client_impl> impl_;
    bool connected_ = false;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_ptt_control_server                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

class tcp_ptt_control_server : public tcp_server_base
{
public:
    tcp_ptt_control_server();

    template<typename Func, typename ... Args>
        requires std::invocable<std::decay_t<Func>, bool, std::decay_t<Args>...>
    tcp_ptt_control_server(Func&& f, Args&& ... args);

    tcp_ptt_control_server& operator=(const tcp_ptt_control_server&) = delete;
    tcp_ptt_control_server(const tcp_ptt_control_server&) = delete;
    tcp_ptt_control_server& operator=(tcp_ptt_control_server&&) noexcept;
    tcp_ptt_control_server(tcp_ptt_control_server&&) noexcept;
    ~tcp_ptt_control_server();

    virtual bool start(const std::string& host, int port) override;

protected:
    void on_data_received(const tcp_client_connection& connection, const std::vector<uint8_t>& data) override;
    void on_client_disconnected(const tcp_client_connection& connection) override;

private:
    std::vector<uint8_t> handle_request(const std::vector<uint8_t>& data);

    struct ptt_callable_base
    {
        virtual void invoke(bool ptt_state) = 0;

        virtual ~ptt_callable_base() = default;
    };

    template<typename Func, typename ... Args>
    struct ptt_callable : public ptt_callable_base
    {
        template<typename F, typename... A>
        ptt_callable(F&& f, A&&... a);

        void invoke(bool ptt_state) override;

    private:
        Func func_;
        std::tuple<Args...> args_;
    };

    std::unique_ptr<ptt_callable_base> ptt_callable_;
    std::mutex buffers_mutex_;
    std::unordered_map<std::size_t, std::vector<uint8_t>> buffers_;
};

template<typename Func, typename ... Args>
    requires std::invocable<std::decay_t<Func>, bool, std::decay_t<Args>...>
LIBMODEM_INLINE tcp_ptt_control_server::tcp_ptt_control_server(Func&& f, Args&& ... args) : tcp_ptt_control_server()
{
    if constexpr (std::is_lvalue_reference_v<Func>)
    {
        ptt_callable_ = std::make_unique<ptt_callable<std::decay_t<Func>*, std::decay_t<Args>...>>(&f, std::forward<Args>(args)...);
    }
    else
    {
        ptt_callable_ = std::make_unique<ptt_callable<std::decay_t<Func>, std::decay_t<Args>...>>(std::forward<Func>(f), std::forward<Args>(args)...);
    }
}

template<typename Func, typename ... Args>
template<typename F, typename... A>
LIBMODEM_INLINE tcp_ptt_control_server::ptt_callable<Func, Args...>::ptt_callable(F&& f, A&&... a) : func_(std::forward<F>(f)), args_(std::forward<A>(a)...)
{
}

template<typename Func, typename ... Args>
LIBMODEM_INLINE void tcp_ptt_control_server::ptt_callable<Func, Args...>::invoke(bool ptt_state)
{
    if constexpr (std::is_pointer_v<Func>)
    {
        std::apply(*func_, std::tuple_cat(std::make_tuple(ptt_state), args_));
    }
    else
    {
        std::apply(func_, std::tuple_cat(std::make_tuple(ptt_state), args_));
    }
}

LIBMODEM_NAMESPACE_END