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

#if WIN32

#include <Functiondiscoverykeys_devpkey.h>
#include <atlbase.h>
#include <audiopolicy.h>

#endif // WIN32

#ifdef __linux__

#include <alsa/asoundlib.h>

#endif // __linux__

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
    if (!s || !*s)
    {
        return {};
    }

    int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0)
    {
        return {};
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
    return stream_->name(); 
}

void audio_stream::volume(int percent)
{ 
    stream_->volume(percent);
}

int audio_stream::volume()
{
    return stream_->volume();
}

int audio_stream::sample_rate()
{ 
    return stream_->sample_rate(); 
}

int audio_stream::channels()
{
    return stream_->channels();
}

size_t audio_stream::write(const double* samples, size_t count)
{
    return stream_->write(samples, count);
}

size_t audio_stream::read(double* samples, size_t count)
{
    return stream_->read(samples, count);
}

bool audio_stream::wait_write_completed(int timeout_ms)
{
    return stream_->wait_write_completed(timeout_ms);
}

audio_stream::operator bool() const
{
    return stream_ != nullptr;
}

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

audio_device::audio_device(const audio_device& other)
{
    id = other.id;
    name = other.name;
    description = other.description;
    type = other.type;
    state = other.state;
    type = other.type;

#if WIN32
    container_id = other.container_id;
    device_ = other.device_;

    if (device_)
    {
        device_->AddRef();
    }
#endif // WIN32

#if __linux__
    card_id = other.card_id;
    device_id = other.device_id;
#endif // __linux__
}

#if WIN32

audio_device::audio_device(IMMDevice* device)
{
    device_ = device;

    if (device_)
    {
        device_->AddRef();
    }

    CComPtr<IMMEndpoint> endpoint;
    EDataFlow flow;
    if (FAILED(device_->QueryInterface(__uuidof(IMMEndpoint), reinterpret_cast<void**>(&endpoint))) || FAILED(endpoint->GetDataFlow(&flow)))
    {
        throw std::runtime_error("Failed to get data flow");
    }

    type = (flow == eRender) ? audio_device_type::render : audio_device_type::capture;

    state = audio_device_state::active;

    DWORD state = 0;
    if (FAILED(device_->GetState(&state)))
    {
        throw std::runtime_error("Failed to get device state");
    }

    switch (state)
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
    if (SUCCEEDED(device_->GetId(&id_wstr)) && id_wstr)
    {
        id = utf16_to_utf8(id_wstr);
        CoTaskMemFree(id_wstr);
    }

    CComPtr<IPropertyStore> props;
    if (SUCCEEDED(device_->OpenPropertyStore(STGM_READ, &props)))
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

audio_device& audio_device::operator=(const audio_device& other)
{
    if (this != &other)
    {
        id = other.id;
        name = other.name;
        description = other.description;
        type = other.type;
        state = other.state;

#if WIN32
        if (device_)
        {
            device_->Release();
            device_ = nullptr;
        }

        container_id = other.container_id;
        device_ = other.device_;

        if (device_)
        {
            device_->AddRef();
        }
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
    if (type == audio_device_type::render)
    {
        auto stream = std::make_unique<wasapi_audio_output_stream>(device_);
        stream->start();
        return stream;
    }
    else if (type == audio_device_type::capture)
    {
        auto stream =  std::make_unique<wasapi_audio_input_stream>(device_);
        stream->start();
        return stream;
	}
#endif // WIN32

#if __linux__
    return std::make_unique<alsa_audio_stream>(card_id, device_id, type);
#endif // __linux__

	return nullptr;
}

audio_device::~audio_device()
{
#if WIN32
    if (device_)
    {
        device_->Release();
        device_ = nullptr;
    }
#endif
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

    hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        throw std::runtime_error("COM init failed");
    }

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

        audio_device device(dev.p);

        devices.push_back(device);
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
    for (const auto& device : all_devices)
    {
        if ((state == audio_device_state::unknown || device.state == state) &&
            (type == audio_device_type::unknown || device.type == type))
        {
            filtered_devices.push_back(device);
        }
    }
    return filtered_devices;
}

bool try_get_audio_device_by_name(const std::string& name, audio_device& device, audio_device_type type, audio_device_state state)
{
    std::vector<audio_device> devices = get_audio_devices();

    auto it = std::find_if(devices.begin(), devices.end(), [&name](const audio_device& dev) {
        return dev.name == name;
    });

    if (it != devices.end())
    {
        device = *it;
        return true;
    }

    return false;
}

bool try_get_audio_device_by_description(const std::string& description, audio_device& device, audio_device_type type, audio_device_state state)
{
    std::vector<audio_device> devices = get_audio_devices();

    auto it = std::find_if(devices.begin(), devices.end(), [&description](const audio_device& dev) {
        return dev.description == description;
    });

    if (it != devices.end())
    {
        device = *it;
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
#if WIN32
    HRESULT hr;

    if (FAILED(hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        return false;
    }

    CComPtr<IMMDeviceEnumerator> enumerator = nullptr;
    if (FAILED(hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator)))
    {
        return false;
    }

    CComPtr<IMMDevice> device_ = nullptr;

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device_);

    if (FAILED(hr))
    {
        return false;
    }

    device = audio_device(device_.p);

    return true;
#endif // WIN32

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
    HRESULT hr;

    if (FAILED(hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        throw std::runtime_error("COM init failed");
    }

    CComPtr<IMMDeviceEnumerator> enumerator = nullptr;
    if (FAILED(hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator)))
    {
        throw std::runtime_error("Failed to create enumerator");
    }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device_);

    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to get device");
    }
}

