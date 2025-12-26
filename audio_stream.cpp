// **************************************************************** //
// modem - APRS modem                                               // 
// Version 0.1.0                                                    //
// https://github.com/iontodirel/modem                              //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// audio_stream.cpp
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

#include "audio_stream.h"

#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>

#if WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <comdef.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <audiopolicy.h>
#include <avrt.h>

#include <atlbase.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

#endif // WIN32

#ifdef __linux__

#include <alsa/asoundlib.h>

#endif // __linux__

#include <sndfile.h>

#include <boost/circular_buffer.hpp>

LIBMODEM_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
//  Win32 utils                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if WIN32

std::string utf16_to_utf8(const wchar_t* s)
{
    if (s == nullptr || s[0] == '\0')
    {
        return "";
    }

    int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0)
    {
        return "";
    }

    std::string r(len - 1, '\0');

    WideCharToMultiByte(CP_UTF8, 0, s, -1, r.data(), len, nullptr, nullptr);

    return r;
}

std::string get_string_property(IPropertyStore* props, const PROPERTYKEY& key)
{
    std::string result;
    PROPVARIANT variant;
    PropVariantInit(&variant);
    if (SUCCEEDED(props->GetValue(key, &variant)) && variant.vt == VT_LPWSTR)
    {
        result = utf16_to_utf8(variant.pwszVal);
    }
    PropVariantClear(&variant);
    return result;
}

std::string get_guid_property(IPropertyStore* props, const PROPERTYKEY& key)
{
    std::string result;
    PROPVARIANT variant;
    PropVariantInit(&variant);
    if (SUCCEEDED(props->GetValue(key, &variant)) && variant.vt == VT_CLSID && variant.puuid)
    {
        wchar_t buffer[64];
        StringFromGUID2(*variant.puuid, buffer, 64);
        result = utf16_to_utf8(buffer);
    }
    PropVariantClear(&variant);
    return result;
}

bool is_float32_format(WAVEFORMATEX* device_format)
{
    bool float32_format = false;

    if (device_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        float32_format = (device_format->wBitsPerSample == 32) && (device_format->nBlockAlign == device_format->nChannels * 4);
    }
    else if (device_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(device_format);
        float32_format = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) && device_format->wBitsPerSample == 32 &&
            ext->Samples.wValidBitsPerSample == 32 && (device_format->nBlockAlign == device_format->nChannels * 4);
    }

    return float32_format;
}

#endif


// **************************************************************** //
//                                                                  //
//                                                                  //
// audio_stream                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

audio_stream::audio_stream(std::unique_ptr<audio_stream_base> s) : stream_(std::move(s))
{
}

audio_stream::~audio_stream()
{
    stream_.reset();
}

void audio_stream::close()
{
    stream_.reset();
}

std::string audio_stream::name()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->name(); 
}

void audio_stream::volume(int percent)
{ 
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    stream_->volume(percent);
}

int audio_stream::volume()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->volume();
}

int audio_stream::sample_rate()
{ 
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->sample_rate(); 
}

int audio_stream::channels()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->channels();
}

size_t audio_stream::write(const double* samples, size_t count)
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->write(samples, count);
}

size_t audio_stream::write_interleaved(const double* samples, size_t count)
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->write_interleaved(samples, count);
}

size_t audio_stream::read(double* samples, size_t count)
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->read(samples, count);
}

size_t audio_stream::read_interleaved(double* samples, size_t count)
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->read_interleaved(samples, count);
}

bool audio_stream::wait_write_completed(int timeout_ms)
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->wait_write_completed(timeout_ms);
}

void audio_stream::start()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    stream_->start();
}

void audio_stream::stop()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    stream_->stop();
}

audio_stream::operator bool() const
{
    return stream_ != nullptr;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// channel_stream                                                   //
//                                                                  //
//                                                                  //
// **************************************************************** //

channel_stream::channel_stream(channelized_stream_base& stream, int channel) : stream(stream), channel_(channel)
{
}

channel_stream::~channel_stream()
{
    stream.reset();
}

void channel_stream::close()
{
    if (!stream.has_value())
    {
        throw std::runtime_error("Stream not initialized");
    }
    stream.value().get().close();
}

std::string channel_stream::name()
{
    if (!stream.has_value())
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream.value().get().name();
}

void channel_stream::volume(int percent)
{
    if (!stream.has_value())
    {
        throw std::runtime_error("Stream not initialized");
    }
    stream.value().get().volume(percent);
}

int channel_stream::volume()
{
    if (!stream.has_value())
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream.value().get().volume();
}

int channel_stream::sample_rate()
{
    if (!stream.has_value())
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream.value().get().sample_rate();
}

int channel_stream::channels()
{
    if (!stream.has_value())
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream.value().get().channels();
}

size_t channel_stream::write(const double* samples, size_t count)
{
    if (!stream.has_value())
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream.value().get().write_channel(channel_, samples, count);
}

size_t channel_stream::write_interleaved(const double* samples, size_t count)
{
    if (!stream.has_value())
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream.value().get().write_interleaved(samples, count);
}

size_t channel_stream::read(double* samples, size_t count)
{
    if (!stream.has_value())
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream.value().get().read_channel(channel_, samples, count);
}

size_t channel_stream::read_interleaved(double* samples, size_t count)
{
    if (!stream.has_value())
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream.value().get().read_interleaved(samples, count);
}

bool channel_stream::wait_write_completed(int timeout_ms)
{
    if (!stream.has_value())
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream.value().get().wait_write_completed(timeout_ms);
}

void channel_stream::start()
{
    if (!stream.has_value())
    {
        throw std::runtime_error("Stream not initialized");
    }
    stream.value().get().start();
}

void channel_stream::stop()
{
    if (!stream.has_value())
    {
        throw std::runtime_error("Stream not initialized");
    }
    stream.value().get().stop();
}

int channel_stream::channel() const
{
    return channel_;
}

channel_stream::operator bool() const
{
    return stream.has_value();
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// channelized_stream                                               //
//                                                                  //
//                                                                  //
// **************************************************************** //

channelized_stream::channelized_stream(std::unique_ptr<audio_stream_base> s) : stream_(std::move(s))
{
    write_buffers_.resize(s->channels());
}

channelized_stream::~channelized_stream()
{
    stream_.reset();
}

void channelized_stream::close()
{
    stream_.reset();
}

std::string channelized_stream::name()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->name();
}

void channelized_stream::volume(int percent)
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    stream_->volume(percent);
}

int channelized_stream::volume()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->volume();
}

int channelized_stream::sample_rate()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->sample_rate();
}

int channelized_stream::channels()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->channels();
}

size_t channelized_stream::write(const double* samples, size_t count)
{
    return 0;
}

size_t channelized_stream::write_interleaved(const double* samples, size_t count)
{
    return 0;
}

size_t channelized_stream::read(double* samples, size_t count)
{
    return 0;
}

size_t channelized_stream::read_interleaved(double* samples, size_t count)
{
    return 0;
}

bool channelized_stream::wait_write_completed(int timeout_ms)
{
    return false;
}


size_t channelized_stream::write_channel(size_t channel, const double* samples, size_t count)
{
    return 0;
}

size_t channelized_stream::read_channel(size_t channel, double* samples, size_t count)
{
    return 0;
}

bool channelized_stream::write_lock(int timeout_ms)
{
    return write_mutex_.try_lock_for(std::chrono::milliseconds(timeout_ms));
}

void channelized_stream::write_unlock()
{
    write_mutex_.unlock();
}

bool channelized_stream::write_lock(int channel, int timeout_ms)
{
    return false;
}

void channelized_stream::write_unlock(int channel)
{
}

void channelized_stream::start()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    stream_->start();
}

void channelized_stream::stop()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    stream_->stop();
}

std::vector<channel_stream> channelized_stream::channel_streams()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    std::vector<channel_stream> result;
    for (int i = 0; i < stream_->channels(); i++)
    {
        result.emplace_back(*this, i);
    }
    return result;
}

channelized_stream::operator bool() const
{
    return stream_ != nullptr;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// com_init                                                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if WIN32

class com_init
{
public:
    com_init();
    ~com_init();

    com_init(const com_init&) = delete;
    com_init& operator=(const com_init&) = delete;

    com_init(com_init&& other) noexcept;
    com_init& operator=(com_init&& other) noexcept;

    operator bool() const noexcept;

private:
    bool initialized_ = false;
};

com_init::com_init()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    initialized_ = SUCCEEDED(hr) || hr == S_FALSE;
}

com_init::~com_init()
{
    if (initialized_)
    {
        CoUninitialize();
    }
}

com_init::com_init(com_init&& other) noexcept : initialized_(other.initialized_)
{
    other.initialized_ = false;
}

com_init& com_init::operator=(com_init&& other) noexcept
{
    if (this != &other)
    {
        if (initialized_)
        {
            CoUninitialize();
        }
        initialized_ = other.initialized_;
        other.initialized_ = false;
    }
    return *this;
}

com_init::operator bool() const noexcept
{
    return initialized_;
}

static void ensure_com_initialized()
{
    static thread_local com_init tls_com_init;
    if (!tls_com_init)
    {
        throw std::runtime_error("COM init failed");
    }
}

#endif // WIN32

// **************************************************************** //
//                                                                  //
//                                                                  //
// audio_device_impl                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct audio_device_impl
{
    audio_device_impl() = default;
    audio_device_impl(const audio_device_impl&) = delete;
    audio_device_impl& operator=(const audio_device_impl& other) = delete;
    audio_device_impl(audio_device_impl&&);
    audio_device_impl& operator=(audio_device_impl&& other);
    ~audio_device_impl() = default;

#if WIN32
    CComPtr<IMMDevice> device_;
#endif // WIN32
};

audio_device_impl::audio_device_impl(audio_device_impl&& other)
{
    if (this != &other)
    {
#if WIN32
        device_.Attach(other.device_.Detach()); // Release old, take new
#endif // WIN32
    }
}

