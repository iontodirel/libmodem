// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// data_source.h
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

#include "io.h"
#include "bitstream.h"
#include "kiss.h"
#include "modem.h"

#include <string>
#include <memory>
#include <queue>
#include <atomic>
#include <chrono>

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
// transport                                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct transport
{
public:
    virtual ~transport() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void write(const std::vector<uint8_t>& data) = 0;
    virtual size_t read(std::size_t client_id, std::vector<uint8_t>& data, size_t size) = 0;

    virtual std::vector<std::size_t> clients() = 0;

    virtual void flush() = 0;

    template<typename Rep, typename Period>
    bool wait_data_received(const std::chrono::duration<Rep, Period>& timeout_duration);

    virtual bool wait_data_received(int timeout_ms = -1) = 0;
};

template<typename Rep, typename Period>
LIBMODEM_INLINE bool transport::wait_data_received(const std::chrono::duration<Rep, Period>& timeout_duration)
{
    if (timeout_duration.count() < 0)
    {
        return wait_data_received(-1);
    }
    else
    {
        auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout_duration).count();
        return wait_data_received(static_cast<int>(timeout_ms));
    }
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_transport                                                    //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_transport : public transport, private tcp_server_base
{
public:
    tcp_transport() {}
    tcp_transport(const std::string& hostname, int port) : hostname_(hostname), port_(port)
    {
    }
    ~tcp_transport()
    {
        stop();
    }

    void start() override;
    void stop() override;

    void write(const std::vector<uint8_t>& data) override;
    size_t read(std::size_t client_id, std::vector<uint8_t>& data, size_t size) override;
    std::vector<std::size_t> clients() override;
    void flush() override;

    bool wait_data_received(int timeout_ms = -1) override;

private:
    void on_data_received(const tcp_client_connection& connection, const std::vector<uint8_t>& data) override;
    void on_client_disconnected(const tcp_client_connection& connection) override;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<std::size_t, std::vector<uint8_t>> client_buffers_;
    std::vector<std::size_t> client_ids_;
    std::string hostname_;
    int port_ = 0;
};


// **************************************************************** //
//                                                                  //
//                                                                  //
// serial_transport                                                 //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct serial_transport : public transport
{
public:
    void start();
    void stop();

    void write(const std::vector<uint8_t>& data) override;
    size_t read(std::size_t client_id, std::vector<uint8_t>& data, size_t size) override;

    std::vector<std::size_t> clients() override;

    void flush() override;

    bool wait_data_received(int timeout_ms = -1) override;

private:
    serial_port port_;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// formatter                                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct formatter
{
    formatter();
    formatter(const formatter& other);
    virtual ~formatter();

    virtual std::unique_ptr<formatter> clone() const = 0;
    virtual std::vector<uint8_t> encode(packet p) = 0;
    virtual bool try_decode(const std::vector<uint8_t>& data, size_t count, packet& p) = 0;

    template<typename Func, typename... Args>
        requires std::invocable<std::decay_t<Func>, const kiss::frame&, std::decay_t<Args>...>

    void add_on_command(Func&& f, Args&&... args);

protected:
    struct command_callable_base
    {
        virtual void invoke(const kiss::frame& frame) = 0;
        virtual std::unique_ptr<command_callable_base> clone() const = 0;
        virtual ~command_callable_base() = default;
    };

    template<typename Func, typename... Args>
    struct command_callable : public command_callable_base
    {
        template<typename F, typename... A>
        command_callable(F&& f, A&&... a);

        void invoke(const kiss::frame& frame) override;
        std::unique_ptr<command_callable_base> clone() const override;

    private:
        Func func_;
        std::tuple<Args...> args_;
    };

    void invoke_on_command(const kiss::frame& frame);

    std::unique_ptr<command_callable_base> on_command_callable_;
};

template<typename Func, typename... Args>
    requires std::invocable<std::decay_t<Func>, const kiss::frame&, std::decay_t<Args>...>
LIBMODEM_INLINE void formatter::add_on_command(Func&& f, Args&&... args)
{
    if constexpr (std::is_lvalue_reference_v<Func>)
    {
        on_command_callable_ = std::make_unique<command_callable<std::decay_t<Func>*, std::decay_t<Args>...>>(&f, std::forward<Args>(args)...);
    }
    else
    {
        on_command_callable_ = std::make_unique<command_callable<std::decay_t<Func>, std::decay_t<Args>...>>(std::forward<Func>(f), std::forward<Args>(args)...);
    }
}

template<typename Func, typename... Args>
template<typename F, typename... A>
LIBMODEM_INLINE formatter::command_callable<Func, Args...>::command_callable(F&& f, A&&... a) : func_(std::forward<F>(f)), args_(std::forward<A>(a)...)
{
}

template<typename Func, typename... Args>
LIBMODEM_INLINE void formatter::command_callable<Func, Args...>::invoke(const kiss::frame& frame)
{
    if constexpr (std::is_pointer_v<Func>)
    {
        std::apply(*func_, std::tuple_cat(std::make_tuple(std::cref(frame)), args_));
    }
    else
    {
        std::apply(func_, std::tuple_cat(std::make_tuple(std::cref(frame)), args_));
    }
}

template<typename Func, typename... Args>
LIBMODEM_INLINE std::unique_ptr<formatter::command_callable_base> formatter::command_callable<Func, Args...>::clone() const
{
    return std::make_unique<command_callable<Func, Args...>>(func_, std::make_from_tuple<std::tuple<Args...>>(args_));
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// ax25_kiss_formatter                                              //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct ax25_kiss_formatter : public formatter
{
public:
    std::unique_ptr<formatter> clone() const override;
    std::vector<uint8_t> encode(packet p) override;
    bool try_decode(const std::vector<uint8_t>& data, size_t count, packet& p) override;
    bool try_decode(const std::vector<uint8_t>& data, size_t count, kiss::frame& f);

private:
    kiss::decoder kiss_decoder_;
    std::queue<kiss::frame> pending_frames_;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// data_source                                                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

class data_source
{
public:
    data_source();
    virtual ~data_source();

    void transport(struct transport& t);
    void formatter(struct formatter& f);

    virtual void start();
    virtual void stop();

    void send(packet p);
    bool try_receive(packet& p);

    bool wait_data_received(int timeout_ms = -1);

    virtual bool wait_stopped(int timeout_ms = -1);

    std::string name;

private:
    std::optional<std::reference_wrapper<struct transport>> transport_;
    std::optional<std::reference_wrapper<struct formatter>> formatter_;
    std::unordered_map<std::size_t, std::unique_ptr<struct formatter>> client_formatters_;
    std::vector<uint8_t> read_buffer_;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// modem_data_source                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

class modem_data_source : public data_source
{
public:
    virtual ~modem_data_source();

    void modem(struct modem& m);

    virtual void start() override;
    virtual void stop() override;

    virtual bool wait_stopped(int timeout_ms = -1) override;

private:
    void receive_callback(std::stop_token stop_token);

    std::optional<std::reference_wrapper<struct modem>> m_;
    std::jthread receive_thread_;
    std::atomic<bool> running_{ false };
    std::condition_variable_any cv_;
    std::mutex stop_mutex_;
    std::condition_variable stop_cv_;
};

LIBMODEM_NAMESPACE_END
