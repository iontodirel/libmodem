#include "audio_stream.h"

#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <vector>

#include <iostream>

#if WIN32

#include <Functiondiscoverykeys_devpkey.h>
#include <atlbase.h>

#endif

// **************************************************************** //
//                                                                  //
//                                                                  //
//  Win32 utils                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if WIN32

std::string wide_to_utf8(const wchar_t* s)
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
        result = wide_to_utf8(variant.pwszVal);
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
        result = wide_to_utf8(buffer);
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

size_t audio_stream::write(const double* samples, size_t count)
{
    return stream_->write(samples, count);
}

size_t audio_stream::read(double* samples, size_t count)
{
    return stream_->read(samples, count);
}

audio_stream::operator bool() const { return stream_ != nullptr; }

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
#if WIN32
    id = other.id;
    name = other.name;
    description = other.description;
    type = other.type;
    state = other.state;
    container_id = other.container_id;
    device_ = other.device_;

    if (device_)
    {
        device_->AddRef();
    }
#endif
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
        id = wide_to_utf8(id_wstr);
        CoTaskMemFree(id_wstr);
    }

    CComPtr<IPropertyStore> props;
    if (SUCCEEDED(device_->OpenPropertyStore(STGM_READ, &props)))
    {
        description = get_string_property(props.p, PKEY_Device_FriendlyName);
        name = get_string_property(props.p, PKEY_Device_DeviceDesc);
        container_id = get_guid_property(props.p, PKEY_Device_ContainerId);

		//std::string instance_id = get_string_property(props.p, PKEY_Device_InstanceId);

  //      PROPVARIANT varDeviceId;
  //      props->GetValue(PKEY_Device_InstanceId, &varDeviceId);

  //      IDeviceTopology* pTopology = nullptr;
  //      device_->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, NULL, (void**)&pTopology);

  //      // Get connector at index 0
  //      IConnector* pConnector = nullptr;
  //      pTopology->GetConnector(0, &pConnector);

  //      // Walk to the connected device
  //      IConnector* pConnectedTo = nullptr;
  //      pConnector->GetConnectedTo(&pConnectedTo);

  //      // Get the part
  //      IPart* pPart = nullptr;
  //      pConnectedTo->QueryInterface(__uuidof(IPart), (void**)&pPart);

  //      // Get the topology of the connected part (the actual device)
  //      IDeviceTopology* pDeviceTopology = nullptr;
  //      pPart->GetTopologyObject(&pDeviceTopology);

  //      // NOW get the device ID (this is the PnP device instance ID)
  //      LPWSTR pwszDeviceId = nullptr;
  //      pDeviceTopology->GetDeviceId(&pwszDeviceId);

        printf("\n");
    }
}

#endif

audio_device& audio_device::operator=(const audio_device& other)
{
    if (this != &other)
    {
#if WIN32
        if (device_)
        {
            device_->Release();
            device_ = nullptr;
        }

        id = other.id;
        name = other.name;
        description = other.description;
        type = other.type;
        state = other.state;
        container_id = other.container_id;
        device_ = other.device_;

        if (device_)
        {
            device_->AddRef();
        }
#endif
    }
    return *this;
}

std::unique_ptr<audio_stream_base> audio_device::stream()
{
#if WIN32
    auto stream = std::make_unique<wasapi_audio_stream>(device_);
    stream->start();
    return stream;
#endif
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
// try_get_audio_device_by_description                              //
// try_get_default_audio_device                                     //
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

        audio_device device(dev.Detach());

        devices.push_back(device);
    }
#endif

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

bool try_get_default_audio_device(audio_device& device)
{
#if WIN32
    HRESULT hr;

    if (FAILED(hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        return false;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator)))
    {
        return false;
    }

    IMMDevice* device_ = nullptr;

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device_);

    enumerator->Release();

    if (FAILED(hr))
    {
        return false;
    }

    device = audio_device(device_);
#endif

    return true;
}


// **************************************************************** //
//                                                                  //
//                                                                  //
// wasapi_audio_stream                                              //
//                                                                  //
//                                                                  //
// **************************************************************** //

#if WIN32

wasapi_audio_stream::wasapi_audio_stream(IMMDevice* device)
{
	device_ = device;

    if (device_)
    {
        device_->AddRef();
	}
}

