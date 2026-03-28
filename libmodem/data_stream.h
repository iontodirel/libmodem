// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// data_stream.h
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
#include "modem.h"
#include "formatter.h"

#include <string>
#include <memory>
#include <queue>
#include <atomic>
#include <chrono>
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
    virtual void write(std::size_t client_id, const std::vector<uint8_t>& data);
    virtual size_t read(std::size_t client_id, std::vector<uint8_t>& data, size_t size) = 0;

    virtual std::vector<std::size_t> clients() = 0;

    virtual void flush() = 0;

    template<typename Rep, typename Period>
    bool wait_data_received(const std::chrono::duration<Rep, Period>& timeout_duration);

    virtual bool wait_data_received(int timeout_ms = -1) = 0;

    virtual void enabled(bool enable) = 0;
    virtual bool enabled() = 0;
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
    tcp_transport();
    tcp_transport(const std::string& hostname, int port);
    ~tcp_transport();

    void start() override;
    void stop() override;

    void write(const std::vector<uint8_t>& data) override;
    void write(std::size_t client_id, const std::vector<uint8_t>& data) override;
    size_t read(std::size_t client_id, std::vector<uint8_t>& data, size_t size) override;
    std::vector<std::size_t> clients() override;
    void flush() override;

    bool wait_data_received(int timeout_ms = -1) override;

    template<typename Func, typename... Args>
        requires std::invocable<std::decay_t<Func>, const tcp_client_connection&, std::decay_t<Args>...>
    void add_on_client_connected(Func&& f, Args&&... args);

    template<typename Func, typename... Args>
        requires std::invocable<std::decay_t<Func>, const tcp_client_connection&, std::decay_t<Args>...>
    void add_on_client_disconnected(Func&& f, Args&&... args);

    void enabled(bool enable) override;
    bool enabled() override;

    tcp_server_base& server();

private:
    void on_data_received(const tcp_client_connection& connection, const std::vector<uint8_t>& data) override;
    void on_client_connected(const tcp_client_connection& connection) override;
    void on_client_disconnected(const tcp_client_connection& connection) override;

    struct client_callable_base
    {
        virtual void invoke(const tcp_client_connection& connection) = 0;
        virtual ~client_callable_base() = default;
    };

    template<typename Func, typename... Args>
    struct client_callable : public client_callable_base
    {
        Func func_;
        std::tuple<Args...> args_;

        template<typename F, typename... A>
        client_callable(F&& f, A&&... a) : func_(std::forward<F>(f)), args_(std::forward<A>(a)...)
        {
        }

        void invoke(const tcp_client_connection& connection) override
        {
            if constexpr (std::is_pointer_v<Func>)
            {
                std::apply(*func_, std::tuple_cat(std::make_tuple(std::cref(connection)), args_));
            }
            else
            {
                std::apply(func_, std::tuple_cat(std::make_tuple(std::cref(connection)), args_));
            }
        }
    };

    std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<std::size_t, std::vector<uint8_t>> client_buffers_;
    std::vector<std::size_t> client_ids_;
    std::string hostname_;
    int port_ = 0;
    std::unique_ptr<client_callable_base> on_client_connected_callable_;
    std::unique_ptr<client_callable_base> on_client_disconnected_callable_;
    std::atomic<bool> enabled_ = true;
};

template<typename Func, typename... Args>
    requires std::invocable<std::decay_t<Func>, const tcp_client_connection&, std::decay_t<Args>...>
LIBMODEM_INLINE void tcp_transport::add_on_client_connected(Func&& f, Args&&... args)
{
    if constexpr (std::is_lvalue_reference_v<Func>)
    {
        on_client_connected_callable_ = std::make_unique<client_callable<std::decay_t<Func>*, std::decay_t<Args>...>>(&f, std::forward<Args>(args)...);
    }
    else
    {
        on_client_connected_callable_ = std::make_unique<client_callable<std::decay_t<Func>, std::decay_t<Args>...>>(std::forward<Func>(f), std::forward<Args>(args)...);
    }
}

template<typename Func, typename... Args>
    requires std::invocable<std::decay_t<Func>, const tcp_client_connection&, std::decay_t<Args>...>
