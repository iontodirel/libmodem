// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// pipeline.cpp
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

#include "pipeline.h"

#include <cstdio>
#include <mutex>

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>

LIBMODEM_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// utility functions forward declarations                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool try_create_audio_entry(audio_entry& entry, const audio_stream_config& config, error_events& events, boost::asio::thread_pool& pool);
bool try_find_audio_device(const audio_stream_config& audio_config, audio_device& device);
std::unique_ptr<audio_stream_base> create_non_hardware_audio_stream(audio_entry& entry, const audio_stream_config& config, error_events& events, boost::asio::thread_pool& pool);
bool try_create_ptt_control(ptt_entry& entry, const ptt_control_config& config, error_events& events, boost::asio::thread_pool& pool);
bool try_create_transport(data_stream_entry& entry, const data_stream_config& config, error_events& events, boost::asio::thread_pool& pool);
std::optional<std::reference_wrapper<audio_entry>> find_audio_entry(std::vector<std::unique_ptr<audio_entry>>& entries, const std::string& name);
std::optional<std::reference_wrapper<audio_entry>> get_output_stream(const modulator_config& config, std::vector<std::unique_ptr<audio_entry>>& audio_entries);
std::unique_ptr<modem_entry> create_modem_entry(const modulator_config& config);
std::unique_ptr<modulator_base> create_modulator(const modulator_config& config, int sample_rate);
std::optional<std::reference_wrapper<ptt_entry>> find_ptt_entry(std::vector<std::unique_ptr<ptt_entry>>& entries, const std::string& name);
std::optional<std::reference_wrapper<data_stream_entry>> find_data_stream_entry(std::vector<std::unique_ptr<data_stream_entry>>& entries, const std::string& name);
bool is_audio_stream_referenced(const config& c, const std::string& name);
bool is_ptt_control_referenced(const config& c, const std::string& name);
bool is_output_stream(audio_stream_config_type type);
bool is_input_stream(audio_stream_config_type type);
bool requires_audio_hardware(audio_stream_config_type type);
serial_port_ptt_trigger parse_ptt_trigger(const std::string& trigger);
serial_port_ptt_line parse_ptt_line(const std::string& line);
std::string platform_name();

// **************************************************************** //
//                                                                  //
//                                                                  //
// wrap                                                             //
//                                                                  //
//                                                                  //
// **************************************************************** //

template<typename Component, typename Entry, typename Func, typename R>
R wrap(Component& component, Entry& entry, std::atomic<bool>& faulted, std::optional<std::reference_wrapper<error_events>>& events, boost::asio::thread_pool& pool, Func&& func, R fallback)
{
    if (faulted.load())
    {
        return fallback;
    }

    try
    {
        return func();
    }
    catch (const std::exception& ex)
    {
        faulted.store(true);
        if (events)
        {
            error_info error;
            error.message = ex.what();
            error.exception = std::current_exception();
            boost::asio::post(pool, [&component, &entry, &events, error]() {
                events->get().on_error(component, entry, error);
            });
        }
        return fallback;
    }
}

template<typename Component, typename Entry, typename Func>
void wrap(Component& component, Entry& entry, std::atomic<bool>& faulted, std::optional<std::reference_wrapper<error_events>>& events, boost::asio::thread_pool& pool, Func&& func)
{
    if (faulted.load())
    {
        return;
    }

    try
    {
        func();
    }
    catch (const std::exception& ex)
    {
        faulted.store(true);
        if (events)
        {
            error_info error;
            error.message = ex.what();
            error.exception = std::current_exception();
            boost::asio::post(pool, [&component, &entry, &events, error]() {
                events->get().on_error(component, entry, error);
            });
        }
    }
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// audio_stream_no_throw                                            //
//                                                                  //
//                                                                  //
// **************************************************************** //

class audio_stream_no_throw : public audio_stream_base
{
public:
    using audio_stream_base::wait_write_completed;

    audio_stream_no_throw(std::unique_ptr<audio_stream_base> inner, audio_entry& entry, error_events& events, boost::asio::thread_pool& pool);

    bool faulted() const;

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

    audio_stream_base& get();

private:
    std::unique_ptr<audio_stream_base> inner_;
    std::optional<std::reference_wrapper<error_events>> events_;
    std::optional<std::reference_wrapper<audio_entry>> entry_;
    std::reference_wrapper<boost::asio::thread_pool> pool_;
    std::atomic<bool> faulted_ = false;
};

audio_stream_no_throw::audio_stream_no_throw(std::unique_ptr<audio_stream_base> inner, audio_entry& entry, error_events& events, boost::asio::thread_pool& pool) : inner_(std::move(inner)), pool_(pool)
{
    events_ = events;
    entry_ = entry;
}

bool audio_stream_no_throw::faulted() const
{
    return faulted_.load();
}

void audio_stream_no_throw::close() noexcept
{
    wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { inner_->close(); });
}

std::string audio_stream_no_throw::name()
{
    return inner_->name();
}

audio_stream_type audio_stream_no_throw::type()
{
    return inner_->type();
}

int audio_stream_no_throw::sample_rate()
{
    return inner_->sample_rate();
}

int audio_stream_no_throw::channels()
{
    return inner_->channels();
}

void audio_stream_no_throw::volume(int percent)
{
    wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { inner_->volume(percent); });
}

int audio_stream_no_throw::volume()
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->volume(); }, 100);
}

size_t audio_stream_no_throw::write(const double* samples, size_t count)
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->write(samples, count); }, size_t{ 0 });
}

size_t audio_stream_no_throw::write_interleaved(const double* samples, size_t count)
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->write_interleaved(samples, count); }, size_t{ 0 });
}

size_t audio_stream_no_throw::read(double* samples, size_t count)
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->read(samples, count); }, size_t{ 0 });
}

size_t audio_stream_no_throw::read_interleaved(double* samples, size_t count)
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->read_interleaved(samples, count); }, size_t{ 0 });
}

bool audio_stream_no_throw::wait_write_completed(int timeout_ms)
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->wait_write_completed(timeout_ms); }, true);
}

bool audio_stream_no_throw::eof()
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->eof(); }, true);
}

