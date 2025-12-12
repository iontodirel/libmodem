#pragma once

#if WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <comdef.h>

#pragma comment(lib, "ole32.lib")

#endif // WIN32

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

#include <sndfile.h>

struct audio_stream_base
{
    virtual ~audio_stream_base() = default;

    virtual std::string name() = 0;

    virtual void volume(int percent) = 0;
    virtual int volume() = 0;

    virtual int sample_rate() = 0;

    virtual size_t write(const double* samples, size_t count) = 0;
    virtual size_t read(double* samples, size_t count) = 0;
};

class audio_stream : public audio_stream_base
{
public:
    audio_stream(std::unique_ptr<audio_stream_base> s);

    audio_stream(audio_stream&&) = default;
    audio_stream& operator=(audio_stream&&) = default;
    audio_stream(const audio_stream&) = delete;
    audio_stream& operator=(const audio_stream&) = delete;

    std::string name();
    void volume(int percent);
    int volume();
    int sample_rate();

    size_t write(const double* samples, size_t count);
    size_t read(double* samples, size_t count);

    explicit operator bool() const;

private:
    std::unique_ptr<audio_stream_base> stream_;
};

enum class audio_device_type : int
{
    unknown = 0,
    capture = 1,
    render = 2
};

enum class audio_device_state
{
    unknown,
    active,
    disabled,
    unplugged,
    not_present
};

struct audio_device
{
friend std::vector<audio_device> get_audio_devices();

    audio_device();

#if WIN32
    audio_device(IMMDevice* device);
#endif

    audio_device& operator=(const audio_device& other);
	audio_device(const audio_device&);
    ~audio_device();

	std::unique_ptr<audio_stream_base> stream();

    std::string id;
    std::string name;
    std::string description;
    audio_device_type type = audio_device_type::unknown;
    audio_device_state state = audio_device_state::active;
	std::string container_id;

private:

#if WIN32
	IMMDevice* device_ = nullptr;
#endif
};

std::vector<audio_device> get_audio_devices();
std::vector<audio_device> get_audio_devices(audio_device_type type, audio_device_state state);
bool try_get_audio_device_by_description(const std::string& description, audio_device& device, audio_device_type type, audio_device_state state);

#if WIN32

struct wasapi_audio_stream : public audio_stream_base
{
    wasapi_audio_stream();
    wasapi_audio_stream(IMMDevice* device);
	wasapi_audio_stream(const wasapi_audio_stream&);
	wasapi_audio_stream& operator=(const wasapi_audio_stream&);
    virtual ~wasapi_audio_stream();

    std::string name();

    void mute(bool);
    bool mute();

    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    size_t write(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    bool wait_write_completed(int timeout_ms);

    void start();
	void stop();

private:
    IMMDevice* device_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioRenderClient* render_client_ = nullptr;
    IAudioEndpointVolume* endpoint_volume_ = nullptr;
    UINT32 buffer_size_ = 0;
    int sample_rate_ = 0;
    WORD num_channels_ = 1;
};

#endif // WIN32

struct input_wav_audio_stream : audio_stream_base
{
public:
    input_wav_audio_stream(const std::string& filename);
    virtual ~input_wav_audio_stream();

    std::string name();

    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    size_t write(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    bool wait_write_completed(int timeout_ms);

    void flush();
    void close();

private:
    SNDFILE* sf_file_ = nullptr;
    std::string filename_;
    int sample_rate_;
    int channels_ = 1;
};

struct output_wav_audio_stream : audio_stream_base
{
public:
    output_wav_audio_stream(const std::string& filename, int sample_rate = 48000);
    virtual ~output_wav_audio_stream();

    std::string name();

    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    size_t write(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    bool wait_write_completed(int timeout_ms);

    void flush();
    void close();

private:
    SNDFILE* sf_file_ = nullptr;
    std::string filename_;
    int sample_rate_;
    int channels_ = 1;
};

class tcp_client_audio_stream : public audio_stream_base
{
using json = nlohmann::json;

public:
    tcp_client_audio_stream(const char* host, int audio_port, int control_port);
    virtual ~tcp_client_audio_stream();

    std::string name();

    void volume(int percent) override;
    int volume() override;
    int sample_rate() override;
    size_t write(const double* samples, size_t count) override;
    size_t read(double* samples, size_t count) override;
    bool wait_write_completed(int timeout_ms);

private:
    std::string host_;
    int audio_port_;
    int control_port_;
    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::socket socket_;         // Audio data socket (raw bytes)
    boost::asio::ip::tcp::socket control_socket_; // Control commands socket
    int volume_;
    int sample_rate_;

    void send_control_command(const json& cmd);
    json receive_control_response();
};