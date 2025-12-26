// **************************************************************** //
// modem - APRS modem                                               // 
// Version 0.1.0                                                    //
// https://github.com/iontodirel/modem                              //
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
#include <cstdint>
#include <numeric>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <stop_token>

#ifdef __linux__

#include <alsa/asoundlib.h>

#endif // __linux__

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
// audio_stream_base                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct audio_stream_base
{
    virtual ~audio_stream_base() = default;

    virtual std::string name() = 0;

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

    virtual void wait_write_completed()
    {
        wait_write_completed(-1);
    }

    virtual bool wait_write_completed(int timeout_ms) = 0;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void close() = 0;
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
    audio_stream(std::unique_ptr<audio_stream_base> s);

    audio_stream(audio_stream&&) = default;
    audio_stream& operator=(audio_stream&&) = default;
    audio_stream(const audio_stream&) = delete;
    audio_stream& operator=(const audio_stream&) = delete;
    virtual ~audio_stream();

    void close() override;

    std::string name() override;
    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    int channels() override;

    size_t write(const double* samples, size_t count) override;
    size_t write_interleaved(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    size_t read_interleaved(double* samples, size_t count) override;

    bool wait_write_completed(int timeout_ms) override;

    void start() override;
    void stop() override;

    explicit operator bool() const;

private:
    std::unique_ptr<audio_stream_base> stream_;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// channelized_stream_base                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct channelized_stream_base : public audio_stream_base
{
    virtual size_t write_channel(size_t channel, const double* samples, size_t count) = 0;
    virtual size_t read_channel(size_t channel, double* samples, size_t count) = 0;
    virtual bool write_lock(int channel, int timeout_ms) = 0;
    virtual void write_unlock(int channel) = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// channel_stream                                                   //
//                                                                  //
//                                                                  //
// **************************************************************** //

class channel_stream : public audio_stream_base
{
public:
    channel_stream(channelized_stream_base& stream, int channel);
    virtual ~channel_stream();

    void close() override;

    std::string name() override;
    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    int channels() override;

    size_t write(const double* samples, size_t count) override;
    size_t write_interleaved(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    size_t read_interleaved(double* samples, size_t count) override;

    bool wait_write_completed(int timeout_ms) override;

    void start() override;
    void stop() override;

    int channel() const;

    explicit operator bool() const;

private:
    std::optional<std::reference_wrapper<channelized_stream_base>> stream;
    int channel_ = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// channelized_stream                                               //
//                                                                  //
//                                                                  //
// **************************************************************** //

class channelized_stream : public channelized_stream_base
{
public:
    channelized_stream(std::unique_ptr<audio_stream_base> s);
    channelized_stream(channelized_stream&&) = default;
    channelized_stream& operator=(channelized_stream&&) = default;
    channelized_stream(const channelized_stream&) = delete;
    channelized_stream& operator=(const channelized_stream&) = delete;
    ~channelized_stream();

    void close() override;

    std::string name() override;
    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    int channels() override;

    size_t write(const double* samples, size_t count) override;
    size_t write_interleaved(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    size_t read_interleaved(double* samples, size_t count) override;

    bool wait_write_completed(int timeout_ms) override;

    size_t write_channel(size_t channel, const double* samples, size_t count);
    size_t read_channel(size_t channel, double* samples, size_t count);

    bool write_lock(int timeout_ms);
    void write_unlock();
    bool write_lock(int channel, int timeout_ms) override;
    void write_unlock(int channel) override;

    void flush();

    void start() override;
    void stop() override;

    std::vector<channel_stream> channel_streams();

    explicit operator bool() const;

private:
    std::unique_ptr<audio_stream_base> stream_;
    int sample_size = 1024;
    std::vector<std::vector<double>> write_buffers_;
    std::shared_timed_mutex write_mutex_;
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

    std::unique_ptr<audio_stream_base> stream();

    std::string id;
    std::string name;
    std::string description;
    audio_device_type type = audio_device_type::unknown;
    audio_device_state state = audio_device_state::active;

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
    wasapi_audio_output_stream();
    wasapi_audio_output_stream(audio_device_impl* impl);
    wasapi_audio_output_stream(const wasapi_audio_output_stream&) = delete;
    wasapi_audio_output_stream& operator=(const wasapi_audio_output_stream&) = delete;
    wasapi_audio_output_stream(wasapi_audio_output_stream&&) noexcept;
    wasapi_audio_output_stream& operator=(wasapi_audio_output_stream&&) noexcept;
    virtual ~wasapi_audio_output_stream();

    void close();

    std::string name();

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

    bool wait_write_completed(int timeout_ms);

    void start() override;
    void stop() override;

private:
    int buffer_size_ = 0;
    int sample_rate_ = 0;
    int channels_ = 0;
    bool started_ = false;
    std::unique_ptr<wasapi_audio_output_stream_impl> impl_;
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

struct wasapi_audio_input_stream : public audio_stream_base
{
    wasapi_audio_input_stream();
    wasapi_audio_input_stream(audio_device_impl* impl);
    wasapi_audio_input_stream(const wasapi_audio_input_stream&) = delete;
    wasapi_audio_input_stream& operator=(const wasapi_audio_input_stream&) = delete;
    wasapi_audio_input_stream(wasapi_audio_input_stream&&) noexcept;
    wasapi_audio_input_stream& operator=(wasapi_audio_input_stream&&) noexcept;
    virtual ~wasapi_audio_input_stream();

    void close();

    std::string name();

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
    bool wait_write_completed(int timeout_ms);

    void start() override;
    void stop() override;

    bool faulted();
    void throw_if_faulted();

    void flush();

private:
    void run(std::stop_token stop_token);
    void run_internal(std::stop_token stop_token);

    int buffer_size_ = 0;
    int sample_rate_ = 0;
    int channels_ = 1;
    bool started_ = false;
    std::unique_ptr<wasapi_audio_input_stream_impl> impl_;
};

#endif // WIN32

// **************************************************************** //
//                                                                  //
//                                                                  //
// alsa_audio_stream                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if __linux__

struct alsa_audio_stream : public audio_stream_base
{
    alsa_audio_stream();
    alsa_audio_stream(int card_id, int device_id, audio_device_type type);
    alsa_audio_stream(const alsa_audio_stream&);
    alsa_audio_stream& operator=(const alsa_audio_stream&);
    virtual ~alsa_audio_stream();

    void close();

    std::string name();

    void mute(bool);
    bool mute();

    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    int channels() override;
    size_t write(const double* samples, size_t count) override;
    size_t write_interleaved(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    bool wait_write_completed(int timeout_ms);

    void start();
    void stop();

private:
    int card_id;
    int device_id;
    snd_pcm_t* pcm_handle_ = nullptr;
    int sample_rate_ = 48000;
    unsigned int num_channels_ = 1;
    audio_device_type type;
    snd_pcm_format_t format_;
    std::vector<float> buffer;
    std::vector<int16_t> s16_buffer;
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

    std::string name() override;

    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    int channels() override;
    size_t write(const double* samples, size_t count) override;
    size_t write_interleaved(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    size_t read_interleaved(double* samples, size_t count) override;

    bool wait_write_completed(int timeout_ms) override;

    void flush();
    void close() override;

    void start() override;
    void stop() override;

private:
    std::unique_ptr<wav_audio_impl> impl_;
    std::string filename_;
    int sample_rate_ = 0;
    int channels_ = 0;
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
    virtual ~wav_audio_output_stream();

    std::string name() override;

    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    int channels() override;
    size_t write(const double* samples, size_t count) override;
    size_t write_interleaved(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    size_t read_interleaved(double* samples, size_t count) override;

    bool wait_write_completed(int timeout_ms) override;

    void flush();
    void close() override;

    void start() override;
    void stop() override;

private:
    std::unique_ptr<wav_audio_impl> impl_;
    std::string filename_;
    int sample_rate_ = 0;
    const int channels_ = 1;
};

LIBMODEM_NAMESPACE_END