void audio_stream_no_throw::start()
{
    wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { inner_->start(); });
}

void audio_stream_no_throw::stop() noexcept
{
    wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { inner_->stop(); });
}

audio_stream_no_throw::operator bool()
{
    if (faulted_)
    {
        return false;
    }
    return inner_->operator bool();
}

audio_stream_base& audio_stream_no_throw::get()
{
    return *inner_;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// ptt_control_no_throw                                             //
//                                                                  //
//                                                                  //
// **************************************************************** //

class ptt_control_no_throw : public ptt_control_base
{
public:
    ptt_control_no_throw(std::unique_ptr<ptt_control_base> inner, ptt_entry& entry, error_events& events, boost::asio::thread_pool& pool);

    bool faulted() const;

    void ptt(bool enable) override;
    bool ptt() override;

private:
    std::unique_ptr<ptt_control_base> inner_;
    std::optional<std::reference_wrapper<error_events>> events_;
    std::optional<std::reference_wrapper<ptt_entry>> entry_;
    std::reference_wrapper<boost::asio::thread_pool> pool_;
    bool last_state_ = false;
    std::atomic<bool> faulted_ = false;
};

ptt_control_no_throw::ptt_control_no_throw(std::unique_ptr<ptt_control_base> inner, ptt_entry& entry, error_events& events, boost::asio::thread_pool& pool) : inner_(std::move(inner)), pool_(pool)
{
    events_ = events;
    entry_ = entry;
}

bool ptt_control_no_throw::faulted() const
{
    return faulted_.load();
}

void ptt_control_no_throw::ptt(bool enable)
{
    wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() {
        inner_->ptt(enable);
        last_state_ = enable;
    });
}

bool ptt_control_no_throw::ptt()
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->ptt(); }, last_state_);
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// transport_no_throw                                               //
//                                                                  //
//                                                                  //
// **************************************************************** //

class transport_no_throw : public transport
{
public:
    transport_no_throw(std::unique_ptr<transport> inner, data_stream_entry& entry, error_events& events, boost::asio::thread_pool& pool);

    bool faulted() const;

    void start() override;
    void stop() override;
    void write(const std::vector<uint8_t>& data) override;
    size_t read(std::size_t client_id, std::vector<uint8_t>& data, size_t size) override;
    std::vector<std::size_t> clients() override;
    void flush() override;
    bool wait_data_received(int timeout_ms) override;

private:
    std::unique_ptr<transport> inner_;
    std::optional<std::reference_wrapper<error_events>> events_;
    std::optional<std::reference_wrapper<data_stream_entry>> entry_;
    std::reference_wrapper<boost::asio::thread_pool> pool_;
    std::atomic<bool> faulted_ = false;
};

transport_no_throw::transport_no_throw(std::unique_ptr<transport> inner, data_stream_entry& entry, error_events& events, boost::asio::thread_pool& pool) : inner_(std::move(inner)), pool_(pool)
{
    events_ = events;
    entry_ = entry;
}

bool transport_no_throw::faulted() const
{
    return faulted_.load();
}

void transport_no_throw::start()
{
    inner_->start();
}

void transport_no_throw::stop()
{
    wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { inner_->stop(); });
}

void transport_no_throw::write(const std::vector<uint8_t>& data)
{
    wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { inner_->write(data); });
}

size_t transport_no_throw::read(std::size_t client_id, std::vector<uint8_t>& data, size_t size)
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->read(client_id, data, size); }, size_t{ 0 });
}

std::vector<std::size_t> transport_no_throw::clients()
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->clients(); }, std::vector<std::size_t>{});
}

void transport_no_throw::flush()
{
    wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { inner_->flush(); });
}

bool transport_no_throw::wait_data_received(int timeout_ms)
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->wait_data_received(timeout_ms); }, false);
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// serial_port_no_throw                                             //
//                                                                  //
//                                                                  //
// **************************************************************** //

class serial_port_no_throw : public serial_port_base
{
public:
    serial_port_no_throw(std::unique_ptr<serial_port_base> inner, ptt_entry& entry, error_events& events, boost::asio::thread_pool& pool);

    void entry(ptt_entry& entry);
    bool faulted() const;

    void rts(bool enable) override;
    bool rts() override;
    void dtr(bool enable) override;
    bool dtr() override;
    bool cts() override;
    bool dsr() override;
    bool dcd() override;
    std::size_t write(const std::vector<uint8_t>& data) override;
    std::size_t write(const std::string& data) override;
    std::vector<uint8_t> read(std::size_t size) override;
    std::vector<uint8_t> read_some(std::size_t max_size) override;
    std::string read_until(const std::string& delimiter) override;
    bool is_open() override;
    std::size_t bytes_available() override;
    void flush() override;

private:
    std::unique_ptr<serial_port_base> inner_;
    std::optional<std::reference_wrapper<error_events>> events_;
    std::optional<std::reference_wrapper<ptt_entry>> entry_;
    std::reference_wrapper<boost::asio::thread_pool> pool_;
    std::atomic<bool> faulted_ = false;
};

serial_port_no_throw::serial_port_no_throw(std::unique_ptr<serial_port_base> inner, ptt_entry& entry, error_events& events, boost::asio::thread_pool& pool) : inner_(std::move(inner)), pool_(pool)
{
    events_ = events;
    entry_ = entry;
}

void serial_port_no_throw::entry(ptt_entry& entry)
{
    entry_ = entry;
}

bool serial_port_no_throw::faulted() const
{
    return faulted_;
}

void serial_port_no_throw::rts(bool enable)
{
    wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { inner_->rts(enable); });
}

bool serial_port_no_throw::rts()
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->rts(); }, false);
}

void serial_port_no_throw::dtr(bool enable)
{
    wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { inner_->dtr(enable); });
}

bool serial_port_no_throw::dtr()
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->dtr(); }, false);
}

bool serial_port_no_throw::cts()
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->cts(); }, false);
}

bool serial_port_no_throw::dsr()
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->dsr(); }, false);
}

bool serial_port_no_throw::dcd()
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->dcd(); }, false);
}

