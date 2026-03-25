// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// formatter.h
//
// MIT License
//
// Copyright (c) 2026 Ion Todirel
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

#include <vector>
#include <string>
#include <iterator>
#include <utility>
#include <cstdint>
#include <cstring>
#include <memory>
#include <queue>
#include <optional>

#include "bitstream.h"

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

#ifndef LIBMODEM_KISS_NAMESPACE_BEGIN
#define LIBMODEM_KISS_NAMESPACE_BEGIN namespace kiss {
#endif
#ifndef LIBMODEM_KISS_NAMESPACE_END
#define LIBMODEM_KISS_NAMESPACE_END }
#endif
#ifndef LIBMODEM_KISS_USING_NAMESPACE
#define LIBMODEM_KISS_USING_NAMESPACE using namespace kiss;
#endif
#ifndef LIBMODEM_KISS_NAMESPACE_REFERENCE
#define LIBMODEM_KISS_NAMESPACE_REFERENCE kiss :: 
#endif

#ifndef LIBMODEM_AGWPE_NAMESPACE_BEGIN
#define LIBMODEM_AGWPE_NAMESPACE_BEGIN namespace agwpe {
#endif
#ifndef LIBMODEM_AGWPE_NAMESPACE_END
#define LIBMODEM_AGWPE_NAMESPACE_END }
#endif
#ifndef LIBMODEM_AGWPE_USING_NAMESPACE
#define LIBMODEM_AGWPE_USING_NAMESPACE using namespace agwpe;
#endif
#ifndef LIBMODEM_AGWPE_NAMESPACE_REFERENCE
#define LIBMODEM_AGWPE_NAMESPACE_REFERENCE agwpe :: 
#endif

#ifndef LIBMODEM_INLINE
#define LIBMODEM_INLINE inline
#endif

#ifndef LIBMODEM_ANONYMOUS_NAMESPACE_BEGIN
#define LIBMODEM_ANONYMOUS_NAMESPACE_BEGIN namespace {
#endif
#ifndef LIBMODEM_ANONYMOUS_NAMESPACE_END
#define LIBMODEM_ANONYMOUS_NAMESPACE_END }
#endif

// **************************************************************** //
//                                                                  //
//                                                                  //
// KISS IMPLEMENTATION                                              //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_NAMESPACE_BEGIN

LIBMODEM_KISS_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// frame_marker                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

enum class frame_marker : unsigned char
{
    fend = 0xC0,
    fesc = 0xDB,
    tfend = 0xDC,
    tfesc = 0xDD
};

bool operator==(unsigned char left, frame_marker right);

// **************************************************************** //
//                                                                  //
//                                                                  //
// decode                                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct decoder_state
{
    bool in_kiss_frame = false;
    bool in_escape_mode = false;
    bool completed = false;
};

bool decode(uint8_t byte, uint8_t& result, decoder_state& state);

// **************************************************************** //
//                                                                  //
//                                                                  //
// decoder                                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

enum class command : unsigned char
{
    data_frame = 0,
    tx_delay = 1,
    p = 2,
    slot_time = 3,
    tx_tail = 4,
    full_duplex = 5,
    set_hw = 6,
    ret = 0xff
};

struct frame
{
    unsigned char command_byte;
    std::vector<unsigned char> data;
};

struct decoder
{
    template <std::input_iterator InputIterator>
    bool decode(InputIterator input_it_begin, InputIterator input_it_end);

    const std::vector<struct frame>& frames() const;

    size_t count() const;

    void reset();
    void clear();

private:
    std::vector<struct frame> data_;
    std::vector<unsigned char> buffer_;
    decoder_state state_;
};

