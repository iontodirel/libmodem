// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
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
#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>

#include <nlohmann/json.hpp>

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
    if (props == nullptr)
    {
        return "";
    }

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
    if (props == nullptr)
    {
        return "";
    }

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
    if (device_format == nullptr)
    {
        return false;
    }

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
// audio_stream_type                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

audio_stream_type parse_audio_stream_type(const std::string& type_string)
{
    if (type_string == "input")
    {
        return audio_stream_type::input;
    }
    else if (type_string == "output")
    {
        return audio_stream_type::output;
    }
    else
    {
        return audio_stream_type::unknown;
    }
}

std::string to_string(audio_stream_type type)
{
    if (type == audio_stream_type::input)
    {
        return "input";
    }
    else if (type == audio_stream_type::output)
    {
        return "output";
    }
    else
    {
        return "unknown";
    }
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// audio_stream_base                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

audio_stream_base& audio_stream_base::operator=(wav_audio_input_stream& rhs)
{
    if (this->sample_rate() != rhs.sample_rate())
    {
        throw std::runtime_error("Cannot assign wav_audio_input_stream to audio_stream_base with different sample rate or channels");
    }

    if (this->type() != audio_stream_type::output)
    {
        throw std::runtime_error("Cannot assign wav_audio_input_stream to non-output audio_stream_base");
    }

    std::vector<double> buffer(1024);
    while (true)
    {
        size_t n = rhs.read(buffer.data(), buffer.size());
        if (n == 0)
        {
            break;
        }
        this->write(buffer.data(), n);
    }

    return *this;
}

void audio_stream_base::wait_write_completed()
{
    wait_write_completed(-1);
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// audio_stream                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

audio_stream::audio_stream(std::nullptr_t)
{
}

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

audio_stream_type audio_stream::type()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->type();
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

bool audio_stream::eof()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return stream_->eof();
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

audio_stream_base& audio_stream::get()
{
    if (!stream_)
    {
        throw std::runtime_error("Stream not initialized");
    }
    return *stream_;
}

audio_stream::operator bool() const
{
    return stream_ != nullptr;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// null_audio_stream                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

void null_audio_stream::close()
{
}

std::string null_audio_stream::name()
{
    return "null";
}

audio_stream_type null_audio_stream::type()
{
    return audio_stream_type::null;
}

void null_audio_stream::volume(int)
{
}

int null_audio_stream::volume()
{
    return 0;
}

int null_audio_stream::sample_rate()
{
    return 0;
}

int null_audio_stream::channels()
{
    return 0;
}

size_t null_audio_stream::write(const double*, size_t)
{
    return 0;
}

size_t null_audio_stream::write_interleaved(const double*, size_t)
{
    return 0;
}

size_t null_audio_stream::read(double*, size_t)
{
    return 0;
}

size_t null_audio_stream::read_interleaved(double*, size_t)
{
    return 0;
}

bool null_audio_stream::wait_write_completed(int)
{
    return true;
}

bool null_audio_stream::eof()
{
    return false;
}

void null_audio_stream::start()
{
}

void null_audio_stream::stop()
{
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
#if WIN32
    CComPtr<IMMDevice> device_;
#endif // WIN32
};

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
    CComPtr<IMMDevice> device_;
    CComPtr<IAudioClient> audio_client_;
    CComPtr<IAudioRenderClient> render_client_;
    CComPtr<IAudioEndpointVolume> endpoint_volume_;
    CComPtr<IAudioClock> audio_clock_;

    // Event that the audio engine signals each time a buffer becomes ready
    // to be processed by the client. Used in event-driven capture mode.
    HANDLE audio_samples_ready_event_ = nullptr;

    // Event used to signal the capture loop to stop.
    HANDLE stop_render_event_ = nullptr;

    // Ring buffer and synchronization
    boost::circular_buffer<float> ring_buffer_;
};

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
    CComPtr<IMMDevice> device_;
    CComPtr<IAudioClient> audio_client_;
    CComPtr<IAudioCaptureClient> capture_client_;
    CComPtr<IAudioEndpointVolume> endpoint_volume_;

    // Event that the audio engine signals each time a buffer becomes ready
    // to be processed by the client. Used in event-driven capture mode.
    HANDLE audio_samples_ready_event_ = nullptr;

    // Event used to signal the capture loop to stop.
    HANDLE stop_capture_event_ = nullptr;

    // Ring buffer and synchronization
    boost::circular_buffer<float> ring_buffer_;
};

#endif // WIN32

// **************************************************************** //
//                                                                  //
//                                                                  //
// audio_device                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

audio_device::audio_device() : impl_(std::make_unique<audio_device_impl>())
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
        throw std::runtime_error("Failed to get request_data flow");
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

audio_device::audio_device(int card_id, int device_id, audio_device_type type) : card_id(card_id), device_id(device_id), type(type)
{
    int err = 0;

    char hw_name[32];
    snprintf(hw_name, sizeof(hw_name), "hw:%d", card_id);

    snd_ctl_t* ctl;
    if ((err = snd_ctl_open(&ctl, hw_name, 0)) != 0)
    {
        throw std::runtime_error("Failed to open ALSA control interface");
    }

    std::unique_ptr<snd_ctl_t, void(*)(snd_ctl_t*)> ctl_guard(ctl, [](snd_ctl_t* h) { if (h) snd_ctl_close(h); });
    (void)ctl_guard;

    snd_pcm_info_t* pcm_info;
    snd_pcm_info_alloca(&pcm_info);
    snd_pcm_info_set_device(pcm_info, device_id);
    snd_pcm_info_set_subdevice(pcm_info, 0);

    snd_pcm_stream_t stream = (type == audio_device_type::render) ? SND_PCM_STREAM_PLAYBACK : SND_PCM_STREAM_CAPTURE;
    snd_pcm_info_set_stream(pcm_info, stream);

    if ((err = snd_ctl_pcm_info(ctl, pcm_info)) != 0)
    {
        throw std::runtime_error("Failed to get ALSA PCM info");
    }

    // Set device id using the format "hw:<card_id>,<device_id>"
    id = "hw:" + std::to_string(card_id) + "," + std::to_string(device_id);

    name = snd_pcm_info_get_name(pcm_info);

    char* card_name = nullptr;
    char* card_longname = nullptr;
    if ((err = snd_card_get_name(card_id, &card_name)) == 0 && card_name != nullptr)
    {
        std::unique_ptr<char, decltype(&free)> guard(card_name, free);
        description = card_name;
    }
    if ((err = snd_card_get_longname(card_id, &card_longname)) == 0 && card_longname != nullptr)
    {
        std::unique_ptr<char, decltype(&free)> guard(card_longname, free);
        description = card_longname;
    }

    state = audio_device_state::active;
}

#endif // __linux__

audio_device::audio_device(audio_device&& other) noexcept : impl_(std::make_unique<audio_device_impl>())
{
    id = std::move(other.id);
    name = std::move(other.name);
    description = std::move(other.description);
    type = other.type;
    state = other.state;

#if WIN32
    impl_->device_.Attach(other.impl_->device_.Detach()); // Release old, take new
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

#if WIN32
        impl_->device_.Attach(other.impl_->device_.Detach()); // Release old, take new
        container_id = std::move(other.container_id);
#endif // WIN32
#if __linux__
        card_id = other.card_id;
        device_id = other.device_id;
#endif // __linux__
    }
    return *this;
}

audio_stream audio_device::stream()
{
#if WIN32
    if (!impl_)
    {
        throw std::runtime_error("Device not initialized");
    }

    if (type == audio_device_type::render)
    {
        return audio_stream(std::make_unique<wasapi_audio_output_stream>(impl_.get()));
    }
    else if (type == audio_device_type::capture)
    {
        return audio_stream(std::make_unique<wasapi_audio_input_stream>(impl_.get()));
    }
#endif // WIN32

#if __linux__
    if (type == audio_device_type::render)
    {
        return audio_stream(std::make_unique<alsa_audio_output_stream>(card_id, device_id));
    }
    else if (type == audio_device_type::capture)
    {
        return audio_stream(std::make_unique<alsa_audio_input_stream>(card_id, device_id));
    }
#endif // __linux__

    return audio_stream(nullptr);
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

    int err = 0;

    int card = -1;

    // Loop through all available ALSA sound cards

    while ((err = snd_card_next(&card)) == 0 && card >= 0)
    {
        char hw_name[32];
        snprintf(hw_name, sizeof(hw_name), "hw:%d", card);

        snd_ctl_t* ctl;
        if ((err = snd_ctl_open(&ctl, hw_name, 0)) != 0)
        {
            continue;
        }

        std::unique_ptr<snd_ctl_t, void(*)(snd_ctl_t*)> ctl_guard(ctl, [](snd_ctl_t* h) { if (h) snd_ctl_close(h); });
        (void)ctl_guard;

        snd_pcm_info_t* pcm_info;
        snd_pcm_info_alloca(&pcm_info);

        int device = -1;

        while ((err = snd_ctl_pcm_next_device(ctl, &device)) == 0 && device >= 0)
        {
            snd_pcm_info_set_device(pcm_info, device);
            snd_pcm_info_set_subdevice(pcm_info, 0);

            // A single ALSA device can typically support both playback and capture
            // To mirror the behavior of other platforms, we create separate audio_device entries

            snd_pcm_info_set_stream(pcm_info, SND_PCM_STREAM_PLAYBACK);
            if ((err = snd_ctl_pcm_info(ctl, pcm_info)) == 0)
            {
                devices.emplace_back(card, device, audio_device_type::render);
            }

            snd_pcm_info_set_stream(pcm_info, SND_PCM_STREAM_CAPTURE);
            if ((err = snd_ctl_pcm_info(ctl, pcm_info)) == 0)
            {
                devices.emplace_back(card, device, audio_device_type::capture);
            }
        }
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
    });

    if (it != devices.end())
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

    if (it != devices.end())
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

    if (it != devices.end())
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

    EDataFlow flow = (type == audio_device_type::render) ? eRender : eCapture;

    CComPtr<IMMDevice> device_;
    if (FAILED(hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device_)))
    {
        return false;
    }

    audio_device_impl device_impl;
    device_impl.device_ = device_.p;

    device = audio_device(&device_impl);

    return true;

#endif // WIN32