std::size_t serial_port_no_throw::write(const std::vector<uint8_t>& data)
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->write(data); }, std::size_t{ 0 });
}

std::size_t serial_port_no_throw::write(const std::string& data)
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->write(data); }, std::size_t{ 0 });
}

std::vector<uint8_t> serial_port_no_throw::read(std::size_t size)
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->read(size); }, std::vector<uint8_t>{});
}

std::vector<uint8_t> serial_port_no_throw::read_some(std::size_t max_size)
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->read_some(max_size); }, std::vector<uint8_t>{});
}

std::string serial_port_no_throw::read_until(const std::string& delimiter)
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->read_until(delimiter); }, std::string{});
}

bool serial_port_no_throw::is_open()
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->is_open(); }, false);
}

std::size_t serial_port_no_throw::bytes_available()
{
    return wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { return inner_->bytes_available(); }, std::size_t{ 0 });
}

void serial_port_no_throw::flush()
{
    wrap(*this, entry_->get(), faulted_, events_, pool_.get(), [&]() { inner_->flush(); });
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// recovery_context                                                 //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct recovery_context
{
    std::unique_ptr<boost::asio::steady_timer> timer;
    int attempts = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// pipeline_impl                                                    //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct pipeline_impl
{
    std::unique_ptr<boost::asio::thread_pool> pool;
    std::mutex recovery_mutex;
    std::unordered_map<audio_entry*, recovery_context> audio_recovery_contexts;
    std::unordered_map<ptt_entry*, recovery_context> ptt_recovery_contexts;
    std::unordered_map<ptt_entry*, recovery_context> serial_port_recovery_contexts;
    std::unordered_map<data_stream_entry*, recovery_context> transport_recovery_contexts;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// pipeline                                                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

pipeline::pipeline(const struct config& c) : config(c), impl_(std::make_unique<pipeline_impl>())
{
    impl_->pool = std::make_unique<boost::asio::thread_pool>(4);
}

pipeline::~pipeline() = default;

void pipeline::init()
{
    modems_.clear();
    ptt_controls_.clear();
    audio_entries_.clear();
    data_streams_.clear();

    used_audio_names_.clear();
    used_audio_devices_.clear();
    used_audio_files_.clear();
    used_serial_ports_.clear();
    used_library_files_.clear();
    used_ptt_names_.clear();
    used_modem_names_.clear();
    used_data_stream_names_.clear();
    used_tcp_ports_.clear();

    populate_audio_entries();
    populate_ptt_controls();
    populate_data_streams();
    populate_transmit_modems();
    populate_receive_modems();

    assign_audio_streams();
    assign_ptt_controls();
    assign_modems();

    validate_entries();
}

void pipeline::start()
{
    for (auto& data_stream : data_streams_)
    {
        if (data_stream->enabled)
        {
            data_stream->data_stream->start();
        }
    }
}

void pipeline::stop()
{
    impl_->pool->join();
}

void pipeline::wait_stopped()
{
    for (auto& data_stream : data_streams_)
    {
        if (data_stream->enabled)
        {
            data_stream->data_stream->wait_stopped();
        }
    }
}

void pipeline::populate_audio_entries()
{
    for (const auto& audio_config : config.audio_streams)
    {
        if (!can_add_audio_entry(audio_config))
        {
            continue;
        }

        auto entry = std::make_unique<audio_entry>();
        if (!try_create_audio_entry(*entry, audio_config, *this, *impl_->pool.get()))
        {
            continue;
        }

        // Book keeping of used filenames and names
        // For hardware streams, check device uniqueness after creation
        // (device info is only available after try_create_audio_entry)
        if (requires_audio_hardware(audio_config.type))
        {
            if (is_duplicate_audio_device(entry->device.id, entry->device.type))
            {
                continue;
            }
        }

        register_audio_entry(*entry, audio_config);

        audio_entries_.push_back(std::move(entry));
    }
}

void pipeline::populate_ptt_controls()
{
    for (const auto& ptt_config : config.ptt_controls)
    {
        if (!can_add_ptt_control(ptt_config))
        {
            continue;
        }

        auto entry = std::make_unique<ptt_entry>();

        entry->name = ptt_config.name;
        entry->port_name = ptt_config.serial_port;
        entry->baud_rate = ptt_config.baud_rate;
        entry->config = ptt_config;
        entry->library_path = ptt_config.library_path;
        entry->serial_trigger = parse_ptt_trigger(ptt_config.trigger);
        entry->serial_line = parse_ptt_line(ptt_config.line);

        if (!try_create_ptt_control(*entry, ptt_config, *this, *impl_->pool.get()))
        {
            continue;
        }

        register_ptt_control(*entry, ptt_config);
        ptt_controls_.push_back(std::move(entry));
    }
}

void pipeline::populate_data_streams()
{
    for (const auto& data_stream_config : config.data_streams)
    {
        if (!can_add_data_stream(data_stream_config))
        {
            continue;
        }

        auto entry = std::make_unique<data_stream_entry>();
        entry->name = data_stream_config.name;
        entry->config = data_stream_config;

        switch (data_stream_config.format)
        {
            case data_stream_format_type::ax25_kiss_formatter:
                entry->formatter = std::make_unique<ax25_kiss_formatter>();
                break;
            default:
                break;
        }

        if (!try_create_transport(*entry, data_stream_config, *this, *impl_->pool.get()))
        {
            continue;
        }

        entry->data_stream = std::make_unique<modem_data_stream>();
        entry->data_stream->formatter(*entry->formatter);
        entry->data_stream->transport(*entry->transport);

        register_data_stream(*entry, data_stream_config);

        data_streams_.push_back(std::move(entry));
    }
}

void pipeline::populate_transmit_modems()
{
    for (const auto& modulator_config : config.modulators)
    {
        if (!can_add_modem(modulator_config))
        {
            continue;
        }

        auto entry = create_modem_entry(modulator_config);

        register_modem(*entry);

        modems_.push_back(std::move(entry));
    }
}

void pipeline::populate_receive_modems()
{
    // To match a modulator and demodulator to the same modem:
    // - Match bitstream converters of the modulator and demodulator
    // - Match data sources
    // TODO: Implement receive modem population
}

void pipeline::assign_audio_streams()
{
    for (auto& modem_entry : modems_)
    {
        if (!modem_entry->enabled)
        {
            continue;
        }

        auto audio_entry = get_output_stream(modem_entry->modulator_config, audio_entries_);
        if (!audio_entry || !audio_entry->get().enabled)
        {
            modem_entry->enabled = false;
            continue;
        }

        // Check if already assigned to another modem
        if (!audio_entry->get().referenced_by.empty())
        {
            modem_entry->enabled = false;
            continue;
        }

        // Wire audio stream
        modem_entry->modem.output_stream(audio_entry->get().stream.get());

        // Create modulator now that we have sample rate
        modem_entry->modulator = create_modulator(modem_entry->modulator_config, modem_entry->modem.output_stream().sample_rate());

        if (!modem_entry->modulator)
        {
            modem_entry->enabled = false;
            continue;
        }

        modem_entry->modem.modulator(*modem_entry->modulator);

        // Initialize modem
        modem_entry->modem.initialize();

        audio_entry->get().referenced_by.push_back(modem_entry->name);
        audio_entry->get().associated_modem_entry = *modem_entry;
        modem_entry->associated_audio_entry = audio_entry;
    }
}

void pipeline::assign_modems()
{
    for (auto& modem_entry : modems_)
    {
        if (!modem_entry->enabled)
        {
            continue;
        }

        for (const auto& ds_name : modem_entry->modulator_config.data_streams)
        {
            auto ds_entry = find_data_stream_entry(data_streams_, ds_name);
            if (!ds_entry || !ds_entry->get().enabled)
            {
                continue;
            }

            // Check if already assigned to another modem
            if (!ds_entry->get().referenced_by.empty())
            {
                continue;
            }

            // Wire modem to data stream
            ds_entry->get().data_stream->modem(modem_entry->modem);

            ds_entry->get().referenced_by.push_back(modem_entry->name);

            modem_entry->associated_data_stream_entry = ds_entry;

            // Only one modem per data stream
            break;
        }
    }
}

void pipeline::assign_ptt_controls()
{
    for (auto& modem_entry : modems_)
    {
        if (!modem_entry->enabled)
        {
            continue;
        }

        for (const auto& ptt_name : modem_entry->modulator_config.ptt_controls)
        {
            auto ptt_entry = find_ptt_entry(ptt_controls_, ptt_name);
            if (!ptt_entry || !ptt_entry->get().enabled)
            {
                continue;
            }

            // Check if already assigned to another modem
            if (!ptt_entry->get().referenced_by.empty())
            {
                continue;
            }

            // Wire PTT control
            modem_entry->modem.ptt_control(*ptt_entry->get().ptt_control);

            ptt_entry->get().referenced_by.push_back(modem_entry->name);
            ptt_entry->get().associated_modem_entry = *modem_entry;
            modem_entry->associated_ptt_entry = ptt_entry;

            // Only one PTT per modem
            break;
        }
    }
}

bool pipeline::can_add_audio_entry(const audio_stream_config& audio_config)
{
    if (!is_valid_audio_config(audio_config))
    {
        return false;
    }

    if (is_duplicate_audio_name(audio_config.name))
    {
        return false;
    }

    // File-based streams: check for duplicate file usage (except input streams)
    if (!audio_config.filename.empty() &&
        !is_input_stream(audio_config.type) &&
        is_duplicate_audio_file(audio_config.filename))
    {
        return false;
    }

    if (!is_audio_stream_referenced(audio_config.name))
    {
        return false;
    }

    return true;
}

bool pipeline::is_duplicate_audio_name(const std::string& name)
{
    return used_audio_names_.count(name) > 0;
}

bool pipeline::is_duplicate_audio_device(const std::string& device_id, audio_device_type type)
{
    return used_audio_devices_.count({ device_id, type }) > 0;
}

bool pipeline::is_duplicate_audio_file(const std::string& filename)
{
    return used_audio_files_.count(filename) > 0;
}

bool pipeline::is_valid_audio_config(const audio_stream_config& audio_config)
{
    // File-based streams cannot require audio hardware
    if (!audio_config.filename.empty() && requires_audio_hardware(audio_config.type))
    {
        return false;
    }

    return true;
}

void pipeline::register_audio_entry(const audio_entry& entry, const audio_stream_config& audio_config)
{
    used_audio_names_.insert(audio_config.name);

    if (requires_audio_hardware(audio_config.type))
    {
        used_audio_devices_.insert({ entry.device.id, entry.device.type });
    }

    if (!audio_config.filename.empty())
    {
        used_audio_files_.insert(audio_config.filename);
    }
}

bool pipeline::can_add_ptt_control(const ptt_control_config& ptt_config)
{
    if (!is_valid_ptt_config(ptt_config))
    {
        return false;
    }

    if (is_duplicate_ptt_name(ptt_config.name))
    {
        return false;
    }

    // Serial ports can't be shared
    if (ptt_config.type == ptt_control_config_type::serial_port_ptt_control &&
        is_duplicate_serial_port(ptt_config.serial_port))
    {
        return false;
    }

    // Library files can't be duplicated
    if (ptt_config.type == ptt_control_config_type::library_ptt_control &&
        !ptt_config.library_path.empty() &&
        is_duplicate_library_file(ptt_config.library_path))
    {
        return false;
    }

    if (!is_ptt_control_referenced(ptt_config.name))
    {
        return false;
    }

    return true;
}

bool pipeline::is_duplicate_ptt_name(const std::string& name)
{
    return used_ptt_names_.count(name) > 0;
}

bool pipeline::is_duplicate_serial_port(const std::string& port)
{
    return used_serial_ports_.count(port) > 0;
}

bool pipeline::is_duplicate_library_file(const std::string& path)
{
    return used_library_files_.count(path) > 0;
}

bool pipeline::is_valid_ptt_config(const ptt_control_config& ptt_config)
{
    // Platform must match
    if (platform_name() != ptt_config.platform && ptt_config.type != ptt_control_config_type::null_ptt_control)
    {
        return false;
    }

    return true;
}

void pipeline::register_ptt_control(const ptt_entry& entry, const ptt_control_config& ptt_config)
{
    used_ptt_names_.insert(ptt_config.name);

    if (ptt_config.type == ptt_control_config_type::serial_port_ptt_control)
    {
        used_serial_ports_.insert(ptt_config.serial_port);
    }
    else if (ptt_config.type == ptt_control_config_type::library_ptt_control)
    {
        used_library_files_.insert(ptt_config.library_path);
    }

    (void)entry; // Unused for now, but kept for consistency
}

bool pipeline::can_add_modem(const modulator_config& modulator_config)
{
    if (!modulator_config.enabled)
    {
        return false;
    }

    if (is_duplicate_modem_name(modulator_config.name))
    {
        return false;
    }

    return true;
}

bool pipeline::is_duplicate_modem_name(const std::string& name)
{
    return used_modem_names_.count(name) > 0;
}

void pipeline::register_modem(const modem_entry& entry)
{
    used_modem_names_.insert(entry.name);
}

bool pipeline::is_audio_stream_referenced(const std::string& name)
{
    for (const auto& modulator_config : config.modulators)
    {
        for (const auto& stream_name : modulator_config.audio_output_streams)
        {
            if (stream_name == name)
            {
                return true;
            }
        }
    }

    return false;
}

bool pipeline::is_ptt_control_referenced(const std::string& name)
{
    for (const auto& modulator_config : config.modulators)
    {
        for (const auto& ptt_name : modulator_config.ptt_controls)
        {
            if (ptt_name == name)
            {
                return true;
            }
        }
    }

    return false;
}

bool pipeline::can_add_data_stream(const data_stream_config& data_config)
{
    if (!is_valid_data_stream_config(data_config))
    {
        return false;
    }

    if (is_duplicate_data_stream_name(data_config.name))
    {
        return false;
    }

    // TCP ports can't be shared
    if (data_config.transport == data_stream_transport_type::tcp &&
        is_duplicate_tcp_port(data_config.port))
    {
        return false;
    }

    // Serial ports can't be shared (reuse existing check)
    if (data_config.transport == data_stream_transport_type::serial &&
        is_duplicate_serial_port(data_config.serial_port))
    {
        return false;
    }

    if (!is_data_stream_referenced(data_config.name))
    {
        return false;
    }

    return true;
}

bool pipeline::is_duplicate_data_stream_name(const std::string& name)
{
    return used_data_stream_names_.count(name) > 0;
}

bool pipeline::is_duplicate_tcp_port(int port)
{
    return used_tcp_ports_.count(port) > 0;
}

bool pipeline::is_valid_data_stream_config(const data_stream_config& data_config)
{
    // Validate transport-specific requirements
    if (data_config.transport == data_stream_transport_type::tcp)
    {
        if (data_config.port <= 0 || data_config.port > 65535)
        {
            return false;
        }
    }
    else if (data_config.transport == data_stream_transport_type::serial)
    {
        if (data_config.serial_port.empty())
        {
            return false;
        }
    }
    else if (data_config.transport == data_stream_transport_type::unknown)
    {
        return false;
    }

    if (data_config.format == data_stream_format_type::unknown)
    {
        return false;
    }

    return true;
}

void pipeline::register_data_stream(const data_stream_entry& entry, const data_stream_config& data_config)
{
    used_data_stream_names_.insert(data_config.name);

    if (data_config.transport == data_stream_transport_type::tcp)
    {
        used_tcp_ports_.insert(data_config.port);
    }
    else if (data_config.transport == data_stream_transport_type::serial)
    {
        used_serial_ports_.insert(data_config.serial_port);
    }

    (void)entry; // Unused for now, but kept for consistency
}

bool pipeline::is_data_stream_referenced(const std::string& name)
{
    for (const auto& modulator_config : config.modulators)
    {
        for (const auto& stream_name : modulator_config.data_streams)
        {
            if (stream_name == name)
            {
                return true;
            }
        }
    }

    return false;
}

void pipeline::validate_entries()
{
    // Disable data streams without modems
    for (auto& ds_entry : data_streams_)
    {
        if (ds_entry->enabled && ds_entry->referenced_by.empty())
        {
            ds_entry->enabled = false;
        }
    }

    // Disable audio streams without modems
    for (auto& audio_entry : audio_entries_)
    {
        if (audio_entry->enabled && audio_entry->referenced_by.empty())
        {
            audio_entry->enabled = false;
        }
    }

    // Disable PTT controls without modems
    for (auto& ptt_entry : ptt_controls_)
    {
        if (ptt_entry->enabled && ptt_entry->referenced_by.empty())
        {
            ptt_entry->enabled = false;
        }
    }
}

bool pipeline::try_recover_audio_stream(audio_entry& audio_entry, modem_entry& modem_entry)
{
    if (!try_create_audio_entry(audio_entry, audio_entry.config, *this, *impl_->pool.get()))
    {
        return false;
    }

    modem_entry.modem.output_stream(audio_entry.stream.get());

    // Recreate the modulator (sample rate might have changed)
    modem_entry.modulator = create_modulator(modem_entry.modulator_config, audio_entry.stream.sample_rate());
    if (!modem_entry.modulator)
    {
        return false;
    }

    modem_entry.modem.modulator(*modem_entry.modulator);

    // Re-initialize modem with new settings
    modem_entry.modem.initialize();

    return true;
}

void pipeline::schedule_audio_recovery(audio_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry)
{
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        auto& context = impl_->audio_recovery_contexts[&entry];
        context.attempts = 0;
        context.timer = std::make_unique<boost::asio::steady_timer>(impl_->pool->get_executor());
    }

    attempt_audio_recovery(entry, modem_entry, ds_entry);
}

void pipeline::attempt_audio_recovery(audio_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry)
{
    int attempts;
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        auto it = impl_->audio_recovery_contexts.find(&entry);
        if (it == impl_->audio_recovery_contexts.end())
        {
            return; // Recovery was cancelled
        }
        it->second.attempts++;
        attempts = it->second.attempts;
    }

    if (try_recover_audio_stream(entry, modem_entry))
    {
        entry.faulted.store(false);
        {
            std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
            impl_->audio_recovery_contexts.erase(&entry);
        }
        try_reenable_data_stream(modem_entry, ds_entry);
        return;
    }

    if (attempts >= max_recovery_attempts_)
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        impl_->audio_recovery_contexts.erase(&entry);
        return;
    }

    // Schedule next attempt
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        auto it = impl_->audio_recovery_contexts.find(&entry);
        if (it == impl_->audio_recovery_contexts.end())
        {
            return;
        }
        it->second.timer->expires_after(std::chrono::seconds(recovery_delay_seconds_));
        it->second.timer->async_wait([this, &entry, &modem_entry, &ds_entry](const boost::system::error_code& ec) {
            if (!ec)
            {
                attempt_audio_recovery(entry, modem_entry, ds_entry);
            }
        });
    }
}