wasapi_audio_output_stream::wasapi_audio_output_stream(IMMDevice* device)
{
	device_ = device;

    if (device_)
    {
        device_->AddRef();
	}
}

wasapi_audio_output_stream::wasapi_audio_output_stream(const wasapi_audio_output_stream& other)
{
    device_ = other.device_;
    audio_client_ = other.audio_client_;
    render_client_ = other.render_client_;
    endpoint_volume_ = other.endpoint_volume_;
    buffer_size_ = other.buffer_size_;
    sample_rate_ = other.sample_rate_;
    channels_ = other.channels_;

    if (device_)
    {
        device_->AddRef();
    }

    if (audio_client_)
    {
        audio_client_->AddRef();
    }

    if (render_client_)
    {
        render_client_->AddRef();
    }

    if (endpoint_volume_)
    {
        endpoint_volume_->AddRef();
    }
}

wasapi_audio_output_stream& wasapi_audio_output_stream::operator=(const wasapi_audio_output_stream& other)
{
    if (this != &other)
    {
        if (endpoint_volume_)
        {
            endpoint_volume_->Release();
        }

        if (render_client_)
        {
            render_client_->Release();
        }

        if (audio_client_)
        {
            audio_client_->Release();
        }

        if (device_)
        {
            device_->Release();
        }

        device_ = other.device_;
        audio_client_ = other.audio_client_;
        render_client_ = other.render_client_;
        endpoint_volume_ = other.endpoint_volume_;
        buffer_size_ = other.buffer_size_;
        sample_rate_ = other.sample_rate_;
        channels_ = other.channels_;

        if (device_)
        {
            device_->AddRef();
        }

        if (audio_client_)
        {
            audio_client_->AddRef();
        }

        if (render_client_)
        {
            render_client_->AddRef();
        }

        if (endpoint_volume_)
        {
            endpoint_volume_->AddRef();
        }
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

    if (endpoint_volume_)
    {
        endpoint_volume_->Release();
        endpoint_volume_ = nullptr;
    }

    if (render_client_)
    {
        render_client_->Release();
        render_client_ = nullptr;
    }

    if (audio_client_)
    {
        audio_client_->Release();
        audio_client_ = nullptr;
    }

    if (device_)
    {
        device_->Release();
        device_ = nullptr;
    }
}

std::string wasapi_audio_output_stream::name()
{
    IPropertyStore* props = nullptr;
    PROPVARIANT variant;

    HRESULT hr;
    if (FAILED(hr = device_->OpenPropertyStore(STGM_READ, &props)))
    {
        return "unknown";
    }

    PropVariantInit(&variant);
    
    if (FAILED(hr = props->GetValue(PKEY_Device_FriendlyName, &variant)))
    {
        props->Release();
        return "unknown";
    }

    std::string name = _com_util::ConvertBSTRToString(variant.bstrVal);

    PropVariantClear(&variant);
    
    props->Release();

    return name;
}

void wasapi_audio_output_stream::mute(bool mute)
{
    BOOL mute_state = mute ? TRUE : FALSE;
    HRESULT hr = endpoint_volume_->SetMute(mute_state, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to set mute state");
    }
}

bool wasapi_audio_output_stream::mute()
{
    BOOL muted;
    HRESULT hr = endpoint_volume_->GetMute(&muted);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to get mute state");
    }
    return muted == TRUE;
}