#ifdef __linux__

    // Linux does not quite support default devices like Windows.
    // While certain distros might store a default, it is not generally standardized.

    int err = 0;

    snd_pcm_stream_t stream = (type == audio_device_type::render) ? SND_PCM_STREAM_PLAYBACK : SND_PCM_STREAM_CAPTURE;

    const char* device_names[] = {
        "default",
        "sysdefault",
        "pulse",
        "pipewire"
    };

    for (const char* device_name : device_names)
    {
        snd_pcm_t* pcm_handle = nullptr;

        if ((err = snd_pcm_open(&pcm_handle, device_name, stream, 0)) == 0 && pcm_handle != nullptr)
        {
            std::unique_ptr<snd_pcm_t, int(*)(snd_pcm_t*)> pcm_handle_guard(pcm_handle, snd_pcm_close);
            (void)pcm_handle_guard;

            snd_pcm_info_t* info;
            snd_pcm_info_alloca(&info);

            if ((err = snd_pcm_info(pcm_handle, info)) == 0)
            {
                int card_id = snd_pcm_info_get_card(info);
                int device_id = snd_pcm_info_get_device(info);

                if (card_id >= 0 && device_id >= 0)
                {
                    device = audio_device(card_id, device_id, type);
                    return true;
                }
            }
        }
    }

    // Fallback: return the first active device of the requested type

    std::vector<audio_device> devices = get_audio_devices(type, audio_device_state::active);

    if (!devices.empty())
    {
        device = std::move(devices[0]);
        return true;
    }

#endif // __linux__

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

wasapi_audio_output_stream::wasapi_audio_output_stream() : impl_(std::make_unique<wasapi_audio_output_stream_impl>())
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

    // Create the audio samples ready event and the stop signaling event
    // The audio samples ready event is used by WASAPI to signal when samples are available for capture
    // Use unique_ptr with a custom deleter to ensure handles are closed if an exception is thrown using RAII

    HANDLE audio_samples_ready_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (audio_samples_ready_event == nullptr)
    {
        throw std::runtime_error("Failed to create audio samples ready event");
    }

    std::unique_ptr<void, void(*)(void*)> audio_samples_ready_event_guard(audio_samples_ready_event, [](void* h) { if (h) CloseHandle(h); });

    HANDLE stop_render_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (stop_render_event == nullptr)
    {
        throw std::runtime_error("Failed to create stop render event");
    }

    std::unique_ptr<void, void(*)(void*)> stop_render_event_guard(stop_render_event, [](void* h) { if (h) CloseHandle(h); });

    HRESULT hr;

    CComPtr<IAudioClient> audio_client;
    CComPtr<IAudioEndpointVolume> endpoint_volume;
    CComPtr<IAudioRenderClient> render_client;
    CComPtr<IAudioClock> audio_clock;

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
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        2000000,  // 200ms buffer
        0,
        device_format,
        nullptr);

    CoTaskMemFree(device_format);

    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to initialize audio client");
    }

    if (FAILED(hr = audio_client->SetEventHandle(audio_samples_ready_event)))
    {
        throw std::runtime_error("Failed to set event handle");
    }

    if (FAILED(hr = audio_client->GetService(__uuidof(IAudioRenderClient), (void**)&render_client)))
    {
        throw std::runtime_error("Failed to get render client");
    }

    if (FAILED(hr = audio_client->GetService(__uuidof(IAudioClock), (void**)&audio_clock)))
    {
        throw std::runtime_error("Failed to get audio clock");
    }

    if (FAILED(hr = audio_client->GetBufferSize(reinterpret_cast<UINT32*>(&buffer_size_))))
    {
        throw std::runtime_error("Failed to get buffer size");
    }

    // Initialize ring buffer to hold ~5 second of audio
    size_t ring_buffer_size = sample_rate_ * channels_ * ring_buffer_size_seconds_;
    impl_->ring_buffer_.set_capacity(ring_buffer_size);

    impl_->audio_client_.Attach(audio_client.Detach());
    impl_->endpoint_volume_.Attach(endpoint_volume.Detach());
    impl_->render_client_.Attach(render_client.Detach());
    impl_->audio_clock_.Attach(audio_clock.Detach());

    impl_->audio_samples_ready_event_ = audio_samples_ready_event_guard.release();
    impl_->stop_render_event_ = stop_render_event_guard.release();
}

wasapi_audio_output_stream::wasapi_audio_output_stream(wasapi_audio_output_stream&& other) noexcept : impl_(std::make_unique<wasapi_audio_output_stream_impl>())
{
    impl_->device_.Attach(other.impl_->device_.Detach());
    impl_->audio_client_.Attach(other.impl_->audio_client_.Detach());
    impl_->render_client_.Attach(other.impl_->render_client_.Detach());
    impl_->endpoint_volume_.Attach(other.impl_->endpoint_volume_.Detach());
    impl_->audio_clock_.Attach(other.impl_->audio_clock_.Detach());

    impl_->audio_samples_ready_event_ = other.impl_->audio_samples_ready_event_;
    other.impl_->audio_samples_ready_event_ = nullptr;

    impl_->stop_render_event_ = other.impl_->stop_render_event_;
    other.impl_->stop_render_event_ = nullptr;

    // Note: render_thread_ cannot be moved while running
    // the mutex, condition_variable, and ring_buffer_ should not be moved as they
    // represent running state, and a stream should not be moved while running

    render_exception_ = other.render_exception_;
    ring_buffer_size_seconds_ = other.ring_buffer_size_seconds_;

    buffer_size_ = other.buffer_size_;
    sample_rate_ = other.sample_rate_;
    channels_ = other.channels_;
}

wasapi_audio_output_stream& wasapi_audio_output_stream::operator=(wasapi_audio_output_stream&& other) noexcept
{
    if (this != &other)
    {
        impl_->render_client_.Release();
        impl_->endpoint_volume_.Release();
        impl_->audio_client_.Release();
        impl_->device_.Release();
        impl_->audio_clock_.Release();

        impl_->device_.Attach(other.impl_->device_.Detach());
        impl_->audio_client_.Attach(other.impl_->audio_client_.Detach());
        impl_->render_client_.Attach(other.impl_->render_client_.Detach());
        impl_->endpoint_volume_.Attach(other.impl_->endpoint_volume_.Detach());
        impl_->audio_clock_.Attach(other.impl_->audio_clock_.Detach());

        if (impl_->audio_samples_ready_event_ != nullptr)
        {
            CloseHandle(impl_->audio_samples_ready_event_);
        }
        if (impl_->stop_render_event_ != nullptr)
        {
            CloseHandle(impl_->stop_render_event_);
        }

        impl_->audio_samples_ready_event_ = other.impl_->audio_samples_ready_event_;
        other.impl_->audio_samples_ready_event_ = nullptr;

        impl_->stop_render_event_ = other.impl_->stop_render_event_;
        other.impl_->stop_render_event_ = nullptr;

        // Note: render_thread_ cannot be moved while running
        // the mutex, condition_variable, and ring_buffer_ should not be moved as they
        // represent running state, and a stream should not be moved while running

        render_exception_ = other.render_exception_;
        ring_buffer_size_seconds_ = other.ring_buffer_size_seconds_;

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

    if (impl_)
    {
        impl_->render_client_.Release();
        impl_->endpoint_volume_.Release();
        impl_->audio_client_.Release();
        impl_->audio_clock_.Release();

        if (impl_->audio_samples_ready_event_ != nullptr)
        {
            CloseHandle(impl_->audio_samples_ready_event_);
        }

        if (impl_->stop_render_event_ != nullptr)
        {
            CloseHandle(impl_->stop_render_event_);
        }

        impl_.reset();
    }
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

audio_stream_type wasapi_audio_output_stream::type()
{
    return audio_stream_type::output;
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
    std::vector<double> interleaved_buffer(count * channels_);

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

            interleaved_buffer[i * channels_ + channel] = samples[i];
        }
    }

    size_t samples_written = write_interleaved(interleaved_buffer.data(), interleaved_buffer.size());

    size_t samples_written_mono = samples_written / channels_;

    return samples_written_mono;
}

size_t wasapi_audio_output_stream::write_interleaved(const double* samples, size_t count)
{
    if (!impl_)
    {
        throw std::runtime_error("Stream not initialized");
    }

    if (samples == nullptr || count == 0)
    {
        return 0;
    }

    size_t samples_to_write = count;

    std::unique_lock<std::mutex> lock(buffer_mutex_);

    // Wait until more data is consumed or stopped
    buffer_cv_.wait(lock, [&]() {
        return !impl_->ring_buffer_.full() || !started_ || render_exception_ != nullptr;
    });

    if (render_exception_)
    {
        std::exception_ptr ex = render_exception_;
        render_exception_ = nullptr;
        std::rethrow_exception(ex);
    }

    if (!started_ || impl_->ring_buffer_.full())
    {
        return 0;
    }

    size_t samples_available_to_write = (std::min)(impl_->ring_buffer_.capacity() - impl_->ring_buffer_.size(), samples_to_write);

    for (size_t i = 0; i < samples_available_to_write; i++)
    {
        impl_->ring_buffer_.push_back(static_cast<float>(samples[i]));
    }

    return samples_available_to_write;
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

    // Wait for the ring buffer to empty

    {
        std::unique_lock<std::mutex> lock(buffer_mutex_);

        if (timeout_ms < 0)
        {
            buffer_cv_.wait(lock, [&]() {
                return impl_->ring_buffer_.empty() || !started_ || render_exception_ != nullptr;
            });
        }
        else
        {
            if (!buffer_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]() {
                return impl_->ring_buffer_.empty() || !started_ || render_exception_ != nullptr;
            }))
            {
                return false; // Timeout waiting for ring buffer
            }
        }

        if (render_exception_)
        {
            std::exception_ptr ex = render_exception_;
            render_exception_ = nullptr;
            std::rethrow_exception(ex);
        }

        if (!started_)
        {
            return impl_->ring_buffer_.empty();
        }
    }

    uint64_t target_frames = total_frames_written_.load();

    if (target_frames == 0)
    {
        return true;
    }

    HRESULT hr;

    UINT64 clock_frequency;
    if (FAILED(hr = impl_->audio_clock_->GetFrequency(&clock_frequency)))
    {
        return false;
    }

    UINT64 target_position = (target_frames * clock_frequency) / sample_rate_;

    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    INT64 timeout_ticks = (timeout_ms >= 0) ? (static_cast<INT64>(timeout_ms) * freq.QuadPart) / 1000 : LLONG_MAX;

    while (true)
    {
        UINT64 current_position;
        if (FAILED(hr = impl_->audio_clock_->GetPosition(&current_position, nullptr)))
        {
            return false;
        }

        if (current_position >= target_position)
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

        UINT64 remaining_units = target_position - current_position;
        DWORD sleep_ms = static_cast<DWORD>((remaining_units * 1000) / clock_frequency);
        sleep_ms = (std::max)(1UL, sleep_ms / 2);

        Sleep(sleep_ms);
    }

    return true;
}