audio_device_impl& audio_device_impl::operator=(audio_device_impl&& other)
{
    if (this != &other)
    {
#if WIN32
        device_.Attach(other.device_.Detach());  // Release old, take new
#endif // WIN32
    }
    return *this;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// wasapi_audio_output_stream_impl                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if WIN32

struct wasapi_audio_output_stream_impl
{
    wasapi_audio_output_stream_impl() = default;
    wasapi_audio_output_stream_impl(const wasapi_audio_output_stream_impl&) = delete;
    wasapi_audio_output_stream_impl& operator=(const wasapi_audio_output_stream_impl& other) = delete;
    wasapi_audio_output_stream_impl(wasapi_audio_output_stream_impl&&);
    wasapi_audio_output_stream_impl& operator=(wasapi_audio_output_stream_impl&& other);
    ~wasapi_audio_output_stream_impl() = default;

    CComPtr<IMMDevice> device_;
    CComPtr<IAudioClient> audio_client_;
    CComPtr<IAudioRenderClient> render_client_;
    CComPtr<IAudioEndpointVolume> endpoint_volume_;
};

wasapi_audio_output_stream_impl::wasapi_audio_output_stream_impl(wasapi_audio_output_stream_impl&& other)
{
    device_.Attach(other.device_.Detach());
    audio_client_.Attach(other.audio_client_.Detach());
    render_client_.Attach(other.render_client_.Detach());
    endpoint_volume_.Attach(other.endpoint_volume_.Detach());
}

wasapi_audio_output_stream_impl& wasapi_audio_output_stream_impl::operator=(wasapi_audio_output_stream_impl&& other)
{
    if (this != &other)
    {
        render_client_.Release();
        endpoint_volume_.Release();
        audio_client_.Release();
        device_.Release();

        device_.Attach(other.device_.Detach());
        audio_client_.Attach(other.audio_client_.Detach());
        render_client_.Attach(other.render_client_.Detach());
        endpoint_volume_.Attach(other.endpoint_volume_.Detach());
    }
    return *this;
}

#endif // WIN32

// **************************************************************** //
//                                                                  //
//                                                                  //
// wasapi_audio_input_stream_impl                                   //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if WIN32

struct wasapi_audio_input_stream_impl
{
    wasapi_audio_input_stream_impl() = default;
    wasapi_audio_input_stream_impl(const wasapi_audio_input_stream_impl&) = delete;
    wasapi_audio_input_stream_impl& operator=(const wasapi_audio_input_stream_impl& other) = delete;
    wasapi_audio_input_stream_impl(wasapi_audio_input_stream_impl&&);
    wasapi_audio_input_stream_impl& operator=(wasapi_audio_input_stream_impl&& other);
    ~wasapi_audio_input_stream_impl();

    CComPtr<IMMDevice> device_;
    CComPtr<IAudioClient> audio_client_;
    CComPtr<IAudioCaptureClient> capture_client_;
    CComPtr<IAudioEndpointVolume> endpoint_volume_;

    // Event that the audio engine signals each time a buffer becomes ready
    // to be processed by the client. Used in event-driven capture mode.
    HANDLE audio_samples_ready_event_ = nullptr;

    // Event used to signal the capture loop to stop.
    HANDLE stop_capture_event_ = nullptr;

    // Capture thread
    std::jthread capture_thread_;

    // Ring buffer and synchronization
    boost::circular_buffer<float> ring_buffer_;
    std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;

    std::exception_ptr capture_exception_;

    size_t discontinuity_count_ = 0;

    size_t ring_buffer_size_seconds_ = 5;
};

wasapi_audio_input_stream_impl::wasapi_audio_input_stream_impl(wasapi_audio_input_stream_impl&& other)
{
    device_.Attach(other.device_.Detach());
    audio_client_.Attach(other.audio_client_.Detach());
    capture_client_.Attach(other.capture_client_.Detach());
    endpoint_volume_.Attach(other.endpoint_volume_.Detach());

    audio_samples_ready_event_ = other.audio_samples_ready_event_;
    other.audio_samples_ready_event_ = nullptr;

    stop_capture_event_ = other.stop_capture_event_;
    other.stop_capture_event_ = nullptr;

    // Note: capture_thread_ cannot be moved while running
    // the mutex, condition_variable, and ring_buffer_ should not be moved as they
    // represent running state, and a stream should not be moved while running

    capture_exception_ = other.capture_exception_;
    discontinuity_count_ = other.discontinuity_count_;
    ring_buffer_size_seconds_ = other.ring_buffer_size_seconds_;
}

wasapi_audio_input_stream_impl& wasapi_audio_input_stream_impl::operator=(wasapi_audio_input_stream_impl&& other)
{
    if (this != &other)
    {
        capture_client_.Release();
        endpoint_volume_.Release();
        audio_client_.Release();
        device_.Release();

        device_.Attach(other.device_.Detach());
        audio_client_.Attach(other.audio_client_.Detach());
        capture_client_.Attach(other.capture_client_.Detach());
        endpoint_volume_.Attach(other.endpoint_volume_.Detach());

        if (audio_samples_ready_event_ != nullptr)
        {
            CloseHandle(audio_samples_ready_event_);
        }
        if (stop_capture_event_ != nullptr)
        {
            CloseHandle(stop_capture_event_);
        }

        audio_samples_ready_event_ = other.audio_samples_ready_event_;
        other.audio_samples_ready_event_ = nullptr;

        stop_capture_event_ = other.stop_capture_event_;
        other.stop_capture_event_ = nullptr;

        // Note: capture_thread_ cannot be moved while running
        // the mutex, condition_variable, and ring_buffer_ should not be moved as they
        // represent running state, and a stream should not be moved while running

        capture_exception_ = other.capture_exception_;
        discontinuity_count_ = other.discontinuity_count_;
        ring_buffer_size_seconds_ = other.ring_buffer_size_seconds_;
    }
    return *this;
}

wasapi_audio_input_stream_impl::~wasapi_audio_input_stream_impl()
{
    if (audio_samples_ready_event_ != nullptr)
    {
        CloseHandle(audio_samples_ready_event_);
        audio_samples_ready_event_ = nullptr;
    }

    if (stop_capture_event_ != nullptr)
    {
        CloseHandle(stop_capture_event_);
        stop_capture_event_ = nullptr;
    }
}

#endif // WIN32

// **************************************************************** //
//                                                                  //
//                                                                  //
// audio_device                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

audio_device::audio_device()
{
}

#if WIN32

audio_device::audio_device(audio_device_impl* impl)
{
    impl_ = std::make_unique<audio_device_impl>();
    impl_->device_ = impl->device_;

    ensure_com_initialized();

    CComPtr<IMMEndpoint> endpoint;
    EDataFlow flow;
    if (FAILED(impl_->device_->QueryInterface(__uuidof(IMMEndpoint), reinterpret_cast<void**>(&endpoint))) || FAILED(endpoint->GetDataFlow(&flow)))
    {
        throw std::runtime_error("Failed to get data flow");
    }

    type = (flow == eRender) ? audio_device_type::render : audio_device_type::capture;

    state = audio_device_state::active;

    DWORD state_val = 0;
    if (FAILED(impl_->device_->GetState(&state_val)))
    {
        throw std::runtime_error("Failed to get device_ state");
    }

    switch (state_val)
    {
        case DEVICE_STATE_ACTIVE:
            this->state = audio_device_state::active;
            break;
        case DEVICE_STATE_DISABLED:
            this->state = audio_device_state::disabled;
            break;
        case DEVICE_STATE_UNPLUGGED:
            this->state = audio_device_state::unplugged;
            break;
        default:
            this->state = audio_device_state::not_present;
            break;
    }

    LPWSTR id_wstr = nullptr;
    if (SUCCEEDED(impl_->device_->GetId(&id_wstr)) && id_wstr)
    {
        id = utf16_to_utf8(id_wstr);
        CoTaskMemFree(id_wstr);
    }

    CComPtr<IPropertyStore> props;
    if (SUCCEEDED(impl_->device_->OpenPropertyStore(STGM_READ, &props)))
    {
        description = get_string_property(props.p, PKEY_Device_FriendlyName);
        name = get_string_property(props.p, PKEY_Device_DeviceDesc);
        container_id = get_guid_property(props.p, PKEY_Device_ContainerId);
    }
}

#endif // WIN32

#if __linux__

audio_device::audio_device(int card_id, int device_id, audio_device_type type)
{
    char hw_name[32];
    snprintf(hw_name, sizeof(hw_name), "hw:%d", card_id);
    
    char* card_name = nullptr;
    char* card_longname = nullptr;
    snd_card_get_name(card_id, &card_name);
    snd_card_get_longname(card_id, &card_longname);
    
    snd_ctl_t* ctl;
    if (snd_ctl_open(&ctl, hw_name, 0) < 0)
    {
        free(card_name);
        free(card_longname);
        return;
    }
    
    snd_pcm_info_t* pcm_info;
    snd_pcm_info_alloca(&pcm_info);
    snd_pcm_info_set_device(pcm_info, device_id);
    snd_pcm_info_set_subdevice(pcm_info, 0);
    
    bool has_playback = false;
    bool has_capture = false;
    
    // Check playback capability
    snd_pcm_info_set_stream(pcm_info, SND_PCM_STREAM_PLAYBACK);
    if (snd_ctl_pcm_info(ctl, pcm_info) >= 0)
    {
        has_playback = true;
    }
    
    // Check capture capability
    snd_pcm_info_set_stream(pcm_info, SND_PCM_STREAM_CAPTURE);
    if (snd_ctl_pcm_info(ctl, pcm_info) >= 0)
    {
        has_capture = true;
    }
    
    if (!has_playback && !has_capture)
    {
        snd_ctl_close(ctl);
        free(card_name);
        free(card_longname);
        return;
    }
    
    // Get name from whichever stream type is available
    snd_pcm_info_set_stream(pcm_info, has_playback ? SND_PCM_STREAM_PLAYBACK : SND_PCM_STREAM_CAPTURE);
    snd_ctl_pcm_info(ctl, pcm_info);
    
    id = "hw:" + std::to_string(card_id) + "," + std::to_string(device_id);
    name = snd_pcm_info_get_name(pcm_info);
    description = card_longname ? card_longname : (card_name ? card_name : "");
    state = audio_device_state::active;
    this->card_id = card_id;
    this->device_id = device_id;
    this->type = type;
    
    snd_ctl_close(ctl);
    free(card_name);
    free(card_longname);
}

#endif // __linux__

audio_device::audio_device(audio_device&& other) noexcept
{
    id = std::move(other.id);
    name = std::move(other.name);
    description = std::move(other.description);
    type = other.type;
    state = other.state;
    impl_ = std::move(other.impl_);

#if WIN32
    container_id = std::move(other.container_id);
#endif // WIN32
    
#if __linux__
    card_id = other.card_id;
    device_id = other.device_id;
#endif // __linux__
}

audio_device& audio_device::operator=(audio_device&& other) noexcept
{
    if (this != &other)
    {
        id = std::move(other.id);
        name = std::move(other.name);
        description = std::move(other.description);
        type = other.type;
        state = other.state;
        impl_ = std::move(other.impl_);

#if WIN32
        container_id = std::move(other.container_id);
#endif // WIN32
#if __linux__
        card_id = other.card_id;
        device_id = other.device_id;
#endif // __linux__
    }
    return *this;
}

std::unique_ptr<audio_stream_base> audio_device::stream()
{
#if WIN32
    if (!impl_)
    {
        throw std::runtime_error("Device not initialized");
    }

    if (type == audio_device_type::render)
    {
        return std::make_unique<wasapi_audio_output_stream>(impl_.get());
    }
    else if (type == audio_device_type::capture)
    {
        return std::make_unique<wasapi_audio_input_stream>(impl_.get());
    }
#endif // WIN32

#if __linux__
    return std::make_unique<alsa_audio_stream>(card_id, device_id, type);
#endif // __linux__

    return nullptr;
}

audio_device::~audio_device()
{
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// get_audio_devices                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<audio_device> get_audio_devices()
{
    std::vector<audio_device> devices;

#if WIN32
    HRESULT hr;

    ensure_com_initialized();

    CComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator)))
    {
        throw std::runtime_error("Failed to create enumerator");
    }

    CComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED | DEVICE_STATE_UNPLUGGED, &collection)))
    {
        throw std::runtime_error("Failed to get audio endpoints");
    }

    UINT count = 0;
    hr = collection->GetCount(&count);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to get devices count");
    }

    for (UINT i = 0; i < count; ++i)
    {
        CComPtr<IMMDevice> dev;
        if (FAILED(collection->Item(i, &dev)))
        {
            continue;
        }

        audio_device_impl dev_impl;
        dev_impl.device_ = dev.p;
        audio_device device(&dev_impl);

        devices.emplace_back(std::move(device));
    }