void wasapi_audio_output_stream::volume(int percent)
{
    percent = std::clamp(percent, 0, 100);

    float volume_scalar = percent / 100.0f;

    HRESULT hr = endpoint_volume_->SetMasterVolumeLevelScalar(volume_scalar, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to set volume");
    }
}

int wasapi_audio_output_stream::volume()
{
    float volume_scalar;

    HRESULT hr = endpoint_volume_->GetMasterVolumeLevelScalar(&volume_scalar);
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
    HRESULT hr;

    size_t samples_written = 0;
    while (samples_written < count)
    {
        UINT32 padding;
        if (FAILED(hr = audio_client_->GetCurrentPadding(&padding)))
        {
            return 0;
        }

        UINT32 available_frames = buffer_size_ - padding;
        if (available_frames == 0)
        {
            continue;
        }

        UINT32 frames_to_write = (std::min)(available_frames, static_cast<UINT32>(count - samples_written));

        BYTE* buffer;
        if (FAILED(hr = render_client_->GetBuffer(frames_to_write, &buffer)))
        {
            return samples_written;
        }

        float* float_buffer = reinterpret_cast<float*>(buffer);

        for (UINT32 i = 0; i < frames_to_write; i++)
        {
            float sample = static_cast<float>(samples[samples_written + i]);

            for (int channel = 0; channel < channels_; channel++)
            {
                float_buffer[i * channels_ + channel] = sample;
            }
        }

        if (FAILED(hr = render_client_->ReleaseBuffer(frames_to_write, 0)))
        {
            return samples_written;
        }

        samples_written += frames_to_write;
    }

    return samples_written;
}

size_t wasapi_audio_output_stream::read(double* samples, size_t count)
{
    // Not implemented for output stream
    (void)samples;
    (void)count;
    return 0;
}

bool wasapi_audio_output_stream::wait_write_completed(int timeout_ms)
{
    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    INT64 timeout_ticks = (static_cast<INT64>(timeout_ms) * freq.QuadPart) / 1000;

    while (true)
    {
        UINT32 padding;
        if (FAILED(audio_client_->GetCurrentPadding(&padding)))
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

	return true;
}

void wasapi_audio_output_stream::start()
{
    HRESULT hr;

    if (FAILED(hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audio_client_)))
    {
        throw std::runtime_error("Failed to activate client");
    }

    if (FAILED(hr = device_->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)&endpoint_volume_)))
    {
        throw std::runtime_error("Failed to get volume control");
    }

    WAVEFORMATEX* device_format = nullptr;
    if (FAILED(hr = audio_client_->GetMixFormat(&device_format)))
    {
        throw std::runtime_error("Failed to get mix format");
    }

    sample_rate_ = device_format->nSamplesPerSec;
    channels_ = device_format->nChannels;

    hr = audio_client_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        50000000,  // 5 second buffer
        0,
        device_format,
        nullptr);

    CoTaskMemFree(device_format);

    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to initialize");
    }

    if (FAILED(hr = audio_client_->GetService(__uuidof(IAudioRenderClient), (void**)&render_client_)))
    {
        throw std::runtime_error("Failed to get render client");
    }

    if (FAILED(hr = audio_client_->GetBufferSize(&buffer_size_)))
    {
        throw std::runtime_error("Failed to get buffer size");
    }

    if (FAILED(hr = audio_client_->Start()))
    {
        throw std::runtime_error("Failed to frame_start");
    }
}