bool pipeline::try_recover_ptt_control(ptt_entry& entry, modem_entry& modem_entry)
{
    if (!try_create_ptt_control(entry, entry.config, *this, *impl_->pool.get()))
    {
        return false;
    }

    modem_entry.modem.ptt_control(*entry.ptt_control);

    return true;
}

void pipeline::schedule_ptt_recovery(ptt_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry)
{
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        auto& context = impl_->ptt_recovery_contexts[&entry];
        context.attempts = 0;
        context.timer = std::make_unique<boost::asio::steady_timer>(impl_->pool->get_executor());
    }

    attempt_ptt_recovery(entry, modem_entry, ds_entry);
}

void pipeline::attempt_ptt_recovery(ptt_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry)
{
    int attempts;
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        auto it = impl_->ptt_recovery_contexts.find(&entry);
        if (it == impl_->ptt_recovery_contexts.end())
        {
            return;
        }
        it->second.attempts++;
        attempts = it->second.attempts;
    }

    if (try_recover_ptt_control(entry, modem_entry))
    {
        entry.faulted.store(false);
        {
            std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
            impl_->ptt_recovery_contexts.erase(&entry);
        }
        try_reenable_data_stream(modem_entry, ds_entry);
        return;
    }

    if (attempts >= max_recovery_attempts_)
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        impl_->ptt_recovery_contexts.erase(&entry);
        return;
    }

    // Schedule next attempt
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        auto it = impl_->ptt_recovery_contexts.find(&entry);
        if (it == impl_->ptt_recovery_contexts.end())
        {
            return;
        }
        it->second.timer->expires_after(std::chrono::seconds(recovery_delay_seconds_));
        it->second.timer->async_wait([this, &entry, &modem_entry, &ds_entry](const boost::system::error_code& ec) {
            if (!ec)
            {
                attempt_ptt_recovery(entry, modem_entry, ds_entry);
            }
        });
    }
}