LIBMODEM_INLINE void tcp_transport::add_on_client_disconnected(Func&& f, Args&&... args)
{
    if constexpr (std::is_lvalue_reference_v<Func>)
    {
        on_client_disconnected_callable_ = std::make_unique<client_callable<std::decay_t<Func>*, std::decay_t<Args>...>>(&f, std::forward<Args>(args)...);
    }
    else
    {
        on_client_disconnected_callable_ = std::make_unique<client_callable<std::decay_t<Func>, std::decay_t<Args>...>>(std::forward<Func>(f), std::forward<Args>(args)...);
    }
}

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
    void start() override;
    void stop() override;

    void write(const std::vector<uint8_t>& data) override;
    size_t read(std::size_t client_id, std::vector<uint8_t>& data, size_t size) override;

    std::vector<std::size_t> clients() override;

    void flush() override;

    bool wait_data_received(int timeout_ms = -1) override;

    void enabled(bool enable) override;
    bool enabled() override;

private:
    serial_port port_;
    std::atomic<bool> enabled_ = true;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// data_stream                                                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct received_data
{
    uint8_t port = 0;
    packet p;
};

class data_stream
{
public:
    data_stream();
    virtual ~data_stream();

    void transport(struct transport& t);
    void formatter(struct formatter& f);

    virtual void start();
    virtual void stop();

    void send(packet p);
    bool try_receive(packet& p);
    bool try_receive(received_data& data);

    bool wait_data_received(int timeout_ms = -1);

    virtual bool wait_stopped(int timeout_ms = -1);

    virtual void enabled(bool enable);
    virtual bool enabled();

    std::string name;

private:
    std::optional<std::reference_wrapper<struct transport>> transport_;
    std::optional<std::reference_wrapper<struct formatter>> formatter_;
    std::unordered_map<std::size_t, std::unique_ptr<struct formatter>> client_formatters_;
    std::vector<uint8_t> read_buffer_;
    std::atomic<bool> enabled_ = true;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// modem_data_stream                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct modem_data_stream_impl;

class modem_data_stream : public data_stream, private modem_events
{
public:
    using data_stream::enabled;

    modem_data_stream();
    virtual ~modem_data_stream();

    void add_modem(struct modem& m);

    void modem(struct modem& m);

    virtual void start() override;
    virtual void stop() override;

    virtual void enabled(bool enable) override;

    size_t audio_stream_error_count(size_t count);
    size_t audio_stream_error_count();

    virtual bool wait_stopped(int timeout_ms = -1) override;

    bool wait_transmit_idle(int timeout_ms = -1);

public:

    template<typename Func, typename... Args>
        requires std::invocable<std::decay_t<Func>, const packet&, uint64_t, uint64_t, std::decay_t<Args>...>
    void add_on_packet_received(Func&& f, Args&&... args);

    template<typename Func, typename... Args>
        requires std::invocable<std::decay_t<Func>, struct modem&, const packet&, uint64_t, uint64_t, uint64_t, std::decay_t<Args>...>
    void add_on_transmit_started(Func&& f, Args&&... args);

    template<typename Func, typename... Args>
        requires std::invocable<std::decay_t<Func>, struct modem&, const packet&, uint64_t, uint64_t, uint64_t, std::decay_t<Args>...>
    void add_on_transmit_completed(Func&& f, Args&&... args);

    template<typename Func, typename... Args>
        requires std::invocable<std::decay_t<Func>, struct modem&, const packet&, uint64_t, uint64_t, uint64_t, std::decay_t<Args>...>
    void add_on_modem_transmit_packet(Func&& f, Args&&... args);

    template<typename Func, typename... Args>
        requires std::invocable<std::decay_t<Func>, struct modem&, const std::vector<uint8_t>&, uint64_t, uint64_t, uint64_t, std::decay_t<Args>...>
    void add_on_modem_transmit_bitstream(Func&& f, Args&&... args);

    template<typename Func, typename... Args>
        requires std::invocable<std::decay_t<Func>, struct modem&, bool, uint64_t, uint64_t, uint64_t, std::decay_t<Args>...>
    void add_on_modem_ptt(Func&& f, Args&&... args);

    template<typename Func, typename... Args>
        requires std::invocable<std::decay_t<Func>, struct modem&, uint64_t, uint64_t, uint64_t, std::decay_t<Args>...>
    void add_on_modem_before_start_render_audio(Func&& f, Args&&... args);