wasapi_audio_stream::wasapi_audio_stream(const wasapi_audio_stream& other)
{
    device_ = other.device_;
    audio_client_ = other.audio_client_;
    render_client_ = other.render_client_;
    endpoint_volume_ = other.endpoint_volume_;
    buffer_size_ = other.buffer_size_;
    sample_rate_ = other.sample_rate_;
    num_channels_ = other.num_channels_;

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

wasapi_audio_stream& wasapi_audio_stream::operator=(const wasapi_audio_stream& other)
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
        num_channels_ = other.num_channels_;

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

void wasapi_audio_stream::start()
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
    num_channels_ = device_format->nChannels;

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

wasapi_audio_stream::wasapi_audio_stream()
{
    HRESULT hr;

    if (FAILED(hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        throw std::runtime_error("COM init failed");
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator)))
    {
        throw std::runtime_error("Failed to create enumerator");
    }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device_);

    enumerator->Release();

    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to get device");
    }
}

wasapi_audio_stream::~wasapi_audio_stream()
{
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

std::string wasapi_audio_stream::name()
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

void wasapi_audio_stream::volume(int percent)
{
    percent = std::clamp(percent, 0, 100);

    float volume_scalar = percent / 100.0f;

    HRESULT hr = endpoint_volume_->SetMasterVolumeLevelScalar(volume_scalar, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to set volume");
    }
}

int wasapi_audio_stream::volume()
{
    float volume_scalar;

    HRESULT hr = endpoint_volume_->GetMasterVolumeLevelScalar(&volume_scalar);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to get volume");
    }

    return static_cast<int>(volume_scalar * 100.0f + 0.5f);
}

int wasapi_audio_stream::sample_rate()
{
    return sample_rate_;
}

size_t wasapi_audio_stream::write(const double* samples, size_t count)
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

        UINT32 frames_to_write = (std::min)(available_frames, static_cast<UINT32>(count));

        BYTE* buffer;
        if (FAILED(hr = render_client_->GetBuffer(frames_to_write, &buffer)))
        {
            return samples_written;
        }

        float* float_buffer = reinterpret_cast<float*>(buffer);

        for (UINT32 i = 0; i < frames_to_write; i++)
        {
            float sample = static_cast<float>(samples[i]);

            for (int channel = 0; channel < num_channels_; channel++)
            {
                float_buffer[i * num_channels_ + channel] = sample;
            }
        }

        if (FAILED(hr = render_client_->ReleaseBuffer(frames_to_write, 0)))
        {
            return samples_written;
        }

        samples_written += frames_to_write;
    }

	wait_write_completed(-1);

    return samples_written;
}

bool wasapi_audio_stream::wait_write_completed(int timeout_ms)
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

size_t wasapi_audio_stream::read(double* samples, size_t count)
{
    return 0;
}

void wasapi_audio_stream::mute(bool mute)
{	
	BOOL mute_state = mute ? TRUE : FALSE;
    HRESULT hr = endpoint_volume_->SetMute(mute_state, nullptr);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to set mute state");
    }
}

bool wasapi_audio_stream::mute()
{
    BOOL muted;
    HRESULT hr = endpoint_volume_->GetMute(&muted);
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to get mute state");
	}
    return muted == TRUE;
}

#endif // WIN32

























tcp_client_audio_stream::tcp_client_audio_stream(const char* host, int audio_port, int control_port)
    : socket_(io_context_)
    , control_socket_(io_context_)
    , volume_(100)
    , sample_rate_(44100)
    , host_(host)
    , audio_port_(audio_port)
    , control_port_(control_port)
{
    try
    {
        boost::asio::ip::tcp::resolver resolver(io_context_);

        // Connect data socket to the specified port
        auto data_endpoints = resolver.resolve(host, std::to_string(audio_port));
        boost::asio::connect(socket_, data_endpoints);

        // Connect control socket to port + 1 (you can adjust this convention)
        auto control_endpoints = resolver.resolve(host, std::to_string(control_port));
        boost::asio::connect(control_socket_, control_endpoints);

        // Get initial sample rate from server via control channel
        json request = {
            {"command", "get_sample_rate"}
        };
        send_control_command(request);
        json response = receive_control_response();
        sample_rate_ = response["sample_rate"];
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error(std::string("TCP connection failed: ") + e.what());
    }
}

bool tcp_client_audio_stream::wait_write_completed(int timeout_ms)
{
    // For TCP, we assume writes are immediate; implement proper flow control if needed
    return true;
}

tcp_client_audio_stream::~tcp_client_audio_stream()
{
    try
    {
        if (control_socket_.is_open())
        {
            json request = {
                {"command", "disconnect"}
            };
            send_control_command(request);
            control_socket_.close();
        }
        if (socket_.is_open())
        {
            socket_.close();
        }
    }
    catch (...)
    {
        // Suppress exceptions in destructor
    }
}

std::string tcp_client_audio_stream::name()
{
    return host_ + "_" + std::to_string(audio_port_) + "_" + std::to_string(control_port_);
}