#endif // WIN32

#ifdef __linux__

    int card = -1;
    
    while (snd_card_next(&card) >= 0 && card >= 0)
    {
        char hw_name[32];
        snprintf(hw_name, sizeof(hw_name), "hw:%d", card);
        
        snd_ctl_t* ctl;
        if (snd_ctl_open(&ctl, hw_name, 0) < 0)
        {
            continue;
        }
        
        int device = -1;
        while (snd_ctl_pcm_next_device(ctl, &device) >= 0 && device >= 0)
        {
            snd_pcm_info_t* pcm_info;
            snd_pcm_info_alloca(&pcm_info);
            snd_pcm_info_set_device(pcm_info, device);
            snd_pcm_info_set_subdevice(pcm_info, 0);
        
            // Check playback
            snd_pcm_info_set_stream(pcm_info, SND_PCM_STREAM_PLAYBACK);
            if (snd_ctl_pcm_info(ctl, pcm_info) >= 0)
            {
                devices.emplace_back(card, device, audio_device_type::render);
            }
        
            // Check capture
            snd_pcm_info_set_stream(pcm_info, SND_PCM_STREAM_CAPTURE);
            if (snd_ctl_pcm_info(ctl, pcm_info) >= 0)
            {
                devices.emplace_back(card, device, audio_device_type::capture);
            }
        }
        
        snd_ctl_close(ctl);
    }

#endif // __linux__

    return devices;
}

std::vector<audio_device> get_audio_devices(audio_device_type type, audio_device_state state)
{
    std::vector<audio_device> all_devices = get_audio_devices();
    std::vector<audio_device> filtered_devices;
    for (auto&& device : all_devices)
    {
        if ((state == audio_device_state::unknown || device.state == state) &&
            (type == audio_device_type::unknown || device.type == type))
        {
            filtered_devices.emplace_back(std::move(device));
        }
    }
    return filtered_devices;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// try_get_audio_device_by_name                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool try_get_audio_device_by_name(const std::string& name, audio_device& device, audio_device_type type, audio_device_state state)
{
    std::vector<audio_device> devices = get_audio_devices();

    auto it = std::find_if(devices.begin(), devices.end(), [&](const audio_device& dev) {
        return dev.name == name && dev.type == type && dev.state == state;
    });

    if (it != devices.end())
    {
        device = std::move(*it);
        return true;
    }

    return false;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// try_get_audio_device_by_id                                       //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool try_get_audio_device_by_id(const std::string& id, audio_device& device)
{
    std::vector<audio_device> devices = get_audio_devices();

    auto it = std::find_if(devices.begin(), devices.end(), [&](const audio_device& dev) {
#if WIN32
        return dev.id == id;
#endif // WIN32
#ifdef __linux__
        int id_int = std::atoi(id.c_str());
        return dev.card_id == id_int;
#endif // __linux__
        return false;
    });

    if(it != devices.end())
    {
        device = std::move(*it);
        return true;
    }

    return false;
}

#ifdef __linux__

bool try_get_audio_device_by_id(int card_id, audio_device& device)
{
    std::vector<audio_device> devices = get_audio_devices();

    auto it = std::find_if(devices.begin(), devices.end(), [&](const audio_device& dev) {
        return dev.card_id == card_id;
    });

    if(it != devices.end())
    {
        device = std::move(*it);
        return true;
    }

    return false;
}

bool try_get_audio_device_by_id(int card_id, int device_id, audio_device& device)
{
    std::vector<audio_device> devices = get_audio_devices();

    auto it = std::find_if(devices.begin(), devices.end(), [&](const audio_device& dev) {
        return dev.card_id == card_id && dev.device_id == device_id;
    });

    if(it != devices.end())
    {
        device = std::move(*it);
        return true;
    }

    return false;
}

#endif // __linux__

// **************************************************************** //
//                                                                  //
//                                                                  //
// try_get_audio_device_by_description                              //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool try_get_audio_device_by_description(const std::string& description, audio_device& device, audio_device_type type, audio_device_state state)
{
    std::vector<audio_device> devices = get_audio_devices();

    auto it = std::find_if(devices.begin(), devices.end(), [&](const audio_device& dev) {
        return dev.description == description && dev.type == type && dev.state == state;
    });

    if (it != devices.end())
    {
        device = std::move(*it);
        return true;
    }

    return false;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// try_get_default_audio_device                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool try_get_default_audio_device(audio_device& device)
{
    return try_get_default_audio_device(device, audio_device_type::render);
}

bool try_get_default_audio_device(audio_device& device, audio_device_type type)
{
#if WIN32
    HRESULT hr;

    ensure_com_initialized();

    CComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator)))
    {
        return false;
    }

    CComPtr<IMMDevice> device_;

    EDataFlow flow = (type == audio_device_type::render) ? eRender : eCapture;
    hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device_);

    if (FAILED(hr))
    {
        return false;
    }

    audio_device_impl device_impl;
    device_impl.device_ = device_.p;

    device = audio_device(&device_impl);

    return true;
#endif // WIN32

    // Linux does not support default devices.
    // While certain distros might store a default, it is not generally standardized.

    return false;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// wasapi_audio_output_stream                                       //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if WIN32

wasapi_audio_output_stream::wasapi_audio_output_stream()
{
}

wasapi_audio_output_stream::wasapi_audio_output_stream(audio_device_impl* impl)
{
    impl_ = std::make_unique<wasapi_audio_output_stream_impl>();
    impl_->device_ = impl->device_;

    assert(impl_->audio_client_ == nullptr);
    assert(impl_->endpoint_volume_ == nullptr);
    assert(impl_->render_client_ == nullptr);

    ensure_com_initialized();

    HRESULT hr;

    CComPtr<IAudioClient> audio_client;
    CComPtr<IAudioEndpointVolume> endpoint_volume;
    CComPtr<IAudioRenderClient> render_client;

    if (FAILED(hr = impl_->device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audio_client)))
    {
        throw std::runtime_error("Failed to activate client");
    }

    if (FAILED(hr = impl_->device_->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)&endpoint_volume)))
    {
        throw std::runtime_error("Failed to get volume control");
    }

    WAVEFORMATEX* device_format = nullptr;
    if (FAILED(hr = audio_client->GetMixFormat(&device_format)))
    {
        throw std::runtime_error("Failed to get mix format");
    }

    if (!is_float32_format(device_format))
    {
        CoTaskMemFree(device_format);
        throw std::runtime_error("Unsupported audio format: only 32-bit float is supported");
    }

    sample_rate_ = static_cast<int>(device_format->nSamplesPerSec);
    channels_ = static_cast<int>(device_format->nChannels);

    hr = audio_client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        2000000,  // 200ms buffer
        0,
        device_format,
        nullptr);

    CoTaskMemFree(device_format);

    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to initialize audio client");
    }

    if (FAILED(hr = audio_client->GetService(__uuidof(IAudioRenderClient), (void**)&render_client)))
    {
        throw std::runtime_error("Failed to get render client");
    }

    if (FAILED(hr = audio_client->GetBufferSize(reinterpret_cast<UINT32*>(&buffer_size_))))
    {
        throw std::runtime_error("Failed to get buffer size");
    }

    impl_->audio_client_.Attach(audio_client.Detach());
    impl_->endpoint_volume_.Attach(endpoint_volume.Detach());
    impl_->render_client_.Attach(render_client.Detach());
}