    template<typename Func, typename... Args>
        requires std::invocable<std::decay_t<Func>, struct modem&, const std::vector<double>&, size_t, uint64_t, uint64_t, uint64_t, std::decay_t<Args>...>
    void add_on_modem_end_render_audio(Func&& f, Args&&... args);

private:
    void receive_callback(std::stop_token stop_token);
    void transmit_callback(std::stop_token stop_token);

    void transmit(const packet& p, uint64_t id);
    void receive(const packet& p, uint64_t id) override;
    void transmit(const std::vector<uint8_t>& bitstream, uint64_t id) override;
    void receive(const std::vector<uint8_t>& bitstream, uint64_t id) override;
    void ptt(bool state, uint64_t id) override;
    void data_carrier_detected(uint64_t id) override;
    void before_start_render_audio(uint64_t id) override;
    void end_render_audio(const std::vector<double>& samples, size_t count, uint64_t id) override;
    void capture_audio(const std::vector<double>& samples, uint64_t id) override;
    void modulate(const std::vector<uint8_t>& bitstream, const std::vector<double>& audio_buffer, uint64_t id);

    template<typename... InvokeArgs>
    class event
    {
        struct callable_base
        {
            virtual void invoke(InvokeArgs... args) = 0;
            virtual ~callable_base() = default;
        };

        template<typename Func, typename... BoundArgs>
        struct callable_impl : callable_base
        {
            template<typename F, typename... A>
            callable_impl(F&& f, A&&... a) : func_(std::forward<F>(f)), args_(std::forward<A>(a)...)
            {
            }

            void invoke(InvokeArgs... invoke_args) override
            {
                if constexpr (std::is_pointer_v<Func>)
                {
                    std::apply(*func_, std::tuple_cat(std::forward_as_tuple(invoke_args...), args_));
                }
                else
                {
                    std::apply(func_, std::tuple_cat(std::forward_as_tuple(invoke_args...), args_));
                }
            }

            Func func_;
            std::tuple<BoundArgs...> args_;
        };

        std::unique_ptr<callable_base> callable_;

    public:
        explicit operator bool() const
        {
            return callable_ != nullptr;
        }

        void operator()(InvokeArgs... args)
        {
            if (callable_)
            {
                callable_->invoke(std::forward<InvokeArgs>(args)...);
            }
        }

        template<typename Func, typename... BoundArgs>
            requires std::invocable<std::decay_t<Func>, InvokeArgs..., std::decay_t<BoundArgs>...>
        void set(Func&& f, BoundArgs&&... args)
        {
            if constexpr (std::is_lvalue_reference_v<Func>)
            {
                callable_ = std::make_unique<callable_impl<std::decay_t<Func>*, std::decay_t<BoundArgs>...>>(&f, std::forward<BoundArgs>(args)...);
            }
            else
            {
                callable_ = std::make_unique<callable_impl<std::decay_t<Func>, std::decay_t<BoundArgs>...>>(std::forward<Func>(f), std::forward<BoundArgs>(args)...);

            }
        }

        void reset()
        {
            callable_.reset();
        }
    };