void tcp_client_audio_stream::volume(int percent)
{
    if (percent < 0 || percent > 100)
    {
        throw std::invalid_argument("Volume must be between 0 and 100");
    }

    volume_ = percent;

    json request = {
        {"command", "set_volume"},
        {"volume", percent}
    };
    send_control_command(request);

    json response = receive_control_response();
    if (response["status"] != "ok")
    {
        throw std::runtime_error("Failed to set volume");
    }
}

int tcp_client_audio_stream::volume()
{
    return volume_;
}

int tcp_client_audio_stream::sample_rate()
{
    return sample_rate_;
}

size_t tcp_client_audio_stream::write(const double* samples, size_t count)
{
    if (!samples || count == 0)
    {
        return 0;
    }

    try
    {
        // Send raw audio data directly on data socket
        size_t bytes_to_send = count * sizeof(double);
        size_t bytes_sent = boost::asio::write(socket_,
            boost::asio::buffer(samples, bytes_to_send));

        return bytes_sent / sizeof(double);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error(std::string("Write failed: ") + e.what());
    }
}

size_t tcp_client_audio_stream::read(double* samples, size_t count)
{
    if (!samples || count == 0)
    {
        return 0;
    }

    try
    {
        // Read raw audio data directly from data socket
        size_t bytes_to_read = count * sizeof(double);
        size_t bytes_read = boost::asio::read(socket_,
            boost::asio::buffer(samples, bytes_to_read),
            boost::asio::transfer_exactly(bytes_to_read));

        return bytes_read / sizeof(double);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error(std::string("Read failed: ") + e.what());
    }
}

void tcp_client_audio_stream::send_control_command(const json& cmd)
{
    std::string message = cmd.dump() + "\n";
    boost::asio::write(control_socket_, boost::asio::buffer(message));
}

nlohmann::json tcp_client_audio_stream::receive_control_response()
{
    boost::asio::streambuf buffer;
    boost::asio::read_until(control_socket_, buffer, "\n");

    std::istream is(&buffer);
    std::string response_str;
    std::getline(is, response_str);

    // Remove trailing carriage return if present
    if (!response_str.empty() && response_str.back() == '\r')
    {
        response_str.pop_back();
    }

    return json::parse(response_str);
}

input_wav_audio_stream::input_wav_audio_stream(const std::string& filename)
    : sf_file_(nullptr)
    , filename_(filename)
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

input_wav_audio_stream::~input_wav_audio_stream()
{
    if (sf_file_)
    {
        sf_close(sf_file_);
        sf_file_ = nullptr;
    }
}

std::string input_wav_audio_stream::name()
{
    return filename_;
}

void input_wav_audio_stream::volume(int percent)
{
    (void)percent;
}

int input_wav_audio_stream::volume()
{
    return 100;
}

int input_wav_audio_stream::sample_rate()
{
    return sample_rate_;
}

size_t input_wav_audio_stream::write(const double* samples, size_t count)
{
    return 0;
}

bool input_wav_audio_stream::wait_write_completed(int timeout_ms)
{
    (void)timeout_ms;
    return true;
}

size_t input_wav_audio_stream::read(double* samples, size_t count)
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

void input_wav_audio_stream::flush()
{
    if (sf_file_)
    {
        sf_write_sync(sf_file_);
    }
}

void input_wav_audio_stream::close()
{
    if (sf_file_)
    {
        sf_close(sf_file_);
        sf_file_ = nullptr;
    }
}

output_wav_audio_stream::output_wav_audio_stream(const std::string& filename, int sample_rate)
    : sf_file_(nullptr)
    , sample_rate_(sample_rate)
    , filename_(filename)
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

output_wav_audio_stream::~output_wav_audio_stream()
{
    if (sf_file_)
    {
        sf_close(sf_file_);
        sf_file_ = nullptr;
    }
}

std::string output_wav_audio_stream::name()
{
    return filename_;
}

void output_wav_audio_stream::volume(int percent)
{
    (void)percent;
}

int output_wav_audio_stream::volume()
{
    return 100;
}

int output_wav_audio_stream::sample_rate()
{
    return sample_rate_;
}

size_t output_wav_audio_stream::write(const double* samples, size_t count)
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

bool output_wav_audio_stream::wait_write_completed(int timeout_ms)
{
    (void)timeout_ms;
    return true;
}

size_t output_wav_audio_stream::read(double* samples, size_t count)
{
    return 0;
}

void output_wav_audio_stream::flush()
{
    if (sf_file_)
    {
        sf_write_sync(sf_file_);
    }
}

void output_wav_audio_stream::close()
{
    if (sf_file_)
    {
        sf_close(sf_file_);
        sf_file_ = nullptr;
    }
}