bool wasapi_audio_output_stream::eof()
{
    return false;
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

    // Start the render thread
    // The render thread will write audio data to the WASAPI render client from a ring buffer
    // After we start the render thread, we start the audio client to begin rendering audio

    render_thread_ = std::jthread(std::bind(&wasapi_audio_output_stream::run, this, std::placeholders::_1));

    // The render thread is now running, and awaiting for the WASAPI audio samples ready event to be signaled

    HRESULT hr;

    if (FAILED(hr = impl_->audio_client_->Start()))
    {
        render_thread_.request_stop();
        SetEvent(impl_->stop_render_event_);
        render_thread_.join();
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

        render_thread_.request_stop();

        // Signal the stop event to unblock any waiting read operations
        if (impl_->stop_render_event_ != nullptr)
        {
            SetEvent(impl_->stop_render_event_);
        }

        buffer_cv_.notify_all();

        render_thread_.join();

        if (impl_->audio_client_)
        {
            impl_->audio_client_->Stop();
        }

        started_ = false;
    }
}

bool wasapi_audio_output_stream::faulted()
{
    if (!impl_)
    {
        throw std::runtime_error("Stream not initialized");
    }

    std::lock_guard<std::mutex> lock(buffer_mutex_);
    return render_exception_ != nullptr;
}

void wasapi_audio_output_stream::throw_if_faulted()
{
    if (!impl_)
    {
        throw std::runtime_error("Stream not initialized");
    }

    std::lock_guard<std::mutex> lock(buffer_mutex_);
    if (render_exception_)
    {
        std::exception_ptr ex = render_exception_;
        render_exception_ = nullptr;
        std::rethrow_exception(ex);
    }
}

void wasapi_audio_output_stream::flush()
{
    if (impl_)
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        impl_->ring_buffer_.clear();
        total_frames_written_ = 0;
    }
}

void wasapi_audio_output_stream::run(std::stop_token stop_token)
{
    try
    {
        run_internal(stop_token);
    }
    catch (...)
    {
        // Swallow exceptions to prevent std::terminate from being called
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        render_exception_ = std::current_exception();
        buffer_cv_.notify_all();
    }
}

void wasapi_audio_output_stream::run_internal(std::stop_token stop_token)
{
    ensure_com_initialized();

    // Enable MMCSS for low-latency audio
    // Multimedia Class Scheduler Service (MMCSS) allows us to prioritize the audio render thread
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

    // MMCSS is used for the lifetime of the render thread
    // When the thread exits, we free the MMCSS handle using the unique_ptr custom deleter
    std::unique_ptr<void, void(*)(void*)> mmcss_guard(mmcss_handle, [](void* h) { if (h) AvRevertMmThreadCharacteristics(h); });
    (void)mmcss_guard;

    HRESULT hr;

    HANDLE wait_handles[2] = { impl_->stop_render_event_, impl_->audio_samples_ready_event_ };

    while (!stop_token.stop_requested())
    {
        DWORD wait_result = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);

        switch (wait_result)
        {
            case WAIT_OBJECT_0: // Stop event signaled
                return;

            case WAIT_OBJECT_0 + 1: // Buffer ready event signaled
                break;

            case WAIT_FAILED:
            default:
                throw std::runtime_error("WaitForMultipleObjects failed");
        }

        UINT32 padding;
        if (FAILED(hr = impl_->audio_client_->GetCurrentPadding(&padding)))
        {
            throw std::runtime_error("Failed to get current padding");
        }

        UINT32 frames_available_to_write = static_cast<UINT32>(buffer_size_) - padding;

        if (frames_available_to_write == 0)
        {
            continue;
        }

        BYTE* buffer;

        if (FAILED(hr = impl_->render_client_->GetBuffer(frames_available_to_write, &buffer)))
        {
            throw std::runtime_error("Failed to get buffer");
        }

        float* float_buffer = reinterpret_cast<float*>(buffer);

        size_t samples_available_to_write = frames_available_to_write * channels_;
        size_t samples_written = 0;
        DWORD flags = 0;

        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);

            size_t samples_available_to_read = impl_->ring_buffer_.size();

            if (samples_available_to_read == 0)
            {
                // No data available, write silence
                flags = AUDCLNT_BUFFERFLAGS_SILENT;
            }
            else
            {
                // Write as much as we can from the ring buffer
                samples_written = (std::min)(samples_available_to_read, samples_available_to_write);

                for (size_t i = 0; i < samples_written; i++)
                {
                    float_buffer[i] = impl_->ring_buffer_.front();
                    impl_->ring_buffer_.pop_front();
                }

                // Fill remaining with silence if we don't have enough data
                for (size_t i = samples_written; i < samples_available_to_write; i++)
                {
                    float_buffer[i] = 0.0f;
                }
            }
        }

        // Notify writers that there's space in the buffer
        buffer_cv_.notify_one();

        // If we partially filled, we still need to release the full buffer we requested
        // but only mark the frames we actually wrote
        if (FAILED(hr = impl_->render_client_->ReleaseBuffer(frames_available_to_write, flags)))
        {
            throw std::runtime_error("Failed to release buffer");
        }

        if (flags != AUDCLNT_BUFFERFLAGS_SILENT)
        {
            total_frames_written_ += frames_available_to_write;
        }
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

wasapi_audio_input_stream::wasapi_audio_input_stream() : impl_(std::make_unique<wasapi_audio_input_stream_impl>())
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
    size_t ring_buffer_size = sample_rate_ * channels_ * ring_buffer_size_seconds_;
    impl_->ring_buffer_.set_capacity(ring_buffer_size);

    impl_->audio_client_.Attach(audio_client.Detach());
    impl_->endpoint_volume_.Attach(endpoint_volume.Detach());
    impl_->capture_client_.Attach(capture_client.Detach());

    impl_->audio_samples_ready_event_ = audio_samples_ready_event_guard.release();
    impl_->stop_capture_event_ = stop_capture_event_guard.release();
}

wasapi_audio_input_stream::wasapi_audio_input_stream(wasapi_audio_input_stream&& other) noexcept : impl_(std::make_unique<wasapi_audio_input_stream_impl>())
{
    impl_->device_.Attach(other.impl_->device_.Detach());
    impl_->audio_client_.Attach(other.impl_->audio_client_.Detach());
    impl_->capture_client_.Attach(other.impl_->capture_client_.Detach());
    impl_->endpoint_volume_.Attach(other.impl_->endpoint_volume_.Detach());

    impl_->audio_samples_ready_event_ = other.impl_->audio_samples_ready_event_;
    other.impl_->audio_samples_ready_event_ = nullptr;

    impl_->stop_capture_event_ = other.impl_->stop_capture_event_;
    other.impl_->stop_capture_event_ = nullptr;

    buffer_size_ = other.buffer_size_;
    sample_rate_ = other.sample_rate_;
    channels_ = other.channels_;

    capture_exception_ = other.capture_exception_;
    discontinuity_count_ = other.discontinuity_count_;
    ring_buffer_size_seconds_ = other.ring_buffer_size_seconds_;
}