void wasapi_audio_output_stream::stop()
{
    if (audio_client_)
    {
        audio_client_->Stop();
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
    HRESULT hr;

    if (FAILED(hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        throw std::runtime_error("COM init failed");
    }

    CComPtr<IMMDeviceEnumerator> enumerator = nullptr;
    if (FAILED(hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator)))
    {
        throw std::runtime_error("Failed to create enumerator");
    }

    hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device_);

    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to get capture device");
    }
}

wasapi_audio_input_stream::wasapi_audio_input_stream(IMMDevice* device)
{
    device_ = device;

    if (device_)
    {
        device_->AddRef();
    }
}

wasapi_audio_input_stream::wasapi_audio_input_stream(const wasapi_audio_input_stream& other)
{
    device_ = other.device_;
    audio_client_ = other.audio_client_;
    capture_client_ = other.capture_client_;
    endpoint_volume_ = other.endpoint_volume_;
    buffer_size_ = other.buffer_size_;
    sample_rate_ = other.sample_rate_;
    channels_ = other.channels_;

    if (device_)
    {
        device_->AddRef();
    }

    if (audio_client_)
    {
        audio_client_->AddRef();
    }

    if (capture_client_)
    {
        capture_client_->AddRef();
    }

    if (endpoint_volume_)
    {
        endpoint_volume_->AddRef();
    }
}

wasapi_audio_input_stream& wasapi_audio_input_stream::operator=(const wasapi_audio_input_stream& other)
{
    if (this != &other)
    {
        if (endpoint_volume_)
        {
            endpoint_volume_->Release();
        }

        if (capture_client_)
        {
            capture_client_->Release();
        }

        if (audio_client_)
        {
            audio_client_->Release();
        }

        if (device_)
        {
            device_->Release();
        }

        device_ = other.device_;
        audio_client_ = other.audio_client_;
        capture_client_ = other.capture_client_;
        endpoint_volume_ = other.endpoint_volume_;
        buffer_size_ = other.buffer_size_;
        sample_rate_ = other.sample_rate_;
        channels_ = other.channels_;

        if (device_)
        {
            device_->AddRef();
        }

        if (audio_client_)
        {
            audio_client_->AddRef();
        }

        if (capture_client_)
        {
            capture_client_->AddRef();
        }

        if (endpoint_volume_)
        {
            endpoint_volume_->AddRef();
        }
    }
    return *this;
}

wasapi_audio_input_stream::~wasapi_audio_input_stream()
{
    close();
}

void wasapi_audio_input_stream::close()
{
    if (endpoint_volume_)
    {
        endpoint_volume_->Release();
        endpoint_volume_ = nullptr;
    }

    if (capture_client_)
    {
        capture_client_->Release();
        capture_client_ = nullptr;
    }

    if (audio_client_)
    {
        audio_client_->Stop();
        audio_client_->Release();
        audio_client_ = nullptr;
    }

    if (device_)
    {
        device_->Release();
        device_ = nullptr;
    }
}

void wasapi_audio_input_stream::start()
{
    HRESULT hr;

    if (FAILED(hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audio_client_)))
    {
        throw std::runtime_error("Failed to activate client");
    }

    CComPtr<IAudioSessionControl> session_control = nullptr;
    if (SUCCEEDED(audio_client_->GetService(__uuidof(IAudioSessionControl), (void**)&session_control)))
    {
        CComPtr<IAudioSessionControl2> session_control2 = nullptr;
        if (SUCCEEDED(session_control->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&session_control2)))
        {
            session_control2->SetDuckingPreference(TRUE); // Disable ducking
        }
    }

    if (FAILED(hr = device_->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)&endpoint_volume_)))
    {
        throw std::runtime_error("Failed to get volume control");
    }

    WAVEFORMATEX* device_format = nullptr;
    if (FAILED(hr = audio_client_->GetMixFormat(&device_format)))
    {
        throw std::runtime_error("Failed to get mix format");
    }

    sample_rate_ = device_format->nSamplesPerSec;
    channels_ = device_format->nChannels;

    hr = audio_client_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        10000000,  // 1 second buffer
        0,
        device_format,
        nullptr);

    CoTaskMemFree(device_format);

    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to initialize");
    }

    if (FAILED(hr = audio_client_->GetService(__uuidof(IAudioCaptureClient), (void**)&capture_client_)))
    {
        throw std::runtime_error("Failed to get capture client");
    }

    if (FAILED(hr = audio_client_->GetBufferSize(&buffer_size_)))
    {
        throw std::runtime_error("Failed to get buffer size");
    }

    if (FAILED(hr = audio_client_->Start()))
    {
        throw std::runtime_error("Failed to start");
    }
}

