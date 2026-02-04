// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// audio_stream.h
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
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <stop_token>
#include <optional>
#include <functional>
#include <thread>
#include <condition_variable>
#include <exception>
#include <array>
#include <unordered_set>
#include <exception>

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
// audio_stream_exception                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

enum class audio_stream_error
{
    none,
    not_initialized,      // stream or device not initialized
    invalid_state,        // operation invalid for current state
    invalid_argument,     // invalid argument provided
    device_not_found,     // device does not exist or was removed
    device_busy,          // device is in use by another process
    device_lost,          // device was disconnected during operation
    system_init_failed,   // COM/system initialization failed
    device_enum_failed,   // failed to enumerate audio devices
    device_open_failed,   // failed to open/activate device
    client_init_failed,   // failed to initialize audio client
    format_not_supported, // audio format not supported by device
    buffer_error,         // failed to get/release audio buffer
    underrun,             // output buffer underrun
    overrun,              // input buffer overrun
    volume_error,         // failed to get/set volume or mute
    start_failed,         // failed to start stream
    stop_failed,          // failed to stop stream
    file_error,           // failed to read/write audio file
    timeout,              // operation timed out
    connection_error,     // connection to remote endpoint failed or lost
    protocol_error,       // invalid or malformed response data
    internal_error        // unexpected internal failure
};

class audio_stream_exception : public std::exception
{
public:
    audio_stream_exception();
    audio_stream_exception(const std::string& message);
    audio_stream_exception(const std::string& message, audio_stream_error error);
    audio_stream_exception(audio_stream_error error);
    audio_stream_exception(const audio_stream_exception& other);
    audio_stream_exception& operator=(const audio_stream_exception& other);
    ~audio_stream_exception();

    const char* what() const noexcept override;

    audio_stream_error error() const noexcept;

    const std::string& message() const noexcept;

private:
    std::string message_;
    audio_stream_error error_;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// audio_stream_type                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

enum class audio_stream_type : int
{
    unknown,
    output,
    input,
    null
};

audio_stream_type parse_audio_stream_type(const std::string& type_string);
std::string to_string(audio_stream_type type);

// **************************************************************** //
//                                                                  //
//                                                                  //
// audio_stream_base                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct wav_audio_input_stream;

struct audio_stream_base
{
    virtual audio_stream_base& operator=(wav_audio_input_stream& rhs);

    virtual ~audio_stream_base() = default;

    virtual void close() noexcept = 0;

    virtual std::string name() = 0;
    virtual audio_stream_type type() = 0;

    virtual void volume(int percent) = 0;
    virtual int volume() = 0;

    virtual int sample_rate() = 0;
    virtual int channels() = 0;

    virtual size_t write(const double* samples, size_t count) = 0;
    virtual size_t write_interleaved(const double* samples, size_t count) = 0;
    virtual size_t read(double* samples, size_t count) = 0;
    virtual size_t read_interleaved(double* samples, size_t count) = 0;

    template<typename Rep, typename Period>
    bool wait_write_completed(const std::chrono::duration<Rep, Period>& timeout)
    {
        return wait_write_completed(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count()));
    }

    virtual void wait_write_completed();

    virtual bool wait_write_completed(int timeout_ms) = 0;

    virtual bool eof() = 0;

    virtual void start() = 0;
    virtual void stop() noexcept = 0;

    virtual explicit operator bool() = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// audio_stream                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

class audio_stream : public audio_stream_base
{
public:
    using audio_stream_base::wait_write_completed;

    audio_stream(std::nullptr_t);
    explicit audio_stream(std::unique_ptr<audio_stream_base> s);
    audio_stream(const audio_stream&) = delete;
    audio_stream& operator=(const audio_stream&) = delete;
    audio_stream(audio_stream&&) = default;
    audio_stream& operator=(audio_stream&&) = default;
    audio_stream& operator=(wav_audio_input_stream& rhs);
    virtual ~audio_stream();

    void close() noexcept override;

    std::string name() override;
    audio_stream_type type() override;
    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    int channels() override;