wasapi_audio_input_stream& wasapi_audio_input_stream::operator=(wasapi_audio_input_stream&& other) noexcept
{
    if (this != &other)
    {
        impl_->capture_client_.Release();
        impl_->endpoint_volume_.Release();
        impl_->audio_client_.Release();
        impl_->device_.Release();

        impl_->device_.Attach(other.impl_->device_.Detach());
        impl_->audio_client_.Attach(other.impl_->audio_client_.Detach());
        impl_->capture_client_.Attach(other.impl_->capture_client_.Detach());
        impl_->endpoint_volume_.Attach(other.impl_->endpoint_volume_.Detach());

        if (impl_->audio_samples_ready_event_ != nullptr)
        {
            CloseHandle(impl_->audio_samples_ready_event_);
        }
        if (impl_->stop_capture_event_ != nullptr)
        {
            CloseHandle(impl_->stop_capture_event_);
        }

        impl_->audio_samples_ready_event_ = other.impl_->audio_samples_ready_event_;
        other.impl_->audio_samples_ready_event_ = nullptr;
        impl_->stop_capture_event_ = other.impl_->stop_capture_event_;
        other.impl_->stop_capture_event_ = nullptr;

        buffer_size_ = other.buffer_size_;
        sample_rate_ = other.sample_rate_;
        channels_ = other.channels_;

        capture_exception_ = other.capture_exception_;
        discontinuity_count_ = other.discontinuity_count_;
        ring_buffer_size_seconds_ = other.ring_buffer_size_seconds_;
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

        if (impl_->audio_samples_ready_event_ != nullptr)
        {
            CloseHandle(impl_->audio_samples_ready_event_);
            impl_->audio_samples_ready_event_ = nullptr;
        }

        if (impl_->stop_capture_event_ != nullptr)
        {
            CloseHandle(impl_->stop_capture_event_);
            impl_->stop_capture_event_ = nullptr;
        }

        impl_.reset();
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

audio_stream_type wasapi_audio_input_stream::type()
{
    return audio_stream_type::input;
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
    return channels_;
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
    // Read mono by reading interleaved and taking first channel
    // Output samples are always mono
    // Count denotes the size of the buffer
    // The function returns the number of samples read

    if (samples == nullptr || count == 0)
    {
        return 0;
    }

    std::vector<double> interleaved_buffer(count * channels_);

    size_t interleaved_samples_read = read_interleaved(interleaved_buffer.data(), interleaved_buffer.size());

    size_t samples_read = interleaved_samples_read / channels_;

    for (size_t i = 0; i < samples_read; i++)
    {
        samples[i] = interleaved_buffer[i * channels_ + 0]; // Always take the first channel
    }

    return samples_read;
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

    size_t samples_needed = count;

    std::unique_lock<std::mutex> lock(buffer_mutex_);

    // Wait until we have enough data or stopped
    buffer_cv_.wait(lock, [&]() {
        return !impl_->ring_buffer_.empty() || !started_ || capture_exception_ != nullptr;
    });

    if (capture_exception_)
    {
        std::exception_ptr ex = capture_exception_;
        capture_exception_ = nullptr;
        std::rethrow_exception(ex);
    }

    if (!started_ && impl_->ring_buffer_.empty())
    {
        return 0;
    }

    // Read available samples (up to requested amount)
    size_t samples_available = (std::min)(impl_->ring_buffer_.size(), samples_needed);
    for (size_t i = 0; i < samples_available; i++)
    {
        samples[i] = static_cast<double>(impl_->ring_buffer_.front());
        impl_->ring_buffer_.pop_front();
    }

    return samples_available;
}

bool wasapi_audio_input_stream::wait_write_completed(int timeout_ms)
{
    // Not applicable for input streams
    (void)timeout_ms;
    return true;
}

bool wasapi_audio_input_stream::eof()
{
    return false;
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

    capture_thread_ = std::jthread(std::bind(&wasapi_audio_input_stream::run, this, std::placeholders::_1));

    // The capture thread is now running, and awaiting for the WASAPI audio samples ready event to be signaled
    // It does not matter if the audio client is started after the thread is started
    // Now we start the audio client

    HRESULT hr;

    if (FAILED(hr = impl_->audio_client_->Start()))
    {
        capture_thread_.request_stop();
        SetEvent(impl_->stop_capture_event_);
        capture_thread_.join();
        throw std::runtime_error("Failed to start");
    }

    started_ = true;
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

        capture_thread_.request_stop();

        // Signal the stop event to unblock any waiting read operations
        if (impl_->stop_capture_event_ != nullptr)
        {
            SetEvent(impl_->stop_capture_event_);
        }

        buffer_cv_.notify_all();

        capture_thread_.join();

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

    std::lock_guard<std::mutex> lock(buffer_mutex_);
    return capture_exception_ != nullptr;
}

void wasapi_audio_input_stream::throw_if_faulted()
{
    if (!impl_)
    {
        throw std::runtime_error("Stream not initialized");
    }

    std::lock_guard<std::mutex> lock(buffer_mutex_);
    if (capture_exception_)
    {
        std::exception_ptr ex = capture_exception_;
        capture_exception_ = nullptr;
        std::rethrow_exception(ex);
    }
}

void wasapi_audio_input_stream::flush()
{
    if (impl_)
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        impl_->ring_buffer_.clear();
    }
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
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        capture_exception_ = std::current_exception();
        buffer_cv_.notify_all();
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

    HANDLE wait_handles[2] = { impl_->stop_capture_event_, impl_->audio_samples_ready_event_ };

    // The capture thread runs continuously, until stop is requested
    // Or if we encounter an error
    // Before we retrieve available audio data, we wait for WASAPI to signal us that data is available
    // This avoids busy-waiting and reduces CPU usage
    // If data is available, we drain all available (audio) packets from the WASAPI capture buffer
    // And store them in our ring buffer for later retrieval

    while (!stop_token.stop_requested())
    {
        DWORD wait_result = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);

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

        // Drain all available frames

        UINT32 frames_maybe_available = 0;

        if (FAILED(hr = impl_->capture_client_->GetNextPacketSize(&frames_maybe_available)))
        {
            throw std::runtime_error("Failed to get next frame size");
        }

        while (frames_maybe_available > 0)
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

                if (FAILED(hr = impl_->capture_client_->GetNextPacketSize(&frames_maybe_available)))
                {
                    throw std::runtime_error("Failed to get next packet size");
                }

                continue;
            }

            if (FAILED(hr))
            {
                throw std::runtime_error("Failed to get capture buffer");
            }

            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
            {
                discontinuity_count_++;
            }

            size_t samples_count = frames_available * channels_;

            {
                // Push the captured samples into the ring buffer
                // The buffer contains samples for all channels interleaved

                std::lock_guard<std::mutex> lock(buffer_mutex_);

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

            buffer_cv_.notify_one();

            if (FAILED(impl_->capture_client_->ReleaseBuffer(frames_available)))
            {
                throw std::runtime_error("Failed to release capture buffer");
            }

            if (stop_token.stop_requested())
            {
                break;
            }

            if (FAILED(hr = impl_->capture_client_->GetNextPacketSize(&frames_maybe_available)))
            {
                throw std::runtime_error("Failed to get next packet size");
            }
        }
    }
}

#endif // WIN32

// **************************************************************** //
//                                                                  //
//                                                                  //
// alsa_audio_stream_control                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if __linux__

alsa_audio_stream_control::alsa_audio_stream_control(int card_id, const std::string& name, int index, int channel, audio_stream_type type) : card_id_(card_id), name_(name), index_(index), channel_(channel), type_(type)
{
}

std::string alsa_audio_stream_control::id() const
{
    return name_ + ":" + std::to_string(channel_);
}

std::string alsa_audio_stream_control::name() const
{
    return name_;
}

int alsa_audio_stream_control::index() const
{
    return index_;
}