bool pipeline::try_recover_serial_port(ptt_entry& entry, modem_entry& modem_entry)
{
    // For serial port PTT, we need to recreate both serial port and PTT control
    if (entry.type != ptt_control_type::serial_port)
    {
        return true; // Nothing to recover for non-serial PTT
    }

    if (!try_create_ptt_control(entry, entry.config, *this, *impl_->pool.get()))
    {
        return false;
    }

    modem_entry.modem.ptt_control(*entry.ptt_control);

    return true;
}

void pipeline::schedule_serial_port_recovery(ptt_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry)
{
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        auto& context = impl_->serial_port_recovery_contexts[&entry];
        context.attempts = 0;
        context.timer = std::make_unique<boost::asio::steady_timer>(impl_->pool->get_executor());
    }

    attempt_serial_port_recovery(entry, modem_entry, ds_entry);
}

void pipeline::attempt_serial_port_recovery(ptt_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry)
{
    int attempts;
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        auto it = impl_->serial_port_recovery_contexts.find(&entry);
        if (it == impl_->serial_port_recovery_contexts.end())
        {
            return;
        }
        it->second.attempts++;
        attempts = it->second.attempts;
    }

    if (try_recover_serial_port(entry, modem_entry))
    {
        entry.serial_port_faulted.store(false);
        {
            std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
            impl_->serial_port_recovery_contexts.erase(&entry);
        }
        try_reenable_data_stream(modem_entry, ds_entry);
        return;
    }

    if (attempts >= max_recovery_attempts_)
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        impl_->serial_port_recovery_contexts.erase(&entry);
        return;
    }

    // Schedule next attempt
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        auto it = impl_->serial_port_recovery_contexts.find(&entry);
        if (it == impl_->serial_port_recovery_contexts.end())
        {
            return;
        }
        it->second.timer->expires_after(std::chrono::seconds(recovery_delay_seconds_));
        it->second.timer->async_wait([this, &entry, &modem_entry, &ds_entry](const boost::system::error_code& ec) {
            if (!ec)
            {
                attempt_serial_port_recovery(entry, modem_entry, ds_entry);
            }
        });
    }
}