    std::vector<std::reference_wrapper<struct modem>> modems_;
    std::jthread receive_thread_;
    std::jthread transmit_thread_;
    std::atomic<bool> running_{ false };
    std::mutex enabled_mutex_;
    std::condition_variable_any enabled_cv_;
    std::mutex stop_mutex_;
    std::condition_variable stop_cv_;
    std::atomic<size_t> audio_stream_error_count_{ 0 };
    event<const packet&, uint64_t, uint64_t> on_packet_received_;
    event<struct modem&, const packet&, uint64_t, uint64_t, uint64_t> on_transmit_started_;
    event<struct modem&, const packet&, uint64_t, uint64_t, uint64_t> on_transmit_completed_;
    event<struct modem&, const packet&, uint64_t, uint64_t, uint64_t> on_modem_transmit_packet_;
    event<struct modem&, const std::vector<uint8_t>&, uint64_t, uint64_t, uint64_t> on_modem_transmit_bitstream_;
    event<struct modem&, bool, uint64_t, uint64_t, uint64_t> on_modem_ptt_;
    event<struct modem&, uint64_t, uint64_t, uint64_t> on_modem_before_start_render_audio_;
    event<struct modem&, const std::vector<double>&, size_t, uint64_t, uint64_t, uint64_t> on_modem_end_render_audio_;
    struct modem* current_modem_ = nullptr;
    std::atomic<bool> transmitting_{ false };
    std::mutex transmit_mutex_;
    std::condition_variable transmit_cv_;
    std::unique_ptr<modem_data_stream_impl> impl_;
    std::atomic<uint64_t> next_packet_id_{ 0 };
    std::atomic<uint64_t> next_receive_id_{ 0 };
    std::atomic<uint64_t> next_transmit_id_{ 0 };
    uint64_t current_packet_id_{ 0 };
    uint64_t current_receive_id_{ 0 };
    uint64_t current_transmit_id_{ 0 };
};

template<typename Func, typename... Args>
    requires std::invocable<std::decay_t<Func>, const packet&, uint64_t, uint64_t, std::decay_t<Args>...>
LIBMODEM_INLINE void modem_data_stream::add_on_packet_received(Func&& f, Args&&... args)
{
    on_packet_received_.set(std::forward<Func>(f), std::forward<Args>(args)...);
}

template<typename Func, typename... Args>
    requires std::invocable<std::decay_t<Func>, struct modem&, const packet&, uint64_t, uint64_t, uint64_t, std::decay_t<Args>...>
LIBMODEM_INLINE void modem_data_stream::add_on_transmit_started(Func&& f, Args&&... args)
{
    on_transmit_started_.set(std::forward<Func>(f), std::forward<Args>(args)...);
}

template<typename Func, typename... Args>
    requires std::invocable<std::decay_t<Func>, struct modem&, const packet&, uint64_t, uint64_t, uint64_t, std::decay_t<Args>...>
LIBMODEM_INLINE void modem_data_stream::add_on_transmit_completed(Func&& f, Args&&... args)
{
    on_transmit_completed_.set(std::forward<Func>(f), std::forward<Args>(args)...);
}

template<typename Func, typename... Args>
    requires std::invocable<std::decay_t<Func>, struct modem&, const packet&, uint64_t, uint64_t, uint64_t, std::decay_t<Args>...>
LIBMODEM_INLINE void modem_data_stream::add_on_modem_transmit_packet(Func&& f, Args&&... args)
{
    on_modem_transmit_packet_.set(std::forward<Func>(f), std::forward<Args>(args)...);
}

template<typename Func, typename... Args>
    requires std::invocable<std::decay_t<Func>, struct modem&, const std::vector<uint8_t>&, uint64_t, uint64_t, uint64_t, std::decay_t<Args>...>
LIBMODEM_INLINE void modem_data_stream::add_on_modem_transmit_bitstream(Func&& f, Args&&... args)
{
    on_modem_transmit_bitstream_.set(std::forward<Func>(f), std::forward<Args>(args)...);
}

template<typename Func, typename... Args>
    requires std::invocable<std::decay_t<Func>, struct modem&, bool, uint64_t, uint64_t, uint64_t, std::decay_t<Args>...>
LIBMODEM_INLINE void modem_data_stream::add_on_modem_ptt(Func&& f, Args&&... args)
{
    on_modem_ptt_.set(std::forward<Func>(f), std::forward<Args>(args)...);
}

template<typename Func, typename... Args>
    requires std::invocable<std::decay_t<Func>, struct modem&, uint64_t, uint64_t, uint64_t, std::decay_t<Args>...>
LIBMODEM_INLINE void modem_data_stream::add_on_modem_before_start_render_audio(Func&& f, Args&&... args)
{
    on_modem_before_start_render_audio_.set(std::forward<Func>(f), std::forward<Args>(args)...);
}

template<typename Func, typename... Args>
    requires std::invocable<std::decay_t<Func>, struct modem&, const std::vector<double>&, size_t, uint64_t, uint64_t, uint64_t, std::decay_t<Args>...>
LIBMODEM_INLINE void modem_data_stream::add_on_modem_end_render_audio(Func&& f, Args&&... args)
{
    on_modem_end_render_audio_.set(std::forward<Func>(f), std::forward<Args>(args)...);
}

LIBMODEM_NAMESPACE_END