template <std::input_iterator InputIterator>
LIBMODEM_INLINE bool decoder::decode(InputIterator input_it_begin, InputIterator input_it_end)
{
    size_t current_data_count = data_.size();

    for (auto it = input_it_begin; it != input_it_end; ++it)
    {
        uint8_t byte = *it;

        uint8_t decoded_byte;

        bool result = LIBMODEM_KISS_NAMESPACE_REFERENCE decode(byte, decoded_byte, state_);

        if (result)
        {
            buffer_.push_back(decoded_byte);
        }

        if (state_.completed)
        {
            state_ = {};

            frame f;
            f.command_byte = buffer_[0];
            f.data = std::vector<unsigned char>(buffer_.begin() + 1, buffer_.end());
            data_.emplace_back(f);

            buffer_.clear();
        }
    }

    // True if we do not have incomplete data
    return (current_data_count < data_.size()) && buffer_.empty() && !state_.in_kiss_frame;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// encode                                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

template <std::input_iterator InputIt, std::output_iterator<unsigned char> OutputIt>
std::pair<OutputIt, bool> encode(InputIt input_it_begin, InputIt input_it_end, OutputIt output_it);

template <std::input_iterator InputIt, std::output_iterator<unsigned char> OutputIt, bool Command = true>
std::pair<OutputIt, bool> encode(uint8_t command_byte, InputIt input_it_begin, InputIt input_it_end, OutputIt output_it);

std::vector<uint8_t> encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> encode(uint8_t command_byte, const std::vector<uint8_t>& data);

template <std::input_iterator InputIt, std::output_iterator<unsigned char> OutputIt>
LIBMODEM_INLINE std::pair<OutputIt, bool> encode(InputIt input_it_begin, InputIt input_it_end, OutputIt output_it)
{
    return encode<InputIt, OutputIt, false>(0, input_it_begin, input_it_end, output_it);
}

template <std::input_iterator InputIt, std::output_iterator<unsigned char> OutputIt, bool Command>
LIBMODEM_INLINE std::pair<OutputIt, bool> encode(uint8_t command_byte, InputIt input_it_begin, InputIt input_it_end, OutputIt output_it)
{
    if (input_it_begin == input_it_end)
    {
        return { output_it, false };
    }

    *output_it++ = static_cast<unsigned char>(frame_marker::fend);

    if constexpr (Command)
    {
        *output_it++ = command_byte;
    }

    for (auto input_it = input_it_begin; input_it != input_it_end; ++input_it)
    {
        unsigned char b = *input_it;

        if (b == frame_marker::fend)
        {
            *output_it++ = static_cast<unsigned char>(frame_marker::fesc);
            *output_it++ = static_cast<unsigned char>(frame_marker::tfend);
        }
        else if (b == frame_marker::fesc)
        {
            *output_it++ = static_cast<unsigned char>(frame_marker::fesc);
            *output_it++ = static_cast<unsigned char>(frame_marker::tfesc);
        }
        else
        {
            *output_it++ = b;
        }
    }

    *output_it++ = static_cast<unsigned char>(frame_marker::fend);

    return { output_it, true };
}

LIBMODEM_KISS_NAMESPACE_END

LIBMODEM_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// AGWPE IMPLEMENTATION                                             //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_NAMESPACE_BEGIN

LIBMODEM_AGWPE_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// data_kind                                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

enum class data_kind : unsigned char
{
    login = 'P',
    version_request = 'R',
    port_info_request = 'G',
    port_cap_request = 'g',
    register_call = 'X',
    unregister_call = 'x',
    toggle_monitor = 'm',
    toggle_raw = 'k',
    transmit_raw = 'K',
    send_ui = 'M',
    send_ui_via = 'V',
    connect = 'C',
    connect_via = 'v',
    send_data = 'D',
    disconnect = 'd',
    query_frames_port = 'y',
    query_frames_conn = 'Y',
    heard_stations = 'H',
    monitor_ui = 'U',
    monitor_connected = 'I',
    monitor_super = 'S',
    monitor_own = 'T'
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// header                                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

#pragma pack(push, 1)
struct header
{
    uint8_t port = 0;
    uint8_t reserved1 = 0;
    uint8_t reserved2 = 0;
    uint8_t reserved3 = 0;
    uint8_t datakind = 0;
    uint8_t reserved4 = 0;
    uint8_t pid = 0;
    uint8_t reserved5 = 0;
    char from[10] = {};
    char to[10] = {};
    uint32_t data_length = 0;
    uint32_t user_reserved = 0;
};
#pragma pack(pop)

static_assert(sizeof(header) == 36, "AGWPE header must be exactly 36 bytes");

// **************************************************************** //
//                                                                  //
//                                                                  //
// frame                                                            //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct frame
{
    struct header header;
    std::vector<uint8_t> data;
};

struct v_frame
{
    std::vector<std::string> addresses;
    std::vector<uint8_t> info;
};

std::vector<uint8_t> to_bytes(const frame& f);

bool is_data_frame(const frame& f);

// **************************************************************** //
//                                                                  //
//                                                                  //
// encode                                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

frame encode_version_response_frame(uint32_t major, uint32_t minor);
frame encode_port_info_response_frame(const std::vector<std::string>& port_descriptions);
frame encode_port_capabilities_response_frame(uint8_t port, uint8_t on_air_baud_rate, uint8_t traffic_level, uint8_t tx_delay, uint8_t tx_tail, uint8_t persist, uint8_t slottime, uint8_t maxframe, uint8_t active_connections, uint32_t bytes_received);
frame encode_register_response_frame(const std::string& address, bool success);
frame encode_outstanding_frames_response_frame(uint8_t port, uint32_t count);
frame encode_ax25_response_frame(uint8_t port, const std::string& from_address, const std::string& to_address, uint8_t pid, const std::vector<uint8_t>& ax25_bytes);
frame encode_monitor_ui_response_frame(uint8_t port, const std::string& from_address, const std::string& to_address, const std::string& addresses, uint8_t pid, const std::vector<uint8_t>& info_field);
frame encode_connection_frames_response_frame(uint8_t port, const std::string& from_address, const std::string& to_address, uint32_t count);
frame encode_heard_stations_response_frame(uint8_t port);

bool try_encode_ax25_response_frames(const std::vector<uint8_t>& ax25_bytes, uint8_t port, frame& raw, frame& monitor);

bool decode_via_frame(const frame& f, v_frame& result);

bool try_decode(const frame& f, packet& p);

// **************************************************************** //
//                                                                  //
//                                                                  //
// session                                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct session
{
    bool monitoring_enabled = false;
    bool raw_enabled = false;
    std::string callsign;
    std::vector<std::string> port_descriptions = { "Port1 libmodem" };
    uint32_t version_major = 1;
    uint32_t version_minor = 0;
};

std::vector<frame> handle_frame(const frame& f, session& s);

LIBMODEM_AGWPE_NAMESPACE_END

LIBMODEM_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// formatter                                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_NAMESPACE_BEGIN

struct decode_result
{
    std::optional<packet> decoded_packet;
    uint8_t port = 0;
    std::vector<uint8_t> response_bytes;
};

struct formatter
{
    formatter();
    formatter(const formatter& other);
    virtual ~formatter();

    virtual std::unique_ptr<formatter> clone() const = 0;
    virtual std::vector<uint8_t> encode(packet p) = 0;
    virtual bool try_decode(const std::vector<uint8_t>& data, size_t count, packet& p, uint8_t& port) = 0;
    virtual bool try_decode(const std::vector<uint8_t>& data, size_t count, decode_result& result);

    virtual void context(std::size_t client_id) = 0;

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
    bool try_decode(const std::vector<uint8_t>& data, size_t count, packet& p, uint8_t& port) override;
    bool try_decode(const std::vector<uint8_t>& data, size_t count, kiss::frame& f);
    void context(std::size_t client_id) override;

private:
    kiss::decoder kiss_decoder_;
    std::queue<kiss::frame> pending_frames_;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// agwpe_formatter                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct agwpe_formatter : public formatter
{
public:
    std::unique_ptr<formatter> clone() const override;
    std::vector<uint8_t> encode(packet p) override;
    bool try_decode(const std::vector<uint8_t>& data, size_t count, packet& p, uint8_t& port) override;
    bool try_decode(const std::vector<uint8_t>& data, size_t count, decode_result& result) override;
    void context(std::size_t client_id) override;

    void port_descriptions(const std::vector<std::string>& descriptions);
    void version(uint32_t major, uint32_t minor);

private:
    void decode(const std::vector<uint8_t>& data, size_t count);

    std::vector<uint8_t> buffer_;
    std::queue<std::pair<packet, uint8_t>> pending_packets_;
    std::queue<std::vector<uint8_t>> pending_responses_;
    agwpe::session session_;
    size_t client_id_ = 0;
};

LIBMODEM_NAMESPACE_END