bool pipeline::try_recover_transport(data_stream_entry& entry)
{
    if (!try_create_transport(entry, entry.config, *this, *impl_->pool.get()))
    {
        return false;
    }

    entry.data_stream->transport(*entry.transport);

    return true;
}

void pipeline::schedule_transport_recovery(data_stream_entry& ds_entry)
{
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        auto& context = impl_->transport_recovery_contexts[&ds_entry];
        context.attempts = 0;
        context.timer = std::make_unique<boost::asio::steady_timer>(impl_->pool->get_executor());
    }

    attempt_transport_recovery(ds_entry);
}

void pipeline::attempt_transport_recovery(data_stream_entry& ds_entry)
{
    int attempts;
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        auto it = impl_->transport_recovery_contexts.find(&ds_entry);
        if (it == impl_->transport_recovery_contexts.end())
        {
            return;
        }
        it->second.attempts++;
        attempts = it->second.attempts;
    }

    if (try_recover_transport(ds_entry))
    {
        ds_entry.faulted.store(false);
        {
            std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
            impl_->transport_recovery_contexts.erase(&ds_entry);
        }

        // For transport, just re-enable directly since it's the only dependency
        ds_entry.data_stream->enabled(true);
        return;
    }

    if (attempts >= max_recovery_attempts_)
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        impl_->transport_recovery_contexts.erase(&ds_entry);
        return;
    }

    // Schedule next attempt
    {
        std::lock_guard<std::mutex> lock(impl_->recovery_mutex);
        auto it = impl_->transport_recovery_contexts.find(&ds_entry);
        if (it == impl_->transport_recovery_contexts.end())
        {
            return;
        }
        it->second.timer->expires_after(std::chrono::seconds(recovery_delay_seconds_));
        it->second.timer->async_wait([this, &ds_entry](const boost::system::error_code& ec) {
            if (!ec)
            {
                attempt_transport_recovery(ds_entry);
            }
        });
    }
}