void wasapi_audio_input_stream::stop()
{
    if (audio_client_)
    {
        audio_client_->Stop();
    }
}

std::string wasapi_audio_input_stream::name()
{
    IPropertyStore* props = nullptr;
    PROPVARIANT variant;

    HRESULT hr;
    if (FAILED(hr = device_->OpenPropertyStore(STGM_READ, &props)))
    {
        return "unknown";
    }

    PropVariantInit(&variant);

    if (FAILED(hr = props->GetValue(PKEY_Device_FriendlyName, &variant)))
    {
        props->Release();
        return "unknown";
    }

    std::string name = utf16_to_utf8(variant.pwszVal);

    PropVariantClear(&variant);

    props->Release();

    return name;
}

void wasapi_audio_input_stream::mute(bool mute)
{
    BOOL mute_state = mute ? TRUE : FALSE;
    HRESULT hr = endpoint_volume_->SetMute(mute_state, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to set mute state");
    }
}

bool wasapi_audio_input_stream::mute()
{
    BOOL muted;
    HRESULT hr = endpoint_volume_->GetMute(&muted);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to get mute state");
    }
    return muted == TRUE;
}

void wasapi_audio_input_stream::volume(int percent)
{
    percent = std::clamp(percent, 0, 100);

    float volume_scalar = percent / 100.0f;

    HRESULT hr = endpoint_volume_->SetMasterVolumeLevelScalar(volume_scalar, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to set volume");
    }
}