void alsa_audio_stream_control::volume(int percent)
{
    if (card_id_ < 0)
    {
        return;
    }

    int err = 0;

    snd_mixer_t* mixer;
    if ((err = snd_mixer_open(&mixer, 0)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_open: ") + snd_strerror(err));
    }

    std::unique_ptr<snd_mixer_t, void(*)(snd_mixer_t*)> ctl_guard(mixer, [](snd_mixer_t* h) { if (h) snd_mixer_close(h); });
    (void)ctl_guard;

    std::string card_name = "hw:" + std::to_string(card_id_);

    if ((err = snd_mixer_attach(mixer, card_name.c_str())) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_open: ") + snd_strerror(err));
    }

    if ((err = snd_mixer_selem_register(mixer, nullptr, nullptr)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_selem_register: ") + snd_strerror(err));
    }

    if ((err = snd_mixer_load(mixer)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_load: ") + snd_strerror(err));
    }

    snd_mixer_selem_id_t* sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, index_);
    snd_mixer_selem_id_set_name(sid, name_.c_str());

    snd_mixer_elem_t* elem = snd_mixer_find_selem(mixer, sid);
    if (elem == nullptr)
    {
        return;
    }

    long min, max;

    if (type_ == audio_stream_type::input)
    {
        if (snd_mixer_selem_has_capture_volume(elem))
        {
            if ((err = snd_mixer_selem_get_capture_volume_range(elem, &min, &max)) != 0)
            {
                throw std::runtime_error(std::string("snd_mixer_selem_get_capture_volume_range: ") + snd_strerror(err));
            }

            long volume = min + (max - min) * percent / 100;

            if ((err = snd_mixer_selem_set_capture_volume(elem, static_cast<snd_mixer_selem_channel_id_t>(channel_), volume)) != 0)
            {
                throw std::runtime_error(std::string("snd_mixer_selem_set_capture_volume: ") + snd_strerror(err));
            }
        }
    }
    else
    {
        if (snd_mixer_selem_has_playback_volume(elem))
        {
            if ((err = snd_mixer_selem_get_playback_volume_range(elem, &min, &max)) != 0)
            {
                throw std::runtime_error(std::string("snd_mixer_selem_get_playback_volume_range: ") + snd_strerror(err));
            }

            long volume = min + (max - min) * percent / 100;

            if ((err = snd_mixer_selem_set_playback_volume(elem, static_cast<snd_mixer_selem_channel_id_t>(channel_), volume)) != 0)
            {
                throw std::runtime_error(std::string("snd_mixer_selem_set_playback_volume: ") + snd_strerror(err));
            }
        }
    }
}

int alsa_audio_stream_control::volume()
{
    if (card_id_ < 0)
    {
        return 0;
    }

    int err = 0;

    snd_mixer_t* mixer;
    if ((err = snd_mixer_open(&mixer, 0)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_open: ") + snd_strerror(err));
    }

    std::unique_ptr<snd_mixer_t, void(*)(snd_mixer_t*)> mixer_guard(mixer, [](snd_mixer_t* h) { if (h) snd_mixer_close(h); });
    (void)mixer_guard;

    std::string card_name = "hw:" + std::to_string(card_id_);

    if ((err = snd_mixer_attach(mixer, card_name.c_str())) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_attach: ") + snd_strerror(err));
    }

    if ((err = snd_mixer_selem_register(mixer, nullptr, nullptr)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_selem_register: ") + snd_strerror(err));
    }

    if ((err = snd_mixer_load(mixer)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_load: ") + snd_strerror(err));
    }

    int percent = 0;

    snd_mixer_selem_id_t* sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, index_);
    snd_mixer_selem_id_set_name(sid, name_.c_str());

    snd_mixer_elem_t* elem = snd_mixer_find_selem(mixer, sid);
    if (elem == nullptr)
    {
        return 0;
    }

    long min, max, volume;

    if (type_ == audio_stream_type::input)
    {
        if (snd_mixer_selem_has_capture_volume(elem))
        {
            if ((err = snd_mixer_selem_get_capture_volume_range(elem, &min, &max)) != 0)
            {
                throw std::runtime_error(std::string("snd_mixer_selem_get_capture_volume_range: ") + snd_strerror(err));
            }

            if ((err = snd_mixer_selem_get_capture_volume(elem, static_cast<snd_mixer_selem_channel_id_t>(channel_), &volume)) != 0)
            {
                throw std::runtime_error(std::string("snd_mixer_selem_get_capture_volume: ") + snd_strerror(err));
            }

            if (max > min)
            {
                percent = static_cast<int>(100 * (volume - min) / (max - min));
            }
        }
    }
    else
    {
        if (snd_mixer_selem_has_playback_volume(elem))
        {
            if ((err = snd_mixer_selem_get_playback_volume_range(elem, &min, &max)) != 0)
            {
                throw std::runtime_error(std::string("snd_mixer_selem_get_playback_volume_range: ") + snd_strerror(err));
            }

            if ((err = snd_mixer_selem_get_playback_volume(elem, static_cast<snd_mixer_selem_channel_id_t>(channel_), &volume)) != 0)
            {
                throw std::runtime_error(std::string("snd_mixer_selem_get_playback_volume: ") + snd_strerror(err));
            }

            if (max > min)
            {
                percent = static_cast<int>(100 * (volume - min) / (max - min));
            }
        }
    }

    return percent;
}

void alsa_audio_stream_control::mute(bool mute)
{
    if (card_id_ < 0)
    {
        return;
    }

    int err = 0;

    snd_mixer_t* mixer;
    if ((err = snd_mixer_open(&mixer, 0)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_open: ") + snd_strerror(err));
    }

    std::unique_ptr<snd_mixer_t, void(*)(snd_mixer_t*)> mixer_guard(mixer, [](snd_mixer_t* h) { if (h) snd_mixer_close(h); });
    (void)mixer_guard;

    std::string card_name = "hw:" + std::to_string(card_id_);

    if ((err = snd_mixer_attach(mixer, card_name.c_str())) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_attach: ") + snd_strerror(err));
    }

    if ((err = snd_mixer_selem_register(mixer, nullptr, nullptr)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_selem_register: ") + snd_strerror(err));
    }

    if ((err = snd_mixer_load(mixer)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_load: ") + snd_strerror(err));
    }

    snd_mixer_selem_id_t* sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, index_);
    snd_mixer_selem_id_set_name(sid, name_.c_str());

    snd_mixer_elem_t* elem = snd_mixer_find_selem(mixer, sid);
    if (elem == nullptr)
    {
        return;
    }

    if (type_ == audio_stream_type::input)
    {
        if (snd_mixer_selem_has_capture_switch(elem))
        {
            if ((err = snd_mixer_selem_set_capture_switch(elem, static_cast<snd_mixer_selem_channel_id_t>(channel_), mute ? 0 : 1)) != 0)
            {
                throw std::runtime_error(std::string("snd_mixer_selem_set_capture_switch: ") + snd_strerror(err));
            }
        }
    }
    else
    {
        if (snd_mixer_selem_has_playback_switch(elem))
        {
            if ((err = snd_mixer_selem_set_playback_switch(elem, static_cast<snd_mixer_selem_channel_id_t>(channel_), mute ? 0 : 1)) != 0)
            {
                throw std::runtime_error(std::string("snd_mixer_selem_set_playback_switch: ") + snd_strerror(err));
            }
        }
    }
}

bool alsa_audio_stream_control::mute()
{
    if (card_id_ < 0)
    {
        return false;
    }

    int err = 0;

    snd_mixer_t* mixer;
    if ((err = snd_mixer_open(&mixer, 0)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_open: ") + snd_strerror(err));
    }

    std::unique_ptr<snd_mixer_t, void(*)(snd_mixer_t*)> mixer_guard(mixer, [](snd_mixer_t* h) { if (h) snd_mixer_close(h); });
    (void)mixer_guard;

    std::string card_name = "hw:" + std::to_string(card_id_);

    if ((err = snd_mixer_attach(mixer, card_name.c_str())) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_attach: ") + snd_strerror(err));
    }

    if ((err = snd_mixer_selem_register(mixer, nullptr, nullptr)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_selem_register: ") + snd_strerror(err));
    }

    if ((err = snd_mixer_load(mixer)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_load: ") + snd_strerror(err));
    }

    bool is_muted = false;

    snd_mixer_selem_id_t* sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, index_);
    snd_mixer_selem_id_set_name(sid, name_.c_str());

    snd_mixer_elem_t* elem = snd_mixer_find_selem(mixer, sid);
    if (elem == nullptr)
    {
        return false;
    }

    int switch_value = 1;

    if (type_ == audio_stream_type::input)
    {
        if (snd_mixer_selem_has_capture_switch(elem))
        {
            if ((err = snd_mixer_selem_get_capture_switch(elem, static_cast<snd_mixer_selem_channel_id_t>(channel_), &switch_value)) != 0)
            {
                throw std::runtime_error(std::string("snd_mixer_selem_get_capture_switch: ") + snd_strerror(err));
            }

            is_muted = (switch_value == 0);
        }
    }
    else
    {
        if (snd_mixer_selem_has_playback_switch(elem))
        {
            if ((err = snd_mixer_selem_get_playback_switch(elem, static_cast<snd_mixer_selem_channel_id_t>(channel_), &switch_value)) != 0)
            {
                throw std::runtime_error(std::string("snd_mixer_selem_get_playback_switch: ") + snd_strerror(err));
            }

            is_muted = (switch_value == 0);
        }
    }

    return is_muted;
}

int alsa_audio_stream_control::channel() const
{
    return channel_;
}

bool alsa_audio_stream_control::can_mute()
{
    if (card_id_ < 0)
    {
        return false;
    }

    int err = 0;

    snd_mixer_t* mixer;
    if ((err = snd_mixer_open(&mixer, 0)) != 0)
    {
        return false;
    }

    std::unique_ptr<snd_mixer_t, void(*)(snd_mixer_t*)> mixer_guard(mixer, [](snd_mixer_t* h) { if (h) snd_mixer_close(h); });
    (void)mixer_guard;

    std::string card_name = "hw:" + std::to_string(card_id_);

    if ((err = snd_mixer_attach(mixer, card_name.c_str())) != 0)
    {
        return false;
    }

    if ((err = snd_mixer_selem_register(mixer, nullptr, nullptr)) != 0)
    {
        return false;
    }

    if ((err = snd_mixer_load(mixer)) != 0)
    {
        return false;
    }

    snd_mixer_selem_id_t* sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, index_);
    snd_mixer_selem_id_set_name(sid, name_.c_str());

    snd_mixer_elem_t* elem = snd_mixer_find_selem(mixer, sid);
    if (elem == nullptr)
    {
        return false;
    }

    if (type_ == audio_stream_type::input)
    {
        return snd_mixer_selem_has_capture_switch(elem);
    }
    else
    {
        return snd_mixer_selem_has_playback_switch(elem);
    }
}

bool alsa_audio_stream_control::can_set_volume()
{
    if (card_id_ < 0)
    {
        return false;
    }

    int err = 0;

    snd_mixer_t* mixer;
    if ((err = snd_mixer_open(&mixer, 0)) != 0)
    {
        return false;
    }

    std::unique_ptr<snd_mixer_t, void(*)(snd_mixer_t*)> mixer_guard(mixer, [](snd_mixer_t* h) { if (h) snd_mixer_close(h); });
    (void)mixer_guard;

    std::string card_name = "hw:" + std::to_string(card_id_);

    if ((err = snd_mixer_attach(mixer, card_name.c_str())) != 0)
    {
        return false;
    }

    if ((err = snd_mixer_selem_register(mixer, nullptr, nullptr)) != 0)
    {
        return false;
    }

    if ((err = snd_mixer_load(mixer)) != 0)
    {
        return false;
    }

    snd_mixer_selem_id_t* sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, index_);
    snd_mixer_selem_id_set_name(sid, name_.c_str());

    snd_mixer_elem_t* elem = snd_mixer_find_selem(mixer, sid);
    if (elem == nullptr)
    {
        return false;
    }

    if (type_ == audio_stream_type::input)
    {
        return snd_mixer_selem_has_capture_volume(elem);
    }
    else
    {
        return snd_mixer_selem_has_playback_volume(elem);
    }
}

#endif // __linux_

// **************************************************************** //
//                                                                  //
//                                                                  //
// alsa_audio_stream_impl                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if __linux__

struct alsa_audio_stream_impl
{
    snd_pcm_t* pcm_handle_ = nullptr;
    snd_pcm_format_t format_;
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

alsa_audio_output_stream::alsa_audio_output_stream() : impl_(std::make_unique<alsa_audio_stream_impl>())
{
}

alsa_audio_output_stream::alsa_audio_output_stream(int card_id, int device_id) : card_id(card_id), device_id(device_id)
{
    impl_ = std::make_unique<alsa_audio_stream_impl>();

    int err = 0;

    std::string device_name = "plughw:" + std::to_string(card_id) + "," + std::to_string(device_id);

    if ((err = snd_pcm_open(&impl_->pcm_handle_, device_name.c_str(), SND_PCM_STREAM_PLAYBACK, 0)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_open: ") + snd_strerror(err));
    }

    // Configure ALSA hardware parameters
    // First allocate the hw_params structure
    // Set access as SND_PCM_ACCESS_RW_INTERLEAVED
    // Set format to SND_PCM_FORMAT_FLOAT_LE

    snd_pcm_hw_params_t* hw_params;
    snd_pcm_hw_params_alloca(&hw_params);

    if ((err = snd_pcm_hw_params_any(impl_->pcm_handle_, hw_params)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_any: ") + snd_strerror(err));
    }

    if ((err = snd_pcm_hw_params_set_access(impl_->pcm_handle_, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_set_access: ") + snd_strerror(err));
    }

    unsigned int sample_rate = static_cast<unsigned int>(sample_rate_);

    if ((err = snd_pcm_hw_params_set_rate_near(impl_->pcm_handle_, hw_params, &sample_rate, nullptr)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_set_rate_near: ") + snd_strerror(err));
    }

    if ((err = snd_pcm_hw_params_set_format(impl_->pcm_handle_, hw_params, SND_PCM_FORMAT_FLOAT_LE)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_set_format: ") + snd_strerror(err));
    }

    if ((err = snd_pcm_hw_params(impl_->pcm_handle_, hw_params)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params: ") + snd_strerror(err));
    }

    // Read back what ALSA actually set

    unsigned int channels = 0;

    if ((err = snd_pcm_hw_params_get_channels(hw_params, &channels)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_get_channels: ") + snd_strerror(err));
    }

    if ((err = snd_pcm_hw_params_get_rate(hw_params, &sample_rate, nullptr)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_get_rate: ") + snd_strerror(err));
    }

    if ((err = snd_pcm_hw_params_get_format(hw_params, &impl_->format_)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_get_format: ") + snd_strerror(err));
    }

    channels_ = static_cast<int>(channels);
    sample_rate_ = static_cast<int>(sample_rate);

    if ((err = snd_pcm_prepare(impl_->pcm_handle_)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_prepare: ") + snd_strerror(err));
    }
}

alsa_audio_output_stream::alsa_audio_output_stream(alsa_audio_output_stream&& other) noexcept : impl_(std::make_unique<alsa_audio_stream_impl>())
{
    impl_->pcm_handle_ = other.impl_->pcm_handle_;
    other.impl_->pcm_handle_ = nullptr;
    impl_->format_ = other.impl_->format_;

    card_id = other.card_id;
    device_id = other.device_id;
    sample_rate_ = other.sample_rate_;
    channels_ = other.channels_;
}

alsa_audio_output_stream& alsa_audio_output_stream::operator=(alsa_audio_output_stream&& other) noexcept
{
    if (this != &other)
    {
        if (impl_ && impl_->pcm_handle_ != nullptr)
        {
            snd_pcm_drain(impl_->pcm_handle_);
            snd_pcm_close(impl_->pcm_handle_);
            impl_->pcm_handle_ = nullptr;
        }

        impl_->pcm_handle_ = other.impl_->pcm_handle_;
        other.impl_->pcm_handle_ = nullptr;
        impl_->format_ = other.impl_->format_;

        card_id = other.card_id;
        device_id = other.device_id;
        sample_rate_ = other.sample_rate_;
        channels_ = other.channels_;
    }
    return *this;
}

alsa_audio_output_stream::~alsa_audio_output_stream()
{
    close();
}

void alsa_audio_output_stream::close()
{
    if (impl_ && impl_->pcm_handle_ != nullptr)
    {
        snd_pcm_drain(impl_->pcm_handle_);
        snd_pcm_close(impl_->pcm_handle_);
        impl_->pcm_handle_ = nullptr;
    }
}

audio_stream_type alsa_audio_output_stream::type()
{
    return audio_stream_type::output;
}

std::string alsa_audio_output_stream::name()
{
    return "hw:" + std::to_string(card_id) + "," + std::to_string(device_id);
}

void alsa_audio_output_stream::mute(bool mute)
{
    for (auto& control : controls())
    {
        if (control.can_mute())
        {
            control.mute(mute);
        }
    }
}

bool alsa_audio_output_stream::mute()
{
    for (auto& control : controls())
    {
        if (control.can_mute() && !control.mute())
        {
            return false;
        }
    }
    return true;
}

void alsa_audio_output_stream::volume(int percent)
{
    for (auto& control : controls())
    {
        if (control.can_set_volume())
        {
            control.volume(percent);
        }
    }
}

int alsa_audio_output_stream::volume()
{
    int total = 0;
    int count = 0;

    for (auto& control : controls())
    {
        if (!control.can_set_volume())
        {
            continue;
        }
        total += control.volume();
        count++;
    }

    if (count == 0)
    {
        return 0;
    }

    return total / count;
}

size_t alsa_audio_output_stream::write(const double* samples, size_t count)
{
    if (!impl_ || !impl_->pcm_handle_)
    {
        throw std::runtime_error("Stream not initialized");
    }

    if (samples == nullptr || count == 0)
    {
        return 0;
    }

    // Convert mono samples to interleaved (duplicate to all channels)
    std::vector<float> interleaved_buffer(count * channels_);

    for (size_t i = 0; i < count; i++)
    {
        for (int channel = 0; channel < channels_; channel++)
        {
            interleaved_buffer[i * channels_ + channel] = static_cast<float>(samples[i]);
        }
    }

    size_t samples_written = write_interleaved(interleaved_buffer.data(), interleaved_buffer.size());

    size_t samples_written_mono = samples_written / channels_;

    return samples_written_mono;
}

size_t alsa_audio_output_stream::write_interleaved(const double* samples, size_t count)
{
    std::vector<float> samples_float(count);
    std::copy(samples, samples + count, samples_float.begin());
    return write_interleaved(samples_float.data(), count);
}

size_t alsa_audio_output_stream::write_interleaved(const float* samples, size_t count)
{
    int err = 0;

    size_t frames_count = count / channels_;

    size_t frames_written = 0;

    while (frames_written < frames_count)
    {
        snd_pcm_sframes_t n = snd_pcm_writei(impl_->pcm_handle_, samples + (frames_written * channels_), frames_count - frames_written);

        if (n == -EAGAIN)
        {
            snd_pcm_wait(impl_->pcm_handle_, 1000);
            continue;
        }

        if (n == -EPIPE)
        {
            // Underrun - recover and retry
            if ((err = snd_pcm_prepare(impl_->pcm_handle_)) != 0)
            {
                throw std::runtime_error(std::string("snd_pcm_prepare: ") + snd_strerror(err));
            }
            continue;
        }

        if (n < 0)
        {
            if ((err = snd_pcm_recover(impl_->pcm_handle_, static_cast<int>(n), 1)) != 0)
            {
                throw std::runtime_error(std::string("snd_pcm_recover: ") + snd_strerror(err));
            }
            continue;
        }

        frames_written += static_cast<size_t>(n);
    }

    return frames_written * channels_;
}

bool alsa_audio_output_stream::wait_write_completed(int timeout_ms)
{
    if (impl_ && impl_->pcm_handle_)
    {
        int err = 0;

        if ((err = snd_pcm_drain(impl_->pcm_handle_)) != 0)
        {
            throw std::runtime_error(std::string("snd_pcm_drain: ") + snd_strerror(err));
        }

        if ((err = snd_pcm_prepare(impl_->pcm_handle_)) != 0)
        {
            throw std::runtime_error(std::string("snd_pcm_prepare: ") + snd_strerror(err));
        }
    }
    return true;
}

bool alsa_audio_output_stream::eof()
{
    return false;
}

size_t alsa_audio_output_stream::read(double* samples, size_t count)
{
    // Not supported for output stream
    return 0;
}

size_t alsa_audio_output_stream::read_interleaved(double* samples, size_t count)
{
    // Not supported for output stream
    return 0;
}

void alsa_audio_output_stream::start()
{
    if (started_ || !start_stop_enabled_)
    {
        return;
    }

    if (impl_ && impl_->pcm_handle_)
    {
        int err = 0;
        if ((err = snd_pcm_start(impl_->pcm_handle_)) != 0)
        {
            throw std::runtime_error(std::string("snd_pcm_prepare: ") + snd_strerror(err));
        }
    }

    started_ = true;
}

void alsa_audio_output_stream::stop()
{
    if (!started_ || !start_stop_enabled_)
    {
        return;
    }

    if (impl_ && impl_->pcm_handle_)
    {
        int err = 0;

        if ((err = snd_pcm_drop(impl_->pcm_handle_)) != 0)
        {
            throw std::runtime_error(std::string("snd_pcm_drop: ") + snd_strerror(err));
        }

        if ((err = snd_pcm_prepare(impl_->pcm_handle_)) != 0)
        {
            throw std::runtime_error(std::string("snd_pcm_prepare: ") + snd_strerror(err));
        }
    }

    started_ = false;
}

void alsa_audio_output_stream::enable_start_stop(bool enable)
{
    start_stop_enabled_ = enable;
}

bool alsa_audio_output_stream::enable_start_stop()
{
    return start_stop_enabled_;
}

std::vector<alsa_audio_stream_control> alsa_audio_output_stream::controls()
{
    std::vector<alsa_audio_stream_control> controls;

    if (card_id < 0)
    {
        return controls;
    }

    int err = 0;

    snd_mixer_t* mixer;
    if ((err = snd_mixer_open(&mixer, 0)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_open: ") + snd_strerror(err));
    }

    std::unique_ptr<snd_mixer_t, void(*)(snd_mixer_t*)> mixer_guard(mixer, [](snd_mixer_t* h) { if (h) snd_mixer_close(h); });
    (void)mixer_guard;

    std::string card_name = "hw:" + std::to_string(card_id);

    if ((err = snd_mixer_attach(mixer, card_name.c_str())) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_attach: ") + snd_strerror(err));
    }

    if ((err = snd_mixer_selem_register(mixer, nullptr, nullptr)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_selem_register: ") + snd_strerror(err));
    }

    if ((err = snd_mixer_load(mixer)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_load: ") + snd_strerror(err));
    }

    for (snd_mixer_elem_t* elem = snd_mixer_first_elem(mixer); elem != nullptr; elem = snd_mixer_elem_next(elem))
    {
        if (!snd_mixer_selem_is_active(elem))
        {
            continue;
        }

        if (snd_mixer_selem_has_playback_volume(elem) || snd_mixer_selem_has_playback_switch(elem))
        {
            const char* name = snd_mixer_selem_get_name(elem);
            int index = snd_mixer_selem_get_index(elem);

            for (int channel = 0; channel <= SND_MIXER_SCHN_LAST; channel++)
            {
                if (snd_mixer_selem_has_playback_channel(elem, static_cast<snd_mixer_selem_channel_id_t>(channel)))
                {
                    controls.emplace_back(card_id, name, index, channel, audio_stream_type::output);
                }
            }
        }
    }

    return controls;
}

int alsa_audio_output_stream::sample_rate()
{
    return sample_rate_;
}

int alsa_audio_output_stream::channels()
{
    return channels_;
}

#endif // __linux__

// **************************************************************** //
//                                                                  //
//                                                                  //
// alsa_audio_input_stream                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if __linux__

alsa_audio_input_stream::alsa_audio_input_stream() : impl_(std::make_unique<alsa_audio_stream_impl>())
{
}

alsa_audio_input_stream::alsa_audio_input_stream(int card_id, int device_id) : card_id(card_id), device_id(device_id)
{
    impl_ = std::make_unique<alsa_audio_stream_impl>();

    int err = 0;

    std::string device_name = "plughw:" + std::to_string(card_id) + "," + std::to_string(device_id);

    if ((err = snd_pcm_open(&impl_->pcm_handle_, device_name.c_str(), SND_PCM_STREAM_CAPTURE, 0)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_open: ") + snd_strerror(err));
    }

    snd_pcm_hw_params_t* hw_params;
    snd_pcm_hw_params_alloca(&hw_params);

    if ((err = snd_pcm_hw_params_any(impl_->pcm_handle_, hw_params)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_any: ") + snd_strerror(err));
    }

    if ((err = snd_pcm_hw_params_set_access(impl_->pcm_handle_, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_set_access: ") + snd_strerror(err));
    }

    unsigned int sample_rate = static_cast<unsigned int>(sample_rate_);

    if ((err = snd_pcm_hw_params_set_rate_near(impl_->pcm_handle_, hw_params, &sample_rate, nullptr)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_set_rate_near: ") + snd_strerror(err));
    }

    if ((err = snd_pcm_hw_params_set_format(impl_->pcm_handle_, hw_params, SND_PCM_FORMAT_FLOAT_LE)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_set_format: ") + snd_strerror(err));
    }

    if ((err = snd_pcm_hw_params(impl_->pcm_handle_, hw_params)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params: ") + snd_strerror(err));
    }

    unsigned int channels = 0;

    if ((err = snd_pcm_hw_params_get_channels(hw_params, &channels)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_get_channels: ") + snd_strerror(err));
    }

    if ((err = snd_pcm_hw_params_get_rate(hw_params, &sample_rate, nullptr)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_get_rate: ") + snd_strerror(err));
    }

    if ((err = snd_pcm_hw_params_get_format(hw_params, &impl_->format_)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_hw_params_get_format: ") + snd_strerror(err));
    }

    channels_ = static_cast<int>(channels);
    sample_rate_ = static_cast<int>(sample_rate);

    if ((err = snd_pcm_prepare(impl_->pcm_handle_)) != 0)
    {
        throw std::runtime_error(std::string("snd_pcm_prepare: ") + snd_strerror(err));
    }
}

alsa_audio_input_stream::alsa_audio_input_stream(alsa_audio_input_stream&& other) noexcept : impl_(std::make_unique<alsa_audio_stream_impl>())
{
    impl_->pcm_handle_ = other.impl_->pcm_handle_;
    other.impl_->pcm_handle_ = nullptr;
    impl_->format_ = other.impl_->format_;

    card_id = other.card_id;
    device_id = other.device_id;
    sample_rate_ = other.sample_rate_;
    channels_ = other.channels_;
}

alsa_audio_input_stream& alsa_audio_input_stream::operator=(alsa_audio_input_stream&& other) noexcept
{
    if (this != &other)
    {
        if (impl_ && impl_->pcm_handle_)
        {
            snd_pcm_drop(impl_->pcm_handle_);
            snd_pcm_close(impl_->pcm_handle_);
            impl_->pcm_handle_ = nullptr;
        }

        impl_->pcm_handle_ = other.impl_->pcm_handle_;
        other.impl_->pcm_handle_ = nullptr;
        impl_->format_ = other.impl_->format_;
        
        card_id = other.card_id;
        device_id = other.device_id;
        sample_rate_ = other.sample_rate_;
        channels_ = other.channels_;
    }
    return *this;
}

alsa_audio_input_stream::~alsa_audio_input_stream()
{
    close();
}

void alsa_audio_input_stream::close()
{
    if (impl_ && impl_->pcm_handle_)
    {
        snd_pcm_drop(impl_->pcm_handle_);
        snd_pcm_close(impl_->pcm_handle_);
        impl_->pcm_handle_ = nullptr;
    }
}

audio_stream_type alsa_audio_input_stream::type()
{
    return audio_stream_type::input;
}

std::string alsa_audio_input_stream::name()
{
    return "hw:" + std::to_string(card_id) + "," + std::to_string(device_id);
}

void alsa_audio_input_stream::mute(bool mute)
{
    for (auto& control : controls())
    {
        if (control.can_mute())
        {
            control.mute(mute);
        }
    }
}

bool alsa_audio_input_stream::mute()
{
    for (auto& control : controls())
    {
        if (control.can_mute() && !control.mute())
        {
            return false;
        }
    }
    return true;
}

void alsa_audio_input_stream::volume(int percent)
{
    for (auto& control : controls())
    {
        if (control.can_set_volume())
        {
            control.volume(percent);
        }
    }
}

int alsa_audio_input_stream::volume()
{
    int total = 0;
    int count = 0;

    for (auto& control : controls())
    {
        if (control.can_set_volume())
        {
            total += control.volume();
            count++;
        }
    }

    if (count == 0)
    {
        return 0;
    }

    return total / count;
}

size_t alsa_audio_input_stream::write(const double* samples, size_t count)
{
    // Not supported for input stream
    (void)samples;
    (void)count;
    return 0;
}

size_t alsa_audio_input_stream::write_interleaved(const double* samples, size_t count)
{
    // Not supported for input stream
    (void)samples;
    (void)count;
    return 0;
}

size_t alsa_audio_input_stream::read(double* samples, size_t count)
{
    if (!impl_ || !impl_->pcm_handle_)
    {
        throw std::runtime_error("Stream not initialized");
    }

    if (samples == nullptr || count == 0)
    {
        return 0;
    }

    // Read interleaved and extract first channel (mono)
    std::vector<float> interleaved_buffer(count * channels_);

    size_t samples_read = read_interleaved(interleaved_buffer.data(), interleaved_buffer.size());

    size_t frames_read = samples_read / channels_;

    for (size_t i = 0; i < frames_read; i++)
    {
        samples[i] = static_cast<double>(interleaved_buffer[i * channels_]);
    }

    return frames_read;
}

size_t alsa_audio_input_stream::read_interleaved(double* samples, size_t count)
{
    std::vector<float> samples_float(count);

    size_t samples_read = read_interleaved(samples_float.data(), count);

    for (size_t i = 0; i < samples_read; i++)
    {
        samples[i] = static_cast<double>(samples_float[i]);
    }

    return samples_read;
}

size_t alsa_audio_input_stream::read_interleaved(float* samples, size_t count)
{
    if (!impl_ || !impl_->pcm_handle_)
    {
        throw std::runtime_error("Stream not initialized");
    }

    int err = 0;

    size_t frames_count = count / channels_;

    size_t frames_read = 0;

    while (frames_read < frames_count)
    {
        snd_pcm_sframes_t n = snd_pcm_readi(impl_->pcm_handle_, samples + (frames_read * channels_), frames_count - frames_read);

        if (n == -EAGAIN)
        {
            snd_pcm_wait(impl_->pcm_handle_, 1000);
            continue;
        }

        if (n == -EPIPE)
        {
            // Overrun - recover and retry
            if ((err = snd_pcm_prepare(impl_->pcm_handle_)) != 0)
            {
                throw std::runtime_error(std::string("snd_pcm_prepare: ") + snd_strerror(err));
            }
            continue;
        }

        if (n < 0)
        {
            if ((err = snd_pcm_recover(impl_->pcm_handle_, static_cast<int>(n), 1)) != 0)
            {
                throw std::runtime_error(std::string("snd_pcm_recover: ") + snd_strerror(err));
            }
            continue;
        }

        frames_read += static_cast<size_t>(n);
    }

    return frames_read * channels_;
}

bool alsa_audio_input_stream::wait_write_completed(int timeout_ms)
{
    // Not applicable for input streams
    (void)timeout_ms;
    return true;
}

bool alsa_audio_input_stream::eof()
{
    return false;
}

void alsa_audio_input_stream::start()
{
    if (started_ || !start_stop_enabled_)
    {
        return;
    }

    if (impl_ && impl_->pcm_handle_)
    {
        int err = 0;
        if ((err = snd_pcm_start(impl_->pcm_handle_)) != 0)
        {
            throw std::runtime_error(std::string("snd_pcm_start: ") + snd_strerror(err));
        }
    }

    started_ = true;
}

void alsa_audio_input_stream::stop()
{
    if (!started_ || !start_stop_enabled_)
    {
        return;
    }

    if (impl_ && impl_->pcm_handle_)
    {
        int err = 0;

        if ((err = snd_pcm_drop(impl_->pcm_handle_)) != 0)
        {
            throw std::runtime_error(std::string("snd_pcm_drop: ") + snd_strerror(err));
        }

        if ((err = snd_pcm_prepare(impl_->pcm_handle_)) != 0)
        {
            throw std::runtime_error(std::string("snd_pcm_prepare: ") + snd_strerror(err));
        }
    }

    started_ = false;
}

void alsa_audio_input_stream::enable_start_stop(bool enable)
{
    start_stop_enabled_ = enable;
}

bool alsa_audio_input_stream::enable_start_stop()
{
    return start_stop_enabled_;
}

std::vector<alsa_audio_stream_control> alsa_audio_input_stream::controls()
{
    std::vector<alsa_audio_stream_control> controls;

    if (card_id < 0)
    {
        return controls;
    }

    int err = 0;

    snd_mixer_t* mixer;
    if ((err = snd_mixer_open(&mixer, 0)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_open: ") + snd_strerror(err));
    }

    std::unique_ptr<snd_mixer_t, int(*)(snd_mixer_t*)> mixer_guard(mixer, snd_mixer_close);
    (void)mixer_guard;

    std::string card_name = "hw:" + std::to_string(card_id);

    if ((err = snd_mixer_attach(mixer, card_name.c_str())) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_attach: ") + snd_strerror(err));
    }

    if ((err = snd_mixer_selem_register(mixer, nullptr, nullptr)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_selem_register: ") + snd_strerror(err));
    }

    if ((err = snd_mixer_load(mixer)) != 0)
    {
        throw std::runtime_error(std::string("snd_mixer_load: ") + snd_strerror(err));
    }

    for (snd_mixer_elem_t* elem = snd_mixer_first_elem(mixer); elem != nullptr; elem = snd_mixer_elem_next(elem))
    {
        if (!snd_mixer_selem_is_active(elem))
        {
            continue;
        }

        if (snd_mixer_selem_has_capture_volume(elem) || snd_mixer_selem_has_capture_switch(elem))
        {
            const char* name = snd_mixer_selem_get_name(elem);
            int index = snd_mixer_selem_get_index(elem);

            for (int channel = 0; channel <= SND_MIXER_SCHN_LAST; channel++)
            {
                if (snd_mixer_selem_has_capture_channel(elem, static_cast<snd_mixer_selem_channel_id_t>(channel)))
                {
                    controls.emplace_back(card_id, name, index, channel, audio_stream_type::input);
                }
            }
        }
    }

    return controls;
}

int alsa_audio_input_stream::sample_rate()
{
    return sample_rate_;
}

int alsa_audio_input_stream::channels()
{
    return channels_;
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

    if (sfinfo.frames >= 0)
    {
        total_frames_ = static_cast<size_t>(sfinfo.frames);
    }

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

audio_stream_type wav_audio_input_stream::type()
{
    return audio_stream_type::input;
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
    (void)samples;
    (void)count;
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

bool wav_audio_input_stream::eof()
{
    if (!impl_ || impl_->sf_file_ == nullptr)
    {
        return true;
    }
    sf_count_t current_pos = sf_seek(impl_->sf_file_, 0, SEEK_CUR);
    return current_pos >= static_cast<sf_count_t>(total_frames_);
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

wav_audio_output_stream& wav_audio_output_stream::operator=(wav_audio_input_stream& rhs)
{
    if (this->sample_rate() != rhs.sample_rate())
    {
        throw std::runtime_error("Cannot assign wav_audio_input_stream to wav_audio_output_stream with different sample rate or channels");
    }

    std::vector<double> buffer(1024);
    while (true)
    {
        size_t n = rhs.read(buffer.data(), buffer.size());
        if (n == 0)
        {
            break;
        }
        this->write(buffer.data(), n);
    }

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

audio_stream_type wav_audio_output_stream::type()
{
    return audio_stream_type::output;
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

bool wav_audio_output_stream::eof()
{
    return false;
}

size_t wav_audio_output_stream::read(double* samples, size_t count)
{
    (void)samples;
    (void)count;
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

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_audio_stream_control_client_impl                             //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_audio_stream_control_client_impl
{
    boost::asio::io_context io_context;
    boost::asio::ip::tcp::socket socket { io_context };
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// request                                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

nlohmann::json handle_request(tcp_audio_stream_control_client& client, tcp_audio_stream_control_client_impl& impl, const nlohmann::json& request)
{
    if (!client.connected())
    {
        throw std::runtime_error("Client not connected");
    }

    // Send the request

    std::string data = request.dump();
    uint32_t length = boost::endian::native_to_big(static_cast<uint32_t>(data.size()));

    boost::asio::write(impl.socket, boost::asio::buffer(&length, sizeof(length)));
    boost::asio::write(impl.socket, boost::asio::buffer(data));

    // Receive the response

    boost::asio::read(impl.socket, boost::asio::buffer(&length, sizeof(length)));
    data.resize(boost::endian::big_to_native(length));
    boost::asio::read(impl.socket, boost::asio::buffer(data.data(), data.size()));

    nlohmann::json response = nlohmann::json::parse(data);

    if (response.contains("error"))
    {
        std::string error_message = response["error"].get<std::string>();
        throw std::runtime_error(error_message);
    }

    return response;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_audio_stream_control_client                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

tcp_audio_stream_control_client::tcp_audio_stream_control_client() : impl_(std::make_unique<tcp_audio_stream_control_client_impl>())
{
}

tcp_audio_stream_control_client::~tcp_audio_stream_control_client() = default;

tcp_audio_stream_control_client::tcp_audio_stream_control_client(tcp_audio_stream_control_client&& other) noexcept : impl_(std::move(other.impl_)), connected_(other.connected_)
{
    other.connected_ = false;
}

tcp_audio_stream_control_client& tcp_audio_stream_control_client::operator=(tcp_audio_stream_control_client&& other) noexcept
{
    if (this != &other)
    {
        try
        {
            disconnect();
        }
        catch (...)
        {
            // Ignore
        }

        impl_ = std::move(other.impl_);
        connected_ = other.connected_;
        other.connected_ = false;
    }

    return *this;
}

bool tcp_audio_stream_control_client::connect(const std::string& host, int port)
{
    try
    {
        boost::asio::ip::tcp::resolver resolver(impl_->io_context);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        boost::asio::connect(impl_->socket, endpoints);
        connected_ = true;
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

void tcp_audio_stream_control_client::disconnect()
{
    if (connected_)
    {
        boost::system::error_code ec;
        impl_->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        impl_->socket.close(ec);
        connected_ = false;
    }
}

bool tcp_audio_stream_control_client::connected() const
{
    return connected_;
}

std::string tcp_audio_stream_control_client::name()
{
    // Get the name from the server
    //
    // Request: { "command": "get_name" }
    // Response: { "value": "<string>" }

    return handle_request(*this, *impl_, { {"command", "get_name"} })["value"].get<std::string>();
}

audio_stream_type tcp_audio_stream_control_client::type()
{
    // Get the type from the server
    //
    // Request: { "command": "get_type" }
    // Response: { "value": "<string>" }

    return parse_audio_stream_type(handle_request(*this, *impl_, { {"command", "get_type"} })["value"].get<std::string>());
}

void tcp_audio_stream_control_client::volume(int percent)
{
    // Set the volume on the server
    //
    // Request: { "command": "set_volume", "value": <int> }
    // Response: { "value": "ok" }

    handle_request(*this, *impl_, { {"command", "set_volume"}, {"value", percent} });
}

int tcp_audio_stream_control_client::volume()
{
    // Get the volume from the server
    //
    // Request: { "command": "get_volume" }
    // Response: { "value": <int> }

    return handle_request(*this, *impl_, { {"command", "get_volume"} })["value"].get<int>();
}

int tcp_audio_stream_control_client::sample_rate()
{
    // Get the sample rate from the server
    //
    // Request: { "command": "get_sample_rate" }
    // Response: { "value": <int> }

    return handle_request(*this, *impl_, { {"command", "get_sample_rate"} })["value"].get<int>();
}

int tcp_audio_stream_control_client::channels()
{
    // Get the channels from the server
    //
    // Request: { "command": "get_channels" }
    // Response: { "value": <int> }

    return handle_request(*this, *impl_, { {"command", "get_channels"} })["value"].get<int>();
}

void tcp_audio_stream_control_client::start()
{
    // Start the stream on the server
    //
    // Request: { "command": "start" }
    // Response: { "value": "ok" }

    handle_request(*this, *impl_, { {"command", "start"} });
}

void tcp_audio_stream_control_client::stop()
{
    // Stop the stream on the server
    //
    // Request: { "command": "stop" }
    // Response: { "value": "ok" }

    handle_request(*this, *impl_, { {"command", "stop"} });
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_audio_stream_control_server_impl                             //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_audio_stream_control_server_impl
{
    boost::asio::io_context io_context;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor;
    std::unique_ptr<boost::asio::ip::tcp::socket> client_socket;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_audio_stream_control_server                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

tcp_audio_stream_control_server::tcp_audio_stream_control_server() : impl_(std::make_unique<tcp_audio_stream_control_server_impl>())
{
}

tcp_audio_stream_control_server::tcp_audio_stream_control_server(audio_stream_base& stream) : stream_(stream), impl_(std::make_unique<tcp_audio_stream_control_server_impl>())
{
}

tcp_audio_stream_control_server::tcp_audio_stream_control_server(tcp_audio_stream_control_server&& other) noexcept : stream_(std::move(other.stream_)), impl_(std::make_unique<tcp_audio_stream_control_server_impl>())
{
    assert(!other.running_);
}

tcp_audio_stream_control_server& tcp_audio_stream_control_server::operator=(tcp_audio_stream_control_server&& other) noexcept
{
    if (this != &other)
    {
        assert(!running_);
        assert(!other.running_);

        stream_ = std::move(other.stream_);
        impl_ = std::make_unique<tcp_audio_stream_control_server_impl>();
    }

    return *this;
}

tcp_audio_stream_control_server::~tcp_audio_stream_control_server()
{
    stop();
}

bool tcp_audio_stream_control_server::start(const std::string& host, int port)
{
    if (!stream_.has_value())
    {
        return false;
    }

    try
    {
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::make_address(host), static_cast<unsigned short>(port));
        impl_->acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(impl_->io_context, endpoint);
        impl_->acceptor->set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        running_ = true;

        thread_ = std::jthread(&tcp_audio_stream_control_server::run, this);

        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return ready_; });

        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

void tcp_audio_stream_control_server::stop()
{
    running_ = false;

    if (impl_ != nullptr && impl_->client_socket != nullptr && impl_->client_socket->is_open())
    {
        boost::system::error_code ec;
        impl_->client_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        impl_->client_socket->close(ec);
    }

    if (impl_ != nullptr && impl_->acceptor != nullptr && impl_->acceptor->is_open())
    {
        boost::system::error_code ec;
        impl_->acceptor->close(ec);
    }

    if (thread_.joinable())
    {
        thread_.join();
    }
}

void tcp_audio_stream_control_server::throw_if_faulted()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (exception_)
    {
        std::exception_ptr ex = exception_;
        exception_ = nullptr;
        std::rethrow_exception(ex);
    }
}

bool tcp_audio_stream_control_server::faulted()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return exception_ != nullptr;
}

void tcp_audio_stream_control_server::run()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = true;
    }
    cv_.notify_one();

    try
    {
        run_internal();
    }
    catch (...)
    {
        running_ = false;

        std::lock_guard<std::mutex> lock(mutex_);
        exception_ = std::current_exception();
        cv_.notify_all();
    }

    running_ = false;
}

void tcp_audio_stream_control_server::run_internal()
{
    while (running_)
    {
        impl_->client_socket = std::make_unique<boost::asio::ip::tcp::socket>(impl_->io_context);
        impl_->acceptor->accept(*impl_->client_socket);

        while (running_ && impl_->client_socket->is_open())
        {
            try
            {
                // Read the request from the client
                // Read the request length, then read the request data
                // Parse the request, identify the command and handle it, encode the response as a string
                // Write the response length, then write the response data to the client

                uint32_t request_length;
                boost::asio::read(*impl_->client_socket, boost::asio::buffer(&request_length, sizeof(request_length)));

                std::string request_data(boost::endian::big_to_native(request_length), '\0');
                boost::asio::read(*impl_->client_socket, boost::asio::buffer(request_data.data(), request_data.size()));

                std::string response_data = handle_request(request_data);

                uint32_t response_length = boost::endian::native_to_big(static_cast<uint32_t>(response_data.size()));

                boost::asio::write(*impl_->client_socket, boost::asio::buffer(&response_length, sizeof(response_length)));
                boost::asio::write(*impl_->client_socket, boost::asio::buffer(response_data));

            }
            catch (const boost::system::system_error& e)
            {
                if (e.code() == boost::asio::error::eof || e.code() == boost::asio::error::connection_reset ||
                    e.code() == boost::asio::error::connection_aborted || e.code() == boost::asio::error::broken_pipe)
                {
                    break;
                }
                throw;
            }
        }
    }
}

std::string tcp_audio_stream_control_server::handle_request(const std::string& data)
{
    audio_stream_base& stream = stream_->get();

    nlohmann::json request = nlohmann::json::parse(data);

    std::string command = request.value("command", "");

    nlohmann::json response;

    if (command == "get_name")
    {
        // Get the name of the stream
        //
        // Request: { "command": "get_name" }
        // Response: { "value": "<string>" }

        response["value"] = stream.name();
    }
    else if (command == "get_type")
    {
        // Get the type of the stream
        //
        // Request: { "command": "get_type" }
        // Response: { "value": "<string>" }

        response["value"] = to_string(stream.type());
    }
    else if (command == "get_volume")
    {
        // Get the volume of the stream
        //
        // Request: { "command": "get_volume" }
        // Response: { "value": <int> }

        response["value"] = stream.volume();
    }
    else if (command == "set_volume")
    {
        // Set the volume of the stream
        //
        // Request: { "command": "set_volume", "value": <int> }
        // Response: { "value": "ok" }

        stream.volume(request.value("value", 0));
        response["value"] = "ok";
    }
    else if (command == "get_sample_rate")
    {
        // Get the sample rate of the stream
        //
        // Request: { "command": "get_sample_rate" }
        // Response: { "value": <int> }

        response["value"] = stream.sample_rate();
    }
    else if (command == "get_channels")
    {
        // Get the number of channels of the stream
        //
        // Request: { "command": "get_channels" }
        // Response: { "value": <int> }

        response["value"] = stream.channels();
    }
    else if (command == "start")
    {
        // Start the stream on the server
        //
        // Request: { "command": "start" }
        // Response: { "value": "ok" }

        stream.start();
        response["value"] = "ok";
    }
    else if (command == "stop")
    {
        // Stop the stream on the server
        //
        // Request: { "command": "stop" }
        // Response: { "value": "ok" }

        stream.stop();
        response["value"] = "ok";
    }
    else
    {
        // Unknown command
        //
        // Request: { "command": "<string>" }
        // Response: { "error": "unknown command: <unknown>" }

        response["error"] = "unknown command: " + command;
    }

    std::string response_string = response.dump();

    return response_string;
}

LIBMODEM_NAMESPACE_END