wasapi_audio_output_stream::wasapi_audio_output_stream(wasapi_audio_output_stream&& other) noexcept
{
    impl_ = std::move(other.impl_);
    buffer_size_ = other.buffer_size_;
    sample_rate_ = other.sample_rate_;
    channels_ = other.channels_;
}

wasapi_audio_output_stream& wasapi_audio_output_stream::operator=(wasapi_audio_output_stream&& other) noexcept
{
    if (this != &other)
    {
        impl_ = std::move(other.impl_);
        buffer_size_ = other.buffer_size_;
        sample_rate_ = other.sample_rate_;
        channels_ = other.channels_;
    }
    return *this;
}

wasapi_audio_output_stream::~wasapi_audio_output_stream()
{
    close();
}

void wasapi_audio_output_stream::close()
{
    stop();

    impl_->render_client_.Release();
    impl_->endpoint_volume_.Release();
    impl_->audio_client_.Release();

    impl_.reset();
}

std::string wasapi_audio_output_stream::name()
{
    if (!impl_ || impl_->device_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    ensure_com_initialized();

    CComPtr<IPropertyStore> props;
    PROPVARIANT variant;

    HRESULT hr;
    if (FAILED(hr = impl_->device_->OpenPropertyStore(STGM_READ, &props)))
    {
        return "unknown";
    }

    PropVariantInit(&variant);
    
    if (FAILED(hr = props->GetValue(PKEY_Device_FriendlyName, &variant)) || variant.vt != VT_LPWSTR)
    {
        PropVariantClear(&variant);
        return "unknown";
    }

    std::string name = utf16_to_utf8(variant.pwszVal);

    PropVariantClear(&variant);
    
    return name;
}

void wasapi_audio_output_stream::mute(bool mute)
{
    if (!impl_ || impl_->endpoint_volume_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    ensure_com_initialized();

    BOOL mute_state = mute ? TRUE : FALSE;
    HRESULT hr = impl_->endpoint_volume_->SetMute(mute_state, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to set mute state");
    }
}

bool wasapi_audio_output_stream::mute()
{
    if (!impl_ || impl_->endpoint_volume_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    ensure_com_initialized();

    BOOL muted;
    HRESULT hr = impl_->endpoint_volume_->GetMute(&muted);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to get mute state");
    }
    return muted == TRUE;
}

void wasapi_audio_output_stream::volume(int percent)
{
    if (!impl_ || impl_->endpoint_volume_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    ensure_com_initialized();

    percent = std::clamp(percent, 0, 100);

    float volume_scalar = percent / 100.0f;

    HRESULT hr = impl_->endpoint_volume_->SetMasterVolumeLevelScalar(volume_scalar, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to set volume");
    }
}

int wasapi_audio_output_stream::volume()
{
    if (!impl_ || impl_->endpoint_volume_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    ensure_com_initialized();

    float volume_scalar;

    HRESULT hr = impl_->endpoint_volume_->GetMasterVolumeLevelScalar(&volume_scalar);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to get volume");
    }

    return static_cast<int>(volume_scalar * 100.0f + 0.5f);
}

int wasapi_audio_output_stream::sample_rate()
{
    return sample_rate_;
}

int wasapi_audio_output_stream::channels()
{
    return channels_;
}

size_t wasapi_audio_output_stream::write(const double* samples, size_t count)
{
    if (samples == nullptr || count == 0)
    {
        return 0;
    }

    // Convert mono to interleaved (duplicate to all channels)
    std::vector<double> interleaved(count * channels_);

    for (size_t i = 0; i < count; i++)
    {
        for (int channel = 0; channel < channels_; channel++)
        {
            // WASAPI uses interleaved (samples grouped by frame):
            // 
            // 0 * 2 + 0 = 0
            // 0 * 2 + 1 = 1
            // 1 * 2 + 0 = 2
            // 1 * 2 + 1 = 3
            //
            // frame0   frame1   frame2
            //  L0 R0    L1 R1    L2 R2
            // [s0,s0], [s1,s1], [s2,s2] for 2 channels

            interleaved[i * channels_ + channel] = samples[i];
        }
    }

    size_t samples_written = write_interleaved(interleaved.data(), interleaved.size());

    return samples_written / channels_;
}

size_t wasapi_audio_output_stream::write_interleaved(const double* samples, size_t count)
{
    if (!impl_ || !impl_->audio_client_ || !impl_->render_client_)
    {
        throw std::runtime_error("Stream not initialized");
    }

    if (samples == nullptr || count == 0)
    {
        return 0;
    }

    if (count % channels_ != 0)
    {
        throw std::invalid_argument("Sample count must be a multiple of channel count");
    }

    ensure_com_initialized();

    HRESULT hr;

    size_t total_frames = count / channels_;
    size_t frames_written = 0;

    while (frames_written < total_frames)
    {
        // Check available space in buffer
        // GetCurrentPadding gives the number of frames that are queued up to play
        // Wait until buffer space is available

        UINT32 padding;
        if (FAILED(hr = impl_->audio_client_->GetCurrentPadding(&padding)))
        {
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
            {
                throw std::runtime_error("Audio device disconnected");
            }
            throw std::runtime_error("Failed to get current padding");
        }

        UINT32 available_frames = static_cast<UINT32>(buffer_size_) - padding;
        if (available_frames == 0)
        {
            SwitchToThread();
            continue;
        }

        UINT32 frames_to_write = (std::min)(available_frames, static_cast<UINT32>(total_frames - frames_written));

        BYTE* buffer;
        if (FAILED(hr = impl_->render_client_->GetBuffer(frames_to_write, &buffer)))
        {
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
            {
                throw std::runtime_error("Audio device disconnected");
            }
            throw std::runtime_error("Failed to get buffer");
        }

        if (buffer == nullptr)
        {
            throw std::runtime_error("Received null buffer from render client");
        }

        float* float_buffer = reinterpret_cast<float*>(buffer);

        size_t offset = frames_written * channels_;
        size_t samples_to_copy = static_cast<size_t>(frames_to_write) * channels_;

        for (size_t i = 0; i < samples_to_copy; i++)
        {
            float_buffer[i] = static_cast<float>(samples[offset + i]);
        }

        if (FAILED(hr = impl_->render_client_->ReleaseBuffer(frames_to_write, 0)))
        {
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
            {
                throw std::runtime_error("Audio device disconnected");
            }
            throw std::runtime_error("Failed to release buffer");
        }

        frames_written += frames_to_write;
    }

    return frames_written * channels_;  // Return total samples written
}

size_t wasapi_audio_output_stream::read(double* samples, size_t count)
{
    // Not implemented for output streams
    (void)samples;
    (void)count;
    return 0;
}

size_t wasapi_audio_output_stream::read_interleaved(double* samples, size_t count)
{
    // Not implemented for output streams
    (void)samples;
    (void)count;
    return 0;
}

bool wasapi_audio_output_stream::wait_write_completed(int timeout_ms)
{
    if (!impl_ || impl_->audio_client_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    ensure_com_initialized();

    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    INT64 timeout_ticks = (static_cast<INT64>(timeout_ms) * freq.QuadPart) / 1000;

    while (true)
    {
        UINT32 padding;
        if (FAILED(impl_->audio_client_->GetCurrentPadding(&padding)))
        {
            return false;
        }

        if (padding == 0)
        {
            return true;
        }

        if (timeout_ms >= 0)
        {
            QueryPerformanceCounter(&now);
            if ((now.QuadPart - start.QuadPart) >= timeout_ticks)
            {
                return false;
            }
        }

        SwitchToThread();
    }
}

void wasapi_audio_output_stream::start()
{
    if (started_)
    {
        return;
    }

    if (!impl_ || impl_->audio_client_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    ensure_com_initialized();

    HRESULT hr;

    if (FAILED(hr = impl_->audio_client_->Start()))
    {
        throw std::runtime_error("Failed to start audio client");
    }

    started_ = true;
}

void wasapi_audio_output_stream::stop()
{
    if (!started_)
    {        
        return;
    }

    if (impl_)
    {
        ensure_com_initialized();

        if (impl_->audio_client_)
        {
            impl_->audio_client_->Stop();
        }

        started_ = false;
    }
}

#endif // WIN32

// **************************************************************** //
//                                                                  //
//                                                                  //
// wasapi_input_output_stream                                       //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if WIN32

wasapi_audio_input_stream::wasapi_audio_input_stream()
{
}

wasapi_audio_input_stream::wasapi_audio_input_stream(audio_device_impl* impl)
{
    impl_ = std::make_unique<wasapi_audio_input_stream_impl>();
    impl_->device_ = impl->device_;

    assert(impl_->audio_client_ == nullptr);
    assert(impl_->endpoint_volume_ == nullptr);
    assert(impl_->capture_client_ == nullptr);

    ensure_com_initialized();

    // Create the audio samples ready event and the stop signaling event
    // The audio samples ready event is used by WASAPI to signal when samples are available for capture
    // Use unique_ptr with a custom deleter to ensure handles are closed if an exception is thrown using RAII

    HANDLE audio_samples_ready_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (audio_samples_ready_event == nullptr)
    {
        throw std::runtime_error("Failed to create audio samples ready event");
    }

    std::unique_ptr<void, void(*)(void*)> audio_samples_ready_event_guard(audio_samples_ready_event, [](void* h) { if (h) CloseHandle(h); });

    HANDLE stop_capture_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (stop_capture_event == nullptr)
    {
        throw std::runtime_error("Failed to create stop capture event");
    }

    std::unique_ptr<void, void(*)(void*)> stop_capture_event_guard(stop_capture_event, [](void* h) { if (h) CloseHandle(h); });

    // Aquire IAudioClient, IAudioEndpointVolume, and IAudioCaptureClient interfaces
    // Retrieve the hardware format and ensure it is 32-bit float, if not throw
    // Keep the sample rate and channel count default, just retrieve them from the format

    HRESULT hr;

    CComPtr<IAudioClient> audio_client;
    CComPtr<IAudioEndpointVolume> endpoint_volume;
    CComPtr<IAudioCaptureClient> capture_client;

    if (FAILED(hr = impl_->device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audio_client)))
    {
        throw std::runtime_error("Failed to activate client");
    }

    CComPtr<IAudioSessionControl> session_control;
    if (SUCCEEDED(audio_client->GetService(__uuidof(IAudioSessionControl), (void**)&session_control)))
    {
        CComPtr<IAudioSessionControl2> session_control2;
        if (SUCCEEDED(session_control->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&session_control2)))
        {
            session_control2->SetDuckingPreference(TRUE); // Disable ducking
        }
    }

    if (FAILED(hr = impl_->device_->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)&endpoint_volume)))
    {
        throw std::runtime_error("Failed to get volume control");
    }

    WAVEFORMATEX* device_format = nullptr;
    if (FAILED(hr = audio_client->GetMixFormat(&device_format)))
    {
        throw std::runtime_error("Failed to get mix format");
    }

    if (!is_float32_format(device_format))
    {
        CoTaskMemFree(device_format);
        throw std::runtime_error("Unsupported audio format: only 32-bit float is supported");
    }

    sample_rate_ = static_cast<int>(device_format->nSamplesPerSec);
    channels_ = static_cast<int>(device_format->nChannels);

    hr = audio_client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        2000000,  // 200ms buffer, expressed in 100-ns units.
        0,
        device_format,
        nullptr);

    CoTaskMemFree(device_format);

    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to initialize");
    }

    if (FAILED(hr = audio_client->SetEventHandle(audio_samples_ready_event)))
    {
        throw std::runtime_error("Failed to set event handle");
    }

    if (FAILED(hr = audio_client->GetService(__uuidof(IAudioCaptureClient), (void**)&capture_client)))
    {
        throw std::runtime_error("Failed to get capture client");
    }

    if (FAILED(hr = audio_client->GetBufferSize(reinterpret_cast<UINT32*>(&buffer_size_))))
    {
        throw std::runtime_error("Failed to get buffer size");
    }

    // Initialize ring buffer to hold ~5 second of audio
    size_t ring_buffer_size = sample_rate_ * channels_ * impl_->ring_buffer_size_seconds_;
    impl_->ring_buffer_.set_capacity(ring_buffer_size);

    impl_->audio_client_.Attach(audio_client.Detach());
    impl_->endpoint_volume_.Attach(endpoint_volume.Detach());
    impl_->capture_client_.Attach(capture_client.Detach());

    impl_->audio_samples_ready_event_ = audio_samples_ready_event_guard.release();
    impl_->stop_capture_event_ = stop_capture_event_guard.release();
}