    size_t write(const double* samples, size_t count) override;
    size_t write_interleaved(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    size_t read_interleaved(double* samples, size_t count) override;

    bool wait_write_completed(int timeout_ms) override;

    bool eof() override;

    void start() override;
    void stop() noexcept override;

    virtual audio_stream_base& get();

    std::unique_ptr<audio_stream_base> release();

    explicit operator bool() override;

private:
    std::unique_ptr<audio_stream_base> stream_;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// null_audio_stream                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

class null_audio_stream : public audio_stream_base
{
public:
    using audio_stream_base::wait_write_completed;

    void close() noexcept override;

    std::string name() override;
    audio_stream_type type() override;
    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    int channels() override;

    size_t write(const double* samples, size_t count) override;
    size_t write_interleaved(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    size_t read_interleaved(double* samples, size_t count) override;

    bool wait_write_completed(int timeout_ms) override;

    bool eof() override;

    void start() override;
    void stop() noexcept override;

    explicit operator bool() override;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// audio_device_type                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

enum class audio_device_type : int
{
    unknown = 0,
    capture = 1,
    render = 2
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// audio_device_state                                               //
//                                                                  //
//                                                                  //
// **************************************************************** //

enum class audio_device_state
{
    unknown,
    active,
    disabled,
    unplugged,
    not_present
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// audio_device                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct audio_device_impl;

struct audio_device
{
    friend std::vector<audio_device> get_audio_devices();

    audio_device();

#if WIN32
    audio_device(audio_device_impl* impl);
#endif // WIN32

#if __linux__
    audio_device(int card_id, int device_id, audio_device_type type);
#endif // __linux__

    audio_device(const audio_device&) = delete;
    audio_device& operator=(const audio_device& other) = delete;
    audio_device(audio_device&&) noexcept;
    audio_device& operator=(audio_device&& other) noexcept;
    virtual ~audio_device();

    audio_stream stream();

    std::string id;
    std::string name;
    std::string description;
    audio_device_type type = audio_device_type::unknown;
    audio_device_state state = audio_device_state::unknown;

#if WIN32
    std::string container_id;
#endif // WIN32

#if __linux__
    int card_id;
    int device_id;
#endif // __linux__

private:
    std::unique_ptr<audio_device_impl> impl_;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// get_audio_devices                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<audio_device> get_audio_devices();
std::vector<audio_device> get_audio_devices(audio_device_type type, audio_device_state state);

// **************************************************************** //
//                                                                  //
//                                                                  //
// try_get_audio_device_by_name                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool try_get_audio_device_by_name(const std::string& name, audio_device& device, audio_device_type type, audio_device_state state);

// **************************************************************** //
//                                                                  //
//                                                                  //
// try_get_audio_device_by_id                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool try_get_audio_device_by_id(const std::string& id, audio_device& device);

#ifdef __linux__

bool try_get_audio_device_by_id(int card_id, audio_device& device);
bool try_get_audio_device_by_id(int card_id, int device_id, audio_device& device);

#endif // __linux__

// **************************************************************** //
//                                                                  //
//                                                                  //
// try_get_audio_device_by_description                              //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool try_get_audio_device_by_description(const std::string& description, audio_device& device, audio_device_type type, audio_device_state state);

// **************************************************************** //
//                                                                  //
//                                                                  //
// try_get_default_audio_device                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool try_get_default_audio_device(audio_device& device);
bool try_get_default_audio_device(audio_device& device, audio_device_type type);

// **************************************************************** //
//                                                                  //
//                                                                  //
// wasapi_audio_output_stream                                       //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if WIN32

struct wasapi_audio_output_stream_impl;
struct audio_device_impl;

class wasapi_audio_output_stream : public audio_stream_base
{
public:
    using audio_stream_base::wait_write_completed;

    wasapi_audio_output_stream();
    wasapi_audio_output_stream(audio_device_impl* impl);
    wasapi_audio_output_stream(const wasapi_audio_output_stream&) = delete;
    wasapi_audio_output_stream& operator=(const wasapi_audio_output_stream&) = delete;
    wasapi_audio_output_stream(wasapi_audio_output_stream&&) noexcept;
    wasapi_audio_output_stream& operator=(wasapi_audio_output_stream&&) noexcept;
    wasapi_audio_output_stream& operator=(wav_audio_input_stream& rhs);
    virtual ~wasapi_audio_output_stream();

    void close() noexcept override;

    std::string name();
    audio_stream_type type() override;

    void mute(bool);
    bool mute();

    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    int channels() override;
    size_t write(const double* samples, size_t count) override;
    size_t write_interleaved(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    size_t read_interleaved(double* samples, size_t count) override;

    bool wait_write_completed(int timeout_ms) override;

    bool eof() override;

    void start() override;
    void stop() noexcept override;

    bool faulted();
    void throw_if_faulted();

    void flush();

    explicit operator bool() override;

private:
    void run(std::stop_token stop_token);
    void run_internal(std::stop_token stop_token);

    int buffer_size_ = 0;
    int sample_rate_ = 0;
    int channels_ = 0;
    std::atomic<bool> started_ = false;
    std::atomic<bool> render_thread_exited_ = false;
    std::unique_ptr<wasapi_audio_output_stream_impl> impl_;
    std::jthread render_thread_;
    std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;
    std::exception_ptr render_exception_;
    size_t ring_buffer_size_seconds_ = 5;
    std::atomic<uint64_t> total_frames_written_ = 0;
    std::mutex start_stop_mutex_;
};

#endif // WIN32

// **************************************************************** //
//                                                                  //
//                                                                  //
// wasapi_audio_input_stream                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if WIN32

struct wasapi_audio_input_stream_impl;
struct audio_device_impl;

class wasapi_audio_input_stream : public audio_stream_base
{
public:
    using audio_stream_base::wait_write_completed;

    wasapi_audio_input_stream();
    wasapi_audio_input_stream(audio_device_impl* impl);
    wasapi_audio_input_stream(const wasapi_audio_input_stream&) = delete;
    wasapi_audio_input_stream& operator=(const wasapi_audio_input_stream&) = delete;
    wasapi_audio_input_stream(wasapi_audio_input_stream&&) noexcept;
    wasapi_audio_input_stream& operator=(wasapi_audio_input_stream&&) noexcept;
    virtual ~wasapi_audio_input_stream();

    void close() noexcept override;

    std::string name();
    audio_stream_type type() override;

    void mute(bool);
    bool mute();

    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    int channels() override;
    size_t write(const double* samples, size_t count) override;
    size_t write_interleaved(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    size_t read_interleaved(double* samples, size_t count) override;

    bool wait_write_completed(int timeout_ms) override;

    bool eof() override;

    void start() override;
    void stop() noexcept override;

    bool faulted();
    void throw_if_faulted();

    void flush();

    explicit operator bool() override;

private:
    void run(std::stop_token stop_token);
    void run_internal(std::stop_token stop_token);

    int buffer_size_ = 0;
    int sample_rate_ = 0;
    int channels_ = 1;
    std::atomic<bool> started_ = false;
    std::unique_ptr<wasapi_audio_input_stream_impl> impl_;
    std::jthread capture_thread_;
    std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;
    std::exception_ptr capture_exception_;
    std::atomic<bool> capture_thread_exited_ = false;
    size_t discontinuity_count_ = 0;
    size_t ring_buffer_size_seconds_ = 5;
    std::mutex start_stop_mutex_;
};

#endif // WIN32

// **************************************************************** //
//                                                                  //
//                                                                  //
// alsa_audio_stream_control                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if __linux__

class alsa_audio_stream_control
{
public:
    alsa_audio_stream_control(int card_id, const std::string& name, int index, int channel, audio_stream_type type);

    std::string id() const;
    std::string name() const;
    int index() const;

    void volume(int percent);
    int volume();

    void mute(bool mute);
    bool mute();

    int channel() const;

    bool can_mute();
    bool can_set_volume();

private:
    int card_id_ = -1;
    std::string name_;
    int index_ = 0;
    int channel_ = 0;
    audio_stream_type type_ = audio_stream_type::output;
};

#endif // __linux__

// **************************************************************** //
//                                                                  //
//                                                                  //
// alsa_audio_output_stream                                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if __linux__

struct alsa_audio_stream_impl;

class alsa_audio_output_stream : public audio_stream_base
{
public:
    using audio_stream_base::wait_write_completed;

    alsa_audio_output_stream();
    alsa_audio_output_stream(int card_id, int device_id);
    alsa_audio_output_stream(const alsa_audio_output_stream&) = delete;
    alsa_audio_output_stream& operator=(const alsa_audio_output_stream&) = delete;
    alsa_audio_output_stream(alsa_audio_output_stream&&) noexcept;
    alsa_audio_output_stream& operator=(alsa_audio_output_stream&&) noexcept;
    virtual ~alsa_audio_output_stream();

    void close() noexcept override;
    audio_stream_type type() override;

    std::string name() override;

    void mute(bool);
    bool mute();

    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    int channels() override;
    size_t write(const double* samples, size_t count) override;
    size_t write_interleaved(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    size_t read_interleaved(double* samples, size_t count) override;

    bool wait_write_completed(int timeout_ms) override;

    bool eof() override;

    void start() override;
    void stop() noexcept override;
    void enable_start_stop(bool);
    bool enable_start_stop();

    explicit operator bool() override;

    std::vector<alsa_audio_stream_control> controls();

    int card_id = -1;
    int device_id = -1;

private:
    size_t write_interleaved(const float* samples, size_t count);

    int sample_rate_ = 48000;
    int channels_ = 0;
    std::atomic<bool> started_ = false;
    bool start_stop_enabled_ = false;
    std::unique_ptr<alsa_audio_stream_impl> impl_;
    std::mutex start_stop_mutex_;
};

#endif // __linux__

// **************************************************************** //
//                                                                  //
//                                                                  //
// alsa_audio_input_stream                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if __linux__

struct alsa_audio_stream_impl;

class alsa_audio_input_stream : public audio_stream_base
{
public:
    alsa_audio_input_stream();
    alsa_audio_input_stream(int card_id, int device_id);
    alsa_audio_input_stream(const alsa_audio_input_stream&) = delete;
    alsa_audio_input_stream& operator=(const alsa_audio_input_stream&) = delete;
    alsa_audio_input_stream(alsa_audio_input_stream&& other) noexcept;
    alsa_audio_input_stream& operator=(alsa_audio_input_stream&& other) noexcept;
    ~alsa_audio_input_stream();

    void close() noexcept override;
    audio_stream_type type() override;

    std::string name() override;

    void mute(bool mute);
    bool mute();

    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    int channels() override;
    size_t write(const double* samples, size_t count) override;
    size_t write_interleaved(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    size_t read_interleaved(double* samples, size_t count) override;

    bool wait_write_completed(int timeout_ms) override;

    bool eof() override;

    void start() override;
    void stop() noexcept override;
    void enable_start_stop(bool enable);
    bool enable_start_stop();

    explicit operator bool() override;

    std::vector<alsa_audio_stream_control> controls();

    int card_id = -1;
    int device_id = -1;

private:
    size_t read_interleaved(float* samples, size_t count);

    int sample_rate_ = 48000;
    int channels_ = 2;
    std::atomic<bool> started_ = false;
    bool start_stop_enabled_ = false;
    std::unique_ptr<alsa_audio_stream_impl> impl_;
    std::mutex start_stop_mutex_;
};

#endif // __linux__

// **************************************************************** //
//                                                                  //
//                                                                  //
// wav_audio_input_stream                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct wav_audio_impl;

struct wav_audio_input_stream : audio_stream_base
{
public:
    wav_audio_input_stream(const std::string& filename);
    wav_audio_input_stream(const wav_audio_input_stream&) = delete;
    wav_audio_input_stream& operator=(const wav_audio_input_stream&) = delete;
    wav_audio_input_stream(wav_audio_input_stream&&) noexcept;
    wav_audio_input_stream& operator=(wav_audio_input_stream&&) noexcept;
    virtual ~wav_audio_input_stream();

    void close() noexcept override;

    std::string name() override;
    audio_stream_type type() override;

    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    int channels() override;
    size_t write(const double* samples, size_t count) override;
    size_t write_interleaved(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    size_t read_interleaved(double* samples, size_t count) override;

    bool wait_write_completed(int timeout_ms) override;

    bool eof() override;

    void flush();

    void start() override;
    void stop() noexcept override;

    explicit operator bool() override;

private:
    std::unique_ptr<wav_audio_impl> impl_;
    std::string filename_;
    int sample_rate_ = 0;
    int channels_ = 0;
    size_t total_frames_ = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// wav_audio_output_stream                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct wav_audio_impl;

struct wav_audio_output_stream : audio_stream_base
{
public:
    wav_audio_output_stream(const std::string& filename, int sample_rate = 48000);
    wav_audio_output_stream(const wav_audio_output_stream&) = delete;
    wav_audio_output_stream& operator=(const wav_audio_output_stream&) = delete;
    wav_audio_output_stream(wav_audio_output_stream&&) noexcept;
    wav_audio_output_stream& operator=(wav_audio_output_stream&&) noexcept;
    wav_audio_output_stream& operator=(wav_audio_input_stream& rhs) override;
    virtual ~wav_audio_output_stream();

    void close() noexcept override;

    std::string name() override;
    audio_stream_type type() override;

    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    int channels() override;
    size_t write(const double* samples, size_t count) override;
    size_t write_interleaved(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    size_t read_interleaved(double* samples, size_t count) override;

    bool wait_write_completed(int timeout_ms) override;

    bool eof() override;

    void flush();

    void start() override;
    void stop() noexcept override;

    explicit operator bool() override;

private:
    std::unique_ptr<wav_audio_impl> impl_;
    std::string filename_;
    int sample_rate_ = 0;
    const int channels_ = 1;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_audio_stream_control_client                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_audio_stream_control_client_impl;

class tcp_audio_stream_control_client
{
public:
    tcp_audio_stream_control_client();
    tcp_audio_stream_control_client& operator=(const tcp_audio_stream_control_client&) = delete;
    tcp_audio_stream_control_client(const tcp_audio_stream_control_client&) = delete;
    tcp_audio_stream_control_client& operator=(tcp_audio_stream_control_client&&) noexcept;
    tcp_audio_stream_control_client(tcp_audio_stream_control_client&&) noexcept;
    ~tcp_audio_stream_control_client();

    bool connect(const std::string& host, int port);
    void disconnect();

    bool connected() const;

    std::string name();
    audio_stream_type type();

    void volume(int percent);
    int volume();

    int sample_rate();
    int channels();

    void start();
    void stop();

private:
    std::unique_ptr<tcp_audio_stream_control_client_impl> impl_;
    bool connected_ = false;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_audio_stream_control_server                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_audio_stream_control_server_impl;
struct tcp_audio_stream_control_client_connection_impl;

class tcp_audio_stream_control_server
{
public:
    tcp_audio_stream_control_server();
    tcp_audio_stream_control_server(audio_stream_base&);
    tcp_audio_stream_control_server& operator=(const tcp_audio_stream_control_server&) = delete;
    tcp_audio_stream_control_server(const tcp_audio_stream_control_server&) = delete;
    tcp_audio_stream_control_server& operator=(tcp_audio_stream_control_server&&) noexcept;
    tcp_audio_stream_control_server(tcp_audio_stream_control_server&&) noexcept;
    ~tcp_audio_stream_control_server();

    bool start(const std::string& host, int port);
    void stop();

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

    bool faulted();
    void throw_if_faulted();

private:
    void run();
    void accept_async();
    void read_async(std::shared_ptr<tcp_audio_stream_control_client_connection_impl> connection);
    void write_async(std::shared_ptr<tcp_audio_stream_control_client_connection_impl> connection, std::vector<uint8_t> response);
    std::vector<uint8_t> handle_request(const std::vector<uint8_t>& data);

    std::optional<std::reference_wrapper<audio_stream_base>> stream_;
    std::unique_ptr<tcp_audio_stream_control_server_impl> impl_;
    std::vector<std::jthread> threads_;
    std::size_t thread_count_ = 1;
    std::atomic<bool> running_ = false;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::exception_ptr exception_;
    std::mutex clients_mutex_;
    std::vector<std::jthread> client_threads_;
    std::mutex connections_mutex_;
    std::unordered_set<std::shared_ptr<tcp_audio_stream_control_client_connection_impl>> connections_;
    bool ready_ = false;
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

LIBMODEM_NAMESPACE_END