int wasapi_audio_input_stream::volume()
{
    float volume_scalar;

    HRESULT hr = endpoint_volume_->GetMasterVolumeLevelScalar(&volume_scalar);
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

size_t wasapi_audio_input_stream::read(double* samples, size_t count)
{
    if (!capture_client_ || !samples || count == 0)
    {
        return 0;
    }

    HRESULT hr;
    size_t samples_read = 0;

    while (samples_read < count)
    {
        UINT32 packet_length = 0;
        if (FAILED(hr = capture_client_->GetNextPacketSize(&packet_length)))
        {
            throw std::runtime_error("Failed to get packet size");
        }

        if (packet_length == 0)
        {
            break;
        }

        BYTE* data = nullptr;
        UINT32 frames_available = 0;
        DWORD flags = 0;

        if (FAILED(hr = capture_client_->GetBuffer(&data, &frames_available, &flags, nullptr, nullptr)))
        {
            throw std::runtime_error("Failed to get buffer");
        }

        float* float_data = reinterpret_cast<float*>(data);

        UINT32 frames_to_read = (std::min)(frames_available, static_cast<UINT32>(count - samples_read));

        for (UINT32 i = 0; i < frames_to_read; i++)
        {
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
            {
                samples[samples_read + i] = 0.0;
            }
            else
            {
                // Average all channels to mono
                double sum = 0.0;
                for (WORD ch = 0; ch < channels_; ch++)
                {
                    sum += float_data[i * channels_ + ch];
                }
                samples[samples_read + i] = sum / channels_;
            }
        }

        if (FAILED(hr = capture_client_->ReleaseBuffer(frames_available)))
        {
            throw std::runtime_error("Failed to release buffer");
        }

        samples_read += frames_to_read;
    }

    return samples_read;
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
        throw std::runtime_error("Could not open playback or capture device");
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
// wav_audio_input_stream                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

wav_audio_input_stream::wav_audio_input_stream(const std::string& filename) : sf_file_(nullptr), filename_(filename)
{
    SF_INFO sfinfo = {};

    sf_file_ = sf_open(filename.c_str(), SFM_READ, &sfinfo);
    if (!sf_file_)
    {
        throw std::runtime_error(std::string("Failed to open WAV file: ") + sf_strerror(nullptr));
    }

    // When reading, use the file's actual sample rate
    sample_rate_ = sfinfo.samplerate;
    channels_ = sfinfo.channels;
}

wav_audio_input_stream::~wav_audio_input_stream()
{
    if (sf_file_)
    {
        sf_close(sf_file_);
        sf_file_ = nullptr;
    }
}

std::string wav_audio_input_stream::name()
{
    return filename_;
}

void wav_audio_input_stream::volume(int percent)
{
    (void)percent;
}

int wav_audio_input_stream::volume()
{
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
    return 0;
}

bool wav_audio_input_stream::wait_write_completed(int timeout_ms)
{
    (void)timeout_ms;
    return true;
}

size_t wav_audio_input_stream::read(double* samples, size_t count)
{
    if (!sf_file_)
        return 0;

    if (channels_ != 1)
    {
        throw std::runtime_error("Only mono WAV files are supported for reading");
    }

    size_t total_read = 0;
    while (total_read < count)
    {
        std::vector<int16_t> buffer(count);
        int read_count = sf_read_short(sf_file_, buffer.data(), count);
        if (read_count == 0)
        {
            break;
        }
        for (size_t i = 0; i < read_count; i++)
        {
            double val = buffer[i] / 32767.0;
            samples[total_read++] = val;
        }
    }

    return total_read;
}

void wav_audio_input_stream::flush()
{
    if (sf_file_)
    {
        sf_write_sync(sf_file_);
    }
}

void wav_audio_input_stream::close()
{
    if (sf_file_)
    {
        sf_close(sf_file_);
        sf_file_ = nullptr;
    }
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// wav_audio_output_stream                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

wav_audio_output_stream::wav_audio_output_stream(const std::string& filename, int sample_rate) : sf_file_(nullptr), sample_rate_(sample_rate), filename_(filename)
{
    SF_INFO sfinfo = {};

    // Set format for writing
    sfinfo.samplerate = sample_rate;
    sfinfo.channels = 1;
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    sf_file_ = sf_open(filename.c_str(), SFM_WRITE, &sfinfo);
    if (!sf_file_)
    {
        throw std::runtime_error(std::string("Failed to open WAV file: ") + sf_strerror(nullptr));
    }
}

wav_audio_output_stream::~wav_audio_output_stream()
{
    if (sf_file_)
    {
        sf_close(sf_file_);
        sf_file_ = nullptr;
    }
}

std::string wav_audio_output_stream::name()
{
    return filename_;
}

void wav_audio_output_stream::volume(int percent)
{
    (void)percent;
}

int wav_audio_output_stream::volume()
{
    return 100;
}

int wav_audio_output_stream::sample_rate()
{
    return sample_rate_;
}

int wav_audio_output_stream::channels()
{
    return channels_;
}

size_t wav_audio_output_stream::write(const double* samples, size_t count)
{
    if (!sf_file_)
        return 0;

    for (size_t i = 0; i < count; i++)
    {
        int16_t pcm = static_cast<int16_t>(samples[i] * 32767.0);
        sf_writef_short(sf_file_, &pcm, 1);
    }

    return count;
}

bool wav_audio_output_stream::wait_write_completed(int timeout_ms)
{
    (void)timeout_ms;
    return true;
}

size_t wav_audio_output_stream::read(double* samples, size_t count)
{
    return 0;
}

void wav_audio_output_stream::flush()
{
    if (sf_file_)
    {
        sf_write_sync(sf_file_);
    }
}

void wav_audio_output_stream::close()
{
    if (sf_file_)
    {
        sf_close(sf_file_);
        sf_file_ = nullptr;
    }
}

LIBMODEM_NAMESPACE_END