wasapi_audio_input_stream::wasapi_audio_input_stream(wasapi_audio_input_stream&& other) noexcept
{
    impl_ = std::move(other.impl_);
    buffer_size_ = other.buffer_size_;
    sample_rate_ = other.sample_rate_;
    channels_ = other.channels_;
}

wasapi_audio_input_stream& wasapi_audio_input_stream::operator=(wasapi_audio_input_stream&& other) noexcept
{
    if (this != &other)
    {
        impl_ = std::move(other.impl_);
        buffer_size_ = other.buffer_size_;
        sample_rate_ = other.sample_rate_;
        channels_ = other.channels_;
    }
    return *this;
}

wasapi_audio_input_stream::~wasapi_audio_input_stream()
{
    close();
}

void wasapi_audio_input_stream::close()
{
    stop();

    if (impl_)
    {
        impl_->capture_client_.Release();
        impl_->endpoint_volume_.Release();
        impl_->audio_client_.Release();
        impl_.reset();
    }
}

void wasapi_audio_input_stream::start()
{
    if (started_)
    {
        return;
    }

    if (!impl_ || impl_->audio_client_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    ensure_com_initialized();

    // Start the capture thread
    // The capture thread will read audio data from the WASAPI capture client and place it in a ring buffer
    // After we start the capture thread, we start the audio client to begin capturing audio

    impl_->capture_thread_ = std::jthread(std::bind(&wasapi_audio_input_stream::run, this, std::placeholders::_1));

    // The capture thread is now running, and awaiting for the WASAPI audio samples ready event to be signaled
    // It does not matter if the audio client is started after the thread is started
    // Now we start the audio client

    HRESULT hr;

    if (FAILED(hr = impl_->audio_client_->Start()))
    {
        impl_->capture_thread_.request_stop();
        SetEvent(impl_->stop_capture_event_);
        impl_->capture_thread_.join();
        throw std::runtime_error("Failed to start");
    }

    started_ = true;
}

void wasapi_audio_input_stream::run(std::stop_token stop_token)
{
    try
    {
        run_internal(stop_token);
    }
    catch (...)
    {
        // Swallow exceptions to prevent std::terminate from being called
        std::lock_guard<std::mutex> lock(impl_->buffer_mutex_);
        impl_->capture_exception_ = std::current_exception();
        impl_->buffer_cv_.notify_all();
    }
}

void wasapi_audio_input_stream::run_internal(std::stop_token stop_token)
{
    ensure_com_initialized();

    // Enable MMCSS for low-latency audio
    // Multimedia Class Scheduler Service (MMCSS) allows us to prioritize the audio capture thread
    // to reduce latency and improve audio quality
    // If we fail to set MMCSS, we proceed without it

    DWORD task_index = 0;
    HANDLE mmcss_handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
    assert(mmcss_handle != nullptr);
    if (mmcss_handle != nullptr)
    {
         BOOL result = AvSetMmThreadPriority(mmcss_handle, AVRT_PRIORITY_CRITICAL);
         assert(result != FALSE);
         (void)result;
    }

    // MMCSS is used for the lifetime of the capture thread
    // When the thread exits, we free the MMCSS handle using the unique_ptr custom deleter
    std::unique_ptr<void, void(*)(void*)> mmcss_guard(mmcss_handle, [](void* h) { if (h) AvRevertMmThreadCharacteristics(h); });
    (void)mmcss_guard;

    HRESULT hr;

    HANDLE wait_array[2] = { impl_->stop_capture_event_, impl_->audio_samples_ready_event_ };

    // The capture thread runs continuously, until stop is requested
    // Or if we encounter an error
    // Before we retrieve available audio data, we wait for WASAPI to signal us that data is available
    // This avoids busy-waiting and reduces CPU usage
    // If data is available, we drain all available (audio) packets from the WASAPI capture buffer
    // And store them in our ring buffer for later retrieval

    while (!stop_token.stop_requested())
    {
        DWORD wait_result = WaitForMultipleObjects(2, wait_array, FALSE, INFINITE);

        switch (wait_result)
        {
            case WAIT_OBJECT_0: // Stop event signaled
                return;

            case WAIT_OBJECT_0 + 1: // Audio data available
                break;

            case WAIT_FAILED:
            default:
                throw std::runtime_error("WaitForMultipleObjects failed");
        }

        // Drain all available packets
        UINT32 packet_size = 0;

        if (FAILED(hr = impl_->capture_client_->GetNextPacketSize(&packet_size)))
        {
            throw std::runtime_error("Failed to get next packet size");
        }

        while (packet_size > 0)
        {
            BYTE* buffer = nullptr;
            UINT32 frames_available = 0;
            DWORD flags = 0;

            hr = impl_->capture_client_->GetBuffer(&buffer, &frames_available, &flags, nullptr, nullptr);

            if (hr == AUDCLNT_S_BUFFER_EMPTY)
            {
                throw std::runtime_error("Capture buffer is empty");
            }

            if (hr == AUDCLNT_E_OUT_OF_ORDER)
            {
                impl_->capture_client_->ReleaseBuffer(frames_available);
                continue;
            }

            if (FAILED(hr))
            {
                throw std::runtime_error("Failed to get capture buffer");
            }

            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
            {
                impl_->discontinuity_count_++;
            }

            size_t samples_count = frames_available * channels_;

            {
                std::lock_guard<std::mutex> lock(impl_->buffer_mutex_);

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                {
                    for (size_t i = 0; i < samples_count; i++)
                    {
                        impl_->ring_buffer_.push_back(0.0f);
                    }
                }
                else
                {
                    const float* captured_samples = reinterpret_cast<const float*>(buffer);
                    for (size_t i = 0; i < samples_count; i++)
                    {
                        impl_->ring_buffer_.push_back(captured_samples[i]);
                    }
                }
            }

            impl_->buffer_cv_.notify_one();

            if (FAILED(impl_->capture_client_->ReleaseBuffer(frames_available)))
            {
                throw std::runtime_error("Failed to release capture buffer");
            }

            if (stop_token.stop_requested())
            {
                break;
            }

            if (FAILED(hr = impl_->capture_client_->GetNextPacketSize(&packet_size)))
            {
                throw std::runtime_error("Failed to get next packet size");
            }
        }
    }
}

void wasapi_audio_input_stream::stop()
{
    if (!started_)
    {
        return;
    }

    if (impl_)
    {
        ensure_com_initialized();

        impl_->capture_thread_.request_stop();

        // Signal the stop event to unblock any waiting read operations
        if (impl_->stop_capture_event_ != nullptr)
        {
            SetEvent(impl_->stop_capture_event_);
        }

        impl_->buffer_cv_.notify_all();

        impl_->capture_thread_.join();

        if (impl_->audio_client_)
        {
            impl_->audio_client_->Stop();
        }

        started_ = false;
    }
}

bool wasapi_audio_input_stream::faulted()
{
    if (!impl_)
    {
        throw std::runtime_error("Stream not initialized");
    }

    std::lock_guard<std::mutex> lock(impl_->buffer_mutex_);
    return impl_->capture_exception_ != nullptr;
}

void wasapi_audio_input_stream::throw_if_faulted()
{
    if (!impl_)
    {
        throw std::runtime_error("Stream not initialized");
    }

    std::lock_guard<std::mutex> lock(impl_->buffer_mutex_);
    if (impl_->capture_exception_)
    {
        std::exception_ptr ex = impl_->capture_exception_;
        impl_->capture_exception_ = nullptr;
        std::rethrow_exception(ex);
    }
}

void wasapi_audio_input_stream::flush()
{
    if (impl_)
    {
        std::lock_guard<std::mutex> lock(impl_->buffer_mutex_);
        impl_->ring_buffer_.clear();
    }
}

std::string wasapi_audio_input_stream::name()
{
    if (!impl_ || impl_->device_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    ensure_com_initialized();

    CComPtr<IPropertyStore> props;
    PROPVARIANT variant;

    HRESULT hr;
    if (FAILED(hr = impl_->device_->OpenPropertyStore(STGM_READ, &props)))
    {
        return "unknown";
    }

    PropVariantInit(&variant);

    if (FAILED(hr = props->GetValue(PKEY_Device_FriendlyName, &variant)) || variant.vt != VT_LPWSTR)
    {
        PropVariantClear(&variant);
        return "unknown";
    }

    std::string name = utf16_to_utf8(variant.pwszVal);

    PropVariantClear(&variant);

    return name;
}

void wasapi_audio_input_stream::mute(bool mute)
{
    if (!impl_ || impl_->endpoint_volume_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    ensure_com_initialized();

    BOOL mute_state = mute ? TRUE : FALSE;
    HRESULT hr = impl_->endpoint_volume_->SetMute(mute_state, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to set mute state");
    }
}

bool wasapi_audio_input_stream::mute()
{
    if (!impl_ || impl_->endpoint_volume_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    ensure_com_initialized();

    BOOL muted;
    HRESULT hr = impl_->endpoint_volume_->GetMute(&muted);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to get mute state");
    }
    return muted == TRUE;
}

void wasapi_audio_input_stream::volume(int percent)
{
    if (!impl_ || impl_->endpoint_volume_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    ensure_com_initialized();

    percent = std::clamp(percent, 0, 100);

    float volume_scalar = percent / 100.0f;

    HRESULT hr = impl_->endpoint_volume_->SetMasterVolumeLevelScalar(volume_scalar, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to set volume");
    }
}

int wasapi_audio_input_stream::volume()
{
    if (!impl_ || impl_->endpoint_volume_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    ensure_com_initialized();

    float volume_scalar;

    HRESULT hr = impl_->endpoint_volume_->GetMasterVolumeLevelScalar(&volume_scalar);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to get volume");
    }

    return static_cast<int>(volume_scalar * 100.0f + 0.5f);
}

int wasapi_audio_input_stream::sample_rate()
{
    return sample_rate_;
}

int wasapi_audio_input_stream::channels()
{
    return static_cast<int>(channels_);
}

size_t wasapi_audio_input_stream::write(const double* samples, size_t count)
{
    (void)samples;
    (void)count;
    return 0;
}

size_t wasapi_audio_input_stream::write_interleaved(const double* samples, size_t count)
{
    (void)samples;
    (void)count;
    return 0;
}

size_t wasapi_audio_input_stream::read(double* samples, size_t count)
{
    if (samples == nullptr || count == 0)
    {
        return 0;
    }

    std::vector<double> interleaved_buffer(count * channels_);

    size_t frames_read = read_interleaved(interleaved_buffer.data(), count);

    for (size_t i = 0; i < frames_read; i++)
    {
        samples[i] = interleaved_buffer[i * channels_]; // Always take the first channel
    }

    return frames_read;
}

size_t wasapi_audio_input_stream::read_interleaved(double* samples, size_t count)
{
    if (!impl_)
    {
        throw std::runtime_error("Stream not initialized");
    }

    if (samples == nullptr || count == 0)
    {
        return 0;
    }

    size_t samples_needed = count * channels_; // just count!
    size_t samples_read = 0;

    std::unique_lock<std::mutex> lock(impl_->buffer_mutex_);

    // Wait until we have enough data or stopped
    impl_->buffer_cv_.wait(lock, [&]() {
        return !impl_->ring_buffer_.empty() || !started_ || impl_->capture_exception_ != nullptr;
    });

    if (impl_->capture_exception_)
    {
        std::rethrow_exception(impl_->capture_exception_);
    }

    if (!started_ && impl_->ring_buffer_.empty())
    {
        return 0;
    }

    // Read available samples (up to requested amount)
    size_t available = (std::min)(impl_->ring_buffer_.size(), samples_needed);
    for (size_t i = 0; i < available; i++)
    {
        samples[i] = static_cast<double>(impl_->ring_buffer_.front());
        impl_->ring_buffer_.pop_front();
    }

    return available / channels_;
}

bool wasapi_audio_input_stream::wait_write_completed(int timeout_ms)
{
    (void)timeout_ms;
    return true;
}

#endif // WIN32

// **************************************************************** //
//                                                                  //
//                                                                  //
// alsa_audio_stream                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if __linux__

alsa_audio_stream::alsa_audio_stream() : card_id(-1), device_id(-1), pcm_handle_(nullptr), sample_rate_(48000), num_channels_(1)
{
}

alsa_audio_stream::alsa_audio_stream(int card_id, int device_id, audio_device_type type) : card_id(card_id), device_id(device_id), pcm_handle_(nullptr), sample_rate_(48000), num_channels_(1), type(type)
{
    std::string device_name = "plughw:" + std::to_string(card_id) + "," + std::to_string(device_id);

    int err;
    if (type == audio_device_type::render)
    {
        err = snd_pcm_open(&pcm_handle_, device_name.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    }
    else if (type == audio_device_type::capture)
    {
        err = snd_pcm_open(&pcm_handle_, device_name.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    }

    if (err < 0)
    {
        throw std::runtime_error("Could not open playback or capture device_");
    }
    
    // Configure hardware parameters
    snd_pcm_hw_params_t* hw_params;
    snd_pcm_hw_params_alloca(&hw_params);
    
    err = snd_pcm_hw_params_any(pcm_handle_, hw_params);
    if (err < 0)
    {
        throw std::runtime_error(std::string("hw_params_any: ") + snd_strerror(err));
    }

    err = snd_pcm_hw_params_set_access(pcm_handle_, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_set_access: ") + snd_strerror(err));
    }

    err = snd_pcm_hw_params_set_rate_resample(pcm_handle_, hw_params, 1);
    if (err < 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_set_rate_resample: ") + snd_strerror(err));
    }

    unsigned int channels = 2;
    err = snd_pcm_hw_params_set_channels_near(pcm_handle_, hw_params, &channels);
    if (err < 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_set_channels_near: ") + snd_strerror(err));
    }
    
    // Prefer float32; fall back to s16
    snd_pcm_format_t fmt = SND_PCM_FORMAT_FLOAT_LE;
    err = snd_pcm_hw_params_set_format(pcm_handle_, hw_params, fmt);
    if (err < 0)
    {
        fmt = SND_PCM_FORMAT_S16_LE;
        err = snd_pcm_hw_params_set_format(pcm_handle_, hw_params, fmt);
        if (err < 0)
        {
            throw std::runtime_error(std::string("snd_pcm_hw_params_set_format: ") + snd_strerror(err));
        }
    }
    
    unsigned int rate = sample_rate_;
    err = snd_pcm_hw_params_set_rate_near(pcm_handle_, hw_params, &rate, nullptr);
    if (err < 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_set_rate_near: ") + snd_strerror(err));
    }
    
    // Set buffer size (~100ms) and period size (~10ms) in *frames*
    snd_pcm_uframes_t buffer_size = static_cast<snd_pcm_uframes_t>(rate / 10);
    snd_pcm_uframes_t period_size = static_cast<snd_pcm_uframes_t>(rate / 100);

    err = snd_pcm_hw_params_set_buffer_size_near(pcm_handle_, hw_params, &buffer_size);
    if (err < 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_set_buffer_size_near: ") + snd_strerror(err));
    }

    err = snd_pcm_hw_params_set_period_size_near(pcm_handle_, hw_params, &period_size, nullptr);
    if (err < 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_set_period_size_near: ") + snd_strerror(err));
    }
    
    err = snd_pcm_hw_params(pcm_handle_, hw_params);
    if (err < 0)
    {
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
        throw std::runtime_error("Could not set snd_pcm_hw_params");
    }

     // Read back what ALSA actually set
    unsigned int actual_channels = 0;
    unsigned int actual_rate = 0;
    format_ = SND_PCM_FORMAT_UNKNOWN;

    err = snd_pcm_hw_params_get_channels(hw_params, &actual_channels);
    if (err < 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_get_channels: ") + snd_strerror(err));
    }

    err = snd_pcm_hw_params_get_rate(hw_params, &actual_rate, nullptr);
    if (err < 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_get_rate: ") + snd_strerror(err));
    }

    err = snd_pcm_hw_params_get_format(hw_params, &format_);
    if (err < 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_get_format: ") + snd_strerror(err));
    }

    num_channels_ = static_cast<int>(actual_channels);
    sample_rate_ = static_cast<int>(actual_rate);
    
    err = snd_pcm_prepare(pcm_handle_);
    if (err < 0)
    {
        throw std::runtime_error(std::string("snd_pcm_prepare: ") + snd_strerror(err));
    }
}

alsa_audio_stream::alsa_audio_stream(const alsa_audio_stream& other) : card_id(other.card_id), device_id(other.device_id), pcm_handle_(nullptr), sample_rate_(other.sample_rate_), num_channels_(other.num_channels_), type(other.type)
{
    if (other.pcm_handle_)
    {
        // Re-open the device
        *this = alsa_audio_stream(card_id, device_id, type);
    }
}

alsa_audio_stream& alsa_audio_stream::operator=(const alsa_audio_stream& other)
{
    if (this != &other)
    {
        if (pcm_handle_)
        {
            snd_pcm_close(pcm_handle_);
            pcm_handle_ = nullptr;
        }
        
        card_id = other.card_id;
        device_id = other.device_id;
        sample_rate_ = other.sample_rate_;
        num_channels_ = other.num_channels_;
        type = other.type;
        
        if (other.pcm_handle_)
        {
            *this = alsa_audio_stream(card_id, device_id, type);
        }
    }
    return *this;
}

alsa_audio_stream::~alsa_audio_stream()
{
    close();
}

void alsa_audio_stream::close()
{
    if (pcm_handle_)
    {
        snd_pcm_drain(pcm_handle_);
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
    }
}

std::string alsa_audio_stream::name()
{
    return "hw:" + std::to_string(card_id) + "," + std::to_string(device_id);
}

void alsa_audio_stream::mute(bool mute)
{
    if (card_id < 0)
        return;
    
    snd_mixer_t* mixer;
    if (snd_mixer_open(&mixer, 0) < 0)
        return;
    
    std::string card_name = "hw:" + std::to_string(card_id);
    if (snd_mixer_attach(mixer, card_name.c_str()) < 0)
    {
        snd_mixer_close(mixer);
        return;
    }
    
    snd_mixer_selem_register(mixer, nullptr, nullptr);
    snd_mixer_load(mixer);
    
    // Iterate through all mixer elements
    for (snd_mixer_elem_t* elem = snd_mixer_first_elem(mixer); 
         elem != nullptr; 
         elem = snd_mixer_elem_next(elem))
    {
        if (!snd_mixer_selem_is_active(elem))
            continue;
        
        if (snd_mixer_selem_has_playback_switch(elem))
        {
            snd_mixer_selem_set_playback_switch_all(elem, mute ? 0 : 1);
        }
        
        if (snd_mixer_selem_has_capture_switch(elem))
        {
            snd_mixer_selem_set_capture_switch_all(elem, mute ? 0 : 1);
        }
    }
    
    snd_mixer_close(mixer);
}

bool alsa_audio_stream::mute()
{
    if (card_id < 0)
        return false;
    
    snd_mixer_t* mixer;
    if (snd_mixer_open(&mixer, 0) < 0)
        return false;
    
    std::string card_name = "hw:" + std::to_string(card_id);
    if (snd_mixer_attach(mixer, card_name.c_str()) < 0)
    {
        snd_mixer_close(mixer);
        return false;
    }
    
    snd_mixer_selem_register(mixer, nullptr, nullptr);
    snd_mixer_load(mixer);
    
    bool is_muted = true;
    bool found_any = false;
    
    // Check if any element is unmuted
    for (snd_mixer_elem_t* elem = snd_mixer_first_elem(mixer); 
         elem != nullptr; 
         elem = snd_mixer_elem_next(elem))
    {
        if (!snd_mixer_selem_is_active(elem))
            continue;
        
        if (snd_mixer_selem_has_playback_switch(elem))
        {
            int switch_value;
            if (snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_MONO, &switch_value) >= 0)
            {
                found_any = true;
                if (switch_value != 0)
                {
                    is_muted = false;
                    break;
                }
            }
        }
        
        if (snd_mixer_selem_has_capture_switch(elem))
        {
            int switch_value;
            if (snd_mixer_selem_get_capture_switch(elem, SND_MIXER_SCHN_MONO, &switch_value) >= 0)
            {
                found_any = true;
                if (switch_value != 0)
                {
                    is_muted = false;
                    break;
                }
            }
        }
    }
    
    snd_mixer_close(mixer);
    
    // If no switches found, assume not muted
    return found_any ? is_muted : false;
}

void alsa_audio_stream::volume(int percent)
{
    if (card_id < 0)
        return;
    
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    
    snd_mixer_t* mixer;
    if (snd_mixer_open(&mixer, 0) < 0)
        return;
    
    std::string card_name = "hw:" + std::to_string(card_id);
    if (snd_mixer_attach(mixer, card_name.c_str()) < 0)
    {
        snd_mixer_close(mixer);
        return;
    }
    
    snd_mixer_selem_register(mixer, nullptr, nullptr);
    snd_mixer_load(mixer);
    
    // Set volume on all mixer elements that support it
    for (snd_mixer_elem_t* elem = snd_mixer_first_elem(mixer); 
         elem != nullptr; 
         elem = snd_mixer_elem_next(elem))
    {
        if (!snd_mixer_selem_is_active(elem))
            continue;
        
        if (snd_mixer_selem_has_playback_volume(elem))
        {
            long min, max;
            snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
            long volume = min + (max - min) * percent / 100;
            snd_mixer_selem_set_playback_volume_all(elem, volume);
        }
        
        if (snd_mixer_selem_has_capture_volume(elem))
        {
            long min, max;
            snd_mixer_selem_get_capture_volume_range(elem, &min, &max);
            long volume = min + (max - min) * percent / 100;
            snd_mixer_selem_set_capture_volume_all(elem, volume);
        }
    }
    
    snd_mixer_close(mixer);
}

int alsa_audio_stream::volume()
{
    if (card_id < 0)
        return 0;
    
    snd_mixer_t* mixer;
    if (snd_mixer_open(&mixer, 0) < 0)
        return 0;
    
    std::string card_name = "hw:" + std::to_string(card_id);
    if (snd_mixer_attach(mixer, card_name.c_str()) < 0)
    {
        snd_mixer_close(mixer);
        return 0;
    }
    
    snd_mixer_selem_register(mixer, nullptr, nullptr);
    snd_mixer_load(mixer);
    
    int percent = 0;
    bool found = false;
    
    // Get volume from first element that supports it
    for (snd_mixer_elem_t* elem = snd_mixer_first_elem(mixer); 
         elem != nullptr && !found; 
         elem = snd_mixer_elem_next(elem))
    {
        if (!snd_mixer_selem_is_active(elem))
            continue;
        
        if (snd_mixer_selem_has_playback_volume(elem))
        {
            long min, max, volume;
            snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
            if (snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_MONO, &volume) >= 0)
            {
                if (max > min)
                {
                    percent = static_cast<int>(100 * (volume - min) / (max - min));
                }
                found = true;
            }
        }
        else if (snd_mixer_selem_has_capture_volume(elem))
        {
            long min, max, volume;
            snd_mixer_selem_get_capture_volume_range(elem, &min, &max);
            if (snd_mixer_selem_get_capture_volume(elem, SND_MIXER_SCHN_MONO, &volume) >= 0)
            {
                if (max > min)
                {
                    percent = static_cast<int>(100 * (volume - min) / (max - min));
                }
                found = true;
            }
        }
    }
    
    snd_mixer_close(mixer);
    return percent;
}

size_t alsa_audio_stream::write(const double* samples, size_t frames)
{
    if (!pcm_handle_ || !samples || frames == 0 || num_channels_ <= 0)
    {
        return 0;
    }

    const size_t channels = static_cast<size_t>(num_channels_);
    size_t total_written = 0;

    if (format_ == SND_PCM_FORMAT_FLOAT_LE)
    {
        buffer.resize(frames * channels);

        for (size_t i = 0; i < frames; ++i)
        {
            float v = static_cast<float>(std::clamp(samples[i], -1.0, 1.0));
            for (size_t ch = 0; ch < channels; ++ch)
            {
                buffer[i * channels + ch] = v;
            }
        }

        while (total_written < frames)
        {
            snd_pcm_sframes_t n = snd_pcm_writei(
                pcm_handle_,
                buffer.data() + (total_written * channels),
                frames - total_written);

            if (n == -EAGAIN)
            {
                snd_pcm_wait(pcm_handle_, 1000);
                continue;
            }

            if (n == -EPIPE)
            {
                int err = snd_pcm_prepare(pcm_handle_);
                if (err < 0)
                {
                    throw std::runtime_error(std::string("snd_pcm_prepare: ") + snd_strerror(err));
                }
                continue;
            }

            if (n < 0)
            {
                n = snd_pcm_recover(pcm_handle_, static_cast<int>(n), 1);
                if (n < 0)
                {
                    throw std::runtime_error(std::string("snd_pcm_recover: ") + snd_strerror(static_cast<int>(n)));
                }
                continue;
            }

            total_written += static_cast<size_t>(n);
        }
    }
    else if (format_ == SND_PCM_FORMAT_S16_LE)
    {
        s16_buffer.resize(frames * channels);

        for (size_t i = 0; i < frames; ++i)
        {
            int16_t v = static_cast<int16_t>(std::clamp(samples[i], -1.0, 1.0) * 32767.0);
            for (size_t ch = 0; ch < channels; ++ch)
            {
                s16_buffer[i * channels + ch] = v;
            }
        }

        while (total_written < frames)
        {
            snd_pcm_sframes_t n = snd_pcm_writei(
                pcm_handle_,
                s16_buffer.data() + (total_written * channels),
                frames - total_written);

            if (n == -EAGAIN)
            {
                snd_pcm_wait(pcm_handle_, 1000);
                continue;
            }

            if (n == -EPIPE)
            {
                int err = snd_pcm_prepare(pcm_handle_);
                if (err < 0)
                {
                    throw std::runtime_error(std::string("snd_pcm_prepare: ") + snd_strerror(err));
                }
                continue;
            }

            if (n < 0)
            {
                n = snd_pcm_recover(pcm_handle_, static_cast<int>(n), 1);
                if (n < 0)
                {
                    throw std::runtime_error(std::string("snd_pcm_recover: ") + snd_strerror(static_cast<int>(n)));
                }
                continue;
            }

            total_written += static_cast<size_t>(n);
        }
    }
    else
    {
        throw std::runtime_error(std::string("Unsupported audio format: ") + snd_pcm_format_name(format_));
    }

    return total_written;
}

size_t alsa_audio_stream::write_interleaved(const double* samples, size_t count)
{
    return 0;
}

bool alsa_audio_stream::wait_write_completed(int timeout_ms)
{
    if (pcm_handle_)
    {
        snd_pcm_drain(pcm_handle_);
        snd_pcm_prepare(pcm_handle_);
    }
    return true;
}

size_t alsa_audio_stream::read(double* samples, size_t count)
{
    return 0;
}

void alsa_audio_stream::start()
{
    if (pcm_handle_)
    {
        int err = snd_pcm_start(pcm_handle_);
        if (err < 0)
        {
            throw std::runtime_error(std::string("snd_pcm_prepare: ") + snd_strerror(err));
        }
    }
}

void alsa_audio_stream::stop()
{
    if (pcm_handle_)
    {
        snd_pcm_drop(pcm_handle_);
        snd_pcm_prepare(pcm_handle_);
    }
}

int alsa_audio_stream::sample_rate()
{
    return sample_rate_;
}

int alsa_audio_stream::channels()
{
    return static_cast<int>(num_channels_);
}

#endif // __linux__

// **************************************************************** //
//                                                                  //
//                                                                  //
// wav_audio_impl                                                   //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct wav_audio_impl
{
    SNDFILE* sf_file_ = nullptr;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// wav_audio_input_stream                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

wav_audio_input_stream::wav_audio_input_stream(const std::string& filename) : filename_(filename)
{
    impl_ = std::make_unique<wav_audio_impl>();

    SF_INFO sfinfo = {};

    impl_->sf_file_ = sf_open(filename.c_str(), SFM_READ, &sfinfo);
    if (!impl_->sf_file_)
    {
        throw std::runtime_error(std::string("Failed to open WAV file: ") + sf_strerror(nullptr));
    }

    sample_rate_ = sfinfo.samplerate;
    channels_ = sfinfo.channels;

    if (channels_ != 1)
    {
        throw std::runtime_error("Only mono WAV files are supported for reading");
    }

    int type = sfinfo.format & SF_FORMAT_TYPEMASK;
    if (type != SF_FORMAT_WAV /* && type != SF_FORMAT_WAVEX && type != SF_FORMAT_RF64 */)
    {
        throw std::runtime_error("Not a WAV file (unsupported container): " + filename);
    }

    int sub = sfinfo.format & SF_FORMAT_SUBMASK;
    switch (sub)
    {
        case SF_FORMAT_PCM_16:
        case SF_FORMAT_PCM_24:
        case SF_FORMAT_PCM_32:
        case SF_FORMAT_FLOAT:
        case SF_FORMAT_DOUBLE:
            break; // ok
        default:
            throw std::runtime_error("Unsupported WAV encoding (not PCM/float): " + filename);
    }

    sf_command(impl_->sf_file_, SFC_SET_NORM_DOUBLE, nullptr, SF_TRUE);  // normalize to [-1, 1]
}

wav_audio_input_stream::wav_audio_input_stream(wav_audio_input_stream&& other) noexcept
{
    if (impl_ && impl_->sf_file_)
    {
        close();
    }
    impl_ = std::move(other.impl_);
    sample_rate_ = other.sample_rate_;
    channels_ = other.channels_;
    filename_ = std::move(other.filename_);
}

wav_audio_input_stream& wav_audio_input_stream::operator=(wav_audio_input_stream&& other) noexcept
{
    if (impl_ && impl_->sf_file_)
    {
        close();
    }
    impl_ = std::move(other.impl_);
    sample_rate_ = other.sample_rate_;
    channels_ = other.channels_;
    filename_ = std::move(other.filename_);
    return *this;
}

wav_audio_input_stream::~wav_audio_input_stream()
{
    if (impl_ && impl_->sf_file_ != nullptr)
    {
        sf_close(impl_->sf_file_);
        impl_->sf_file_ = nullptr;
    }
}

std::string wav_audio_input_stream::name()
{
    return filename_;
}

void wav_audio_input_stream::volume(int percent)
{
    // Not supported
    (void)percent;
}

int wav_audio_input_stream::volume()
{
    // Always 100% for this implementation
    return 100;
}

int wav_audio_input_stream::sample_rate()
{
    return sample_rate_;
}

int wav_audio_input_stream::channels()
{
    return channels_;
}

size_t wav_audio_input_stream::write(const double* samples, size_t count)
{
    // Not supported, read-only stream
    return 0;
}

size_t wav_audio_input_stream::write_interleaved(const double* samples, size_t count)
{
    // Not supported
    (void)samples;
    (void)count;
    return 0;
}

bool wav_audio_input_stream::wait_write_completed(int timeout_ms)
{
    // Not supported
    (void)timeout_ms;
    return true;
}

size_t wav_audio_input_stream::read(double* samples, size_t count)
{
    if (!impl_ || impl_->sf_file_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    sf_count_t total = 0;
    sf_count_t needed = static_cast<sf_count_t>(count);

    while (total < needed)
    {
        sf_count_t remaining = needed - total;
        sf_count_t n = sf_read_double(impl_->sf_file_, samples + total, remaining);
        if (n == 0)
        {
            if (sf_error(impl_->sf_file_) != SF_ERR_NO_ERROR)
            {
                throw std::runtime_error(std::string("WAV read error: ") + sf_strerror(impl_->sf_file_));
            }
            break; // EOF
        }
        if (n < 0)
        {
            throw std::runtime_error(std::string("WAV read error: ") + sf_strerror(impl_->sf_file_));
        }
        total += n;
    }

    return static_cast<size_t>(total);
}

size_t wav_audio_input_stream::read_interleaved(double* samples, size_t count)
{
    // Not supported
    (void)samples;
    (void)count;
    return 0;
}

void wav_audio_input_stream::flush()
{
    // Not supported
}

void wav_audio_input_stream::close()
{
    if (impl_ && impl_->sf_file_ != nullptr)
    {
        sf_close(impl_->sf_file_);
        impl_.reset();
    }
}

void wav_audio_input_stream::start()
{
    // Not supported
}

void wav_audio_input_stream::stop()
{
    // Not supported
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// wav_audio_output_stream                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

wav_audio_output_stream::wav_audio_output_stream(const std::string& filename, int sample_rate) : sample_rate_(sample_rate), filename_(filename)
{
    impl_ = std::make_unique<wav_audio_impl>();

    SF_INFO sfinfo = {};

    // Set format for writing
    sfinfo.samplerate = sample_rate;
    sfinfo.channels = channels_; // Always mono
    // Use PCM_16 for wider compatibility with software like Direwolf
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    impl_->sf_file_ = sf_open(filename.c_str(), SFM_WRITE, &sfinfo);
    if (!impl_->sf_file_)
    {
        throw std::runtime_error(std::string("Failed to open WAV file: ") + sf_strerror(nullptr));
    }
}

wav_audio_output_stream::wav_audio_output_stream(wav_audio_output_stream&& other) noexcept
{
    if (impl_ && impl_->sf_file_ != nullptr)
    {
        close();
    }
    impl_ = std::move(other.impl_);
    sample_rate_ = other.sample_rate_;
    filename_ = std::move(other.filename_);
}

wav_audio_output_stream& wav_audio_output_stream::operator=(wav_audio_output_stream&& other) noexcept
{
    if (impl_ && impl_->sf_file_ != nullptr)
    {
        close();
    }
    impl_ = std::move(other.impl_);
    sample_rate_ = other.sample_rate_;
    filename_ = std::move(other.filename_);
    return *this;
}

wav_audio_output_stream::~wav_audio_output_stream()
{
    if (impl_ && impl_->sf_file_ != nullptr)
    {
        sf_close(impl_->sf_file_);
        impl_->sf_file_ = nullptr;
    }
}

std::string wav_audio_output_stream::name()
{
    return filename_;
}

void wav_audio_output_stream::volume(int percent)
{
    // Not supported
    (void)percent;
}

int wav_audio_output_stream::volume()
{
    // Always 100% for this implementation
    return 100;
}

int wav_audio_output_stream::sample_rate()
{
    return sample_rate_;
}

int wav_audio_output_stream::channels()
{
    // Always 1 channel for this implementation
    // The value of channels_ is set in the class declaration
    return channels_;
}

size_t wav_audio_output_stream::write(const double* samples, size_t count)
{
    if (!impl_ || impl_->sf_file_ == nullptr)
    {
        throw std::runtime_error("Stream not initialized");
    }

    sf_count_t written = sf_write_double(impl_->sf_file_, samples, static_cast<sf_count_t>(count));
    if (written < 0)
    {
        throw std::runtime_error(std::string("Failed to write WAV: ") + sf_strerror(impl_->sf_file_));
    }

    return static_cast<size_t>(written);
}

size_t wav_audio_output_stream::write_interleaved(const double* samples, size_t count)
{
    // Not supported
    (void)samples;
    (void)count;
    return 0;
}

bool wav_audio_output_stream::wait_write_completed(int timeout_ms)
{
    // Not supported
    (void)timeout_ms;
    return true;
}

size_t wav_audio_output_stream::read(double* samples, size_t count)
{
    return 0;
}

size_t wav_audio_output_stream::read_interleaved(double* samples, size_t count)
{
    // Not supported
    (void)samples;
    (void)count;
    return 0;
}

void wav_audio_output_stream::flush()
{
    if (impl_ && impl_->sf_file_ != nullptr)
    {
        sf_write_sync(impl_->sf_file_);
    }
}

void wav_audio_output_stream::close()
{
    if (impl_ && impl_->sf_file_ != nullptr)
    {
        sf_close(impl_->sf_file_);
        impl_.reset();
    }
}

void wav_audio_output_stream::start()
{
    // Not supported
}

void wav_audio_output_stream::stop()
{
    // Not supported
}

LIBMODEM_NAMESPACE_END