void pipeline::try_reenable_data_stream(modem_entry& modem_entry, data_stream_entry& ds_entry)
{
    // Check all dependencies are healthy before re-enabling

    // Check audio stream
    if (modem_entry.associated_audio_entry)
    {
        if (modem_entry.associated_audio_entry->get().faulted.load())
        {
            return; // Audio still faulted, can't enable
        }
    }

    // Check PTT control
    if (modem_entry.associated_ptt_entry)
    {
        auto& ptt = modem_entry.associated_ptt_entry->get();
        if (ptt.faulted.load() || ptt.serial_port_faulted.load())
        {
            return; // PTT or serial port still faulted, can't enable
        }
    }

    // Check data stream transport
    if (ds_entry.faulted.load())
    {
        return; // Transport still faulted, can't enable
    }

    // All dependencies healthy, re-enable
    ds_entry.data_stream->enabled(true);
}

void pipeline::on_error(audio_stream_no_throw& component, audio_entry& entry, const error_info& error)
{
    (void)component;
    (void)error;

    entry.faulted.store(true);
    entry.stream.close();

    if (entry.associated_modem_entry)
    {
        auto& modem_entry = entry.associated_modem_entry->get();
        if (modem_entry.associated_data_stream_entry)
        {
            auto& ds = modem_entry.associated_data_stream_entry->get();
            ds.data_stream->enabled(false);

            size_t error_count = ds.data_stream->audio_stream_error_count(ds.data_stream->audio_stream_error_count() + 1);
            if (error_count > max_audio_stream_error_count_)
            {
                return;
            }

            schedule_audio_recovery(entry, modem_entry, ds);
        }
    }
}

void pipeline::on_error(ptt_control_no_throw& component, ptt_entry& entry, const error_info& error)
{
    (void)component;
    (void)error;

    entry.faulted.store(true);

    if (entry.associated_modem_entry)
    {
        auto& modem_entry = entry.associated_modem_entry->get();
        if (modem_entry.associated_data_stream_entry)
        {
            auto& ds = modem_entry.associated_data_stream_entry->get();
            ds.data_stream->enabled(false);

            schedule_ptt_recovery(entry, modem_entry, ds);
        }
    }
}

void pipeline::on_error(serial_port_no_throw& component, ptt_entry& entry, const error_info& error)
{
    (void)component;
    (void)error;

    entry.serial_port_faulted.store(true);

    if (entry.associated_modem_entry)
    {
        auto& modem_entry = entry.associated_modem_entry->get();
        if (modem_entry.associated_data_stream_entry)
        {
            auto& ds = modem_entry.associated_data_stream_entry->get();
            ds.data_stream->enabled(false);

            schedule_serial_port_recovery(entry, modem_entry, ds);
        }
    }
}

void pipeline::on_error(transport_no_throw& component, data_stream_entry& entry, const error_info& error)
{
    (void)component;
    (void)error;

    entry.faulted.store(true);
    entry.data_stream->enabled(false);

    schedule_transport_recovery(entry);
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// utility functions                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool try_create_audio_entry(audio_entry& entry, const audio_stream_config& config, error_events& events, boost::asio::thread_pool& pool)
{
    if (requires_audio_hardware(config.type))
    {
        audio_device device;
        if (!try_find_audio_device(config, device))
        {
            return false;
        }

        entry.name = config.name;
        entry.display_name = device.name;
        entry.stream = audio_stream(std::make_unique<audio_stream_no_throw>(device.stream().release(), entry, events, pool));
        entry.device = std::move(device);
        entry.config = config;

        return true;
    }
    else
    {
        auto stream = create_non_hardware_audio_stream(entry, config, events, pool);
        if (!stream)
        {
            return false;
        }

        entry.name = config.name;
        entry.display_name = stream->name();
        entry.device = {};
        entry.stream = audio_stream(std::move(stream));
        entry.config = config;

        return true;
    }
}

bool try_find_audio_device(const audio_stream_config& audio_config, audio_device& device)
{
    audio_device_type device_type = is_output_stream(audio_config.type) ? audio_device_type::render : audio_device_type::capture;

    if (audio_config.device_name == "default")
    {
        return try_get_default_audio_device(device, device_type);
    }
    if (!audio_config.device_id.empty())
    {
        return try_get_audio_device_by_id(audio_config.device_id, device);
    }
    if (!audio_config.device_name.empty())
    {
        return try_get_audio_device_by_name(audio_config.device_name, device, device_type, audio_device_state::active);
    }

    return false;
}

std::unique_ptr<audio_stream_base> create_non_hardware_audio_stream(audio_entry& entry, const audio_stream_config& config, error_events& events, boost::asio::thread_pool& pool)
{
    std::unique_ptr<audio_stream_base> stream_base;

    switch (config.type)
    {
        case audio_stream_config_type::wav_audio_input_stream:
            stream_base = std::make_unique<wav_audio_input_stream>(config.filename);
            break;
        case audio_stream_config_type::wav_audio_output_stream:
            stream_base = std::make_unique<wav_audio_output_stream>(config.filename, config.sample_rate);
            break;
        case audio_stream_config_type::null_audio_stream:
            stream_base = std::make_unique<null_audio_stream>();
            break;
        default:
            return nullptr;
    }

    return std::make_unique<audio_stream_no_throw>(std::move(stream_base), entry, events, pool);
}

bool try_create_ptt_control(ptt_entry& entry, const ptt_control_config& config, error_events& events, boost::asio::thread_pool& pool)
{
    switch (config.type)
    {
        case ptt_control_config_type::null_ptt_control:
            entry.type = ptt_control_type::null;
            entry.ptt_control = std::make_unique<null_ptt_control>();
            return true;
        case ptt_control_config_type::serial_port_ptt_control:
        {
            entry.type = ptt_control_type::serial_port;
            auto serial = std::make_unique<serial_port_no_throw>(std::make_unique<serial_port>(), entry, events, pool);
            entry.ptt_control = std::make_unique<ptt_control_no_throw>(std::make_unique<serial_port_ptt_control>(*serial), entry, events, pool);
            entry.serial_port = std::move(serial);
            return true;
        }
        case ptt_control_config_type::library_ptt_control:
            entry.type = ptt_control_type::library;
            entry.ptt_control = std::make_unique<ptt_control_no_throw>(std::make_unique<library_ptt_control>(entry.ptt_library), entry, events, pool);
            return true;
        case ptt_control_config_type::tcp_ptt_control:
            return false;
        default:
            return false;
    }
}

bool try_create_transport(data_stream_entry& entry, const data_stream_config& config, error_events& events, boost::asio::thread_pool& pool)
{
    switch (config.transport)
    {
        case data_stream_transport_type::tcp:
            entry.transport = std::make_unique<transport_no_throw>(std::make_unique<tcp_transport>(config.bind_address, config.port), entry, events, pool);
            return true;
        case data_stream_transport_type::serial:
            entry.transport = std::make_unique<transport_no_throw>(std::make_unique<serial_transport>(), entry, events, pool);
            return true;
        default:
            return false;
    }
}

std::optional<std::reference_wrapper<audio_entry>> find_audio_entry(std::vector<std::unique_ptr<audio_entry>>& entries, const std::string& name)
{
    for (auto& entry : entries)
    {
        if (entry->name == name)
        {
            return *entry;
        }
    }
    return {};
}

std::optional<std::reference_wrapper<audio_entry>> get_output_stream(const modulator_config& config, std::vector<std::unique_ptr<audio_entry>>& audio_entries)
{
    if (config.audio_output_streams.empty())
    {
        return {};
    }

    if (config.audio_output_streams.size() == 1)
    {
        auto audio_entry = find_audio_entry(audio_entries, config.audio_output_streams[0]);
        if (!audio_entry)
        {
            return {};
        }

        if (audio_entry->get().stream.type() != audio_stream_type::output)
        {
            // A modulator requires an output audio_entry stream
            // If this is not an output stream, skip it
            return {};
        }

        return audio_entry;
    }
    else
    {
        // If the modulator configuration has more than one output stream
        // We have to create chained audio_entry stream
        // Each have to match the sample rate
        return {};
    }
}

std::unique_ptr<modem_entry> create_modem_entry(const modulator_config& config)
{
    auto entry = std::make_unique<modem_entry>();

    entry->name = config.name;
    entry->modulator_config = config;

    switch (config.converter)
    {
        case bitstream_convertor_config_type::fx25_bitstream_converter:
            entry->converter = std::make_unique<fx25_bitstream_converter_adapter>();
            break;
        case bitstream_convertor_config_type::ax25_bitstream_convertor:
            entry->converter = std::make_unique<ax25_bitstream_converter_adapter>();
            break;
        default:
            break;
    }

    entry->modem.converter(*entry->converter);
    entry->modem.baud_rate(config.baud_rate);
    entry->modem.tx_delay(config.tx_delay_ms);
    entry->modem.tx_tail(config.tx_tail_ms);
    entry->modem.gain(config.gain);
    entry->modem.preemphasis(config.preemphasis);
    entry->modem.start_silence(config.begin_silence_ms);
    entry->modem.end_silence(config.end_silence_ms);

    return entry;
}

std::unique_ptr<modulator_base> create_modulator(const modulator_config& config, int sample_rate)
{
    switch (config.type)
    {
        case modulator_config_type::dds_afsk_modulator_double:
            return std::make_unique<dds_afsk_modulator_double_adapter>(
                config.f_mark,
                config.f_space,
                config.baud_rate,
                sample_rate,
                config.tau);
        default:
            return nullptr;
    }
}

std::optional<std::reference_wrapper<ptt_entry>> find_ptt_entry(std::vector<std::unique_ptr<ptt_entry>>& entries, const std::string& name)
{
    for (auto& entry : entries)
    {
        if (entry->name == name)
        {
            return *entry;
        }
    }
    return {};
}

std::optional<std::reference_wrapper<data_stream_entry>> find_data_stream_entry(std::vector<std::unique_ptr<data_stream_entry>>& entries, const std::string& name)
{
    for (auto& entry : entries)
    {
        if (entry->name == name)
        {
            return *entry;
        }
    }
    return {};
}

bool is_output_stream(audio_stream_config_type type)
{
    return type == audio_stream_config_type::wasapi_audio_output_stream ||
        type == audio_stream_config_type::alsa_audio_output_stream ||
        type == audio_stream_config_type::wav_audio_output_stream;
}

bool is_input_stream(audio_stream_config_type type)
{
    return type == audio_stream_config_type::wasapi_audio_input_stream ||
        type == audio_stream_config_type::alsa_audio_input_stream ||
        type == audio_stream_config_type::wav_audio_input_stream;
}

bool requires_audio_hardware(audio_stream_config_type type)
{
    return type != audio_stream_config_type::wav_audio_input_stream &&
        type != audio_stream_config_type::wav_audio_output_stream &&
        type != audio_stream_config_type::null_audio_stream;
}

serial_port_ptt_trigger parse_ptt_trigger(const std::string& trigger)
{
    if (trigger == "off") return serial_port_ptt_trigger::off;
    if (trigger == "on")  return serial_port_ptt_trigger::on;
    return serial_port_ptt_trigger::on; // default
}

serial_port_ptt_line parse_ptt_line(const std::string& line)
{
    if (line == "rts") return serial_port_ptt_line::rts;
    if (line == "dtr") return serial_port_ptt_line::dtr;
    return serial_port_ptt_line::rts; // default
}

std::string platform_name()
{
#if WIN32
    return "windows";
#elif defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "macos";
#else
    return "unknown";
#endif
}

LIBMODEM_NAMESPACE_END