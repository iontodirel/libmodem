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

LIBMODEM_NAMESPACE_BEGIN

bool try_create_audio_entry(const audio_stream_config& config, audio_entry& entry);
bool try_find_audio_device(const audio_stream_config& audio_config, audio_device& device);
std::unique_ptr<audio_stream_base> create_non_hardware_audio_stream(const audio_stream_config& config);
bool try_create_ptt_control(ptt_entry& entry, const ptt_control_config& config);
std::unique_ptr<bitstream_converter_base> create_converter(bitstream_convertor_config_type type);
std::unique_ptr<modulator_base> create_modulator(const modulator_config& config, int sample_rate);
std::optional<std::reference_wrapper<audio_entry>> find_audio_entry(std::vector<audio_entry>& entries, const std::string& name);
std::optional<std::reference_wrapper<audio_entry>> get_output_stream(const modulator_config& config, std::vector<audio_entry>& audio_entries);
modem_entry create_modem_entry(const modulator_config& config, struct audio_entry& audio_entry);
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
// pipeline                                                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

pipeline::pipeline(const struct config& c) : config(c)
{
}

pipeline::~pipeline() = default;

void pipeline::init()
{
    modems_.clear();
    ptt_controls_.clear();
    audio_entries_.clear();

    used_audio_names_.clear();
    used_audio_devices_.clear();
    used_audio_files_.clear();
    used_serial_ports_.clear();
    used_library_files_.clear();
    used_ptt_names_.clear();
    used_modem_names_.clear();

    populate_audio_entries();
    populate_ptt_controls();
    populate_transmit_modems();
    populate_receive_modems();
}

void pipeline::start()
{
}

void pipeline::wait_stopped()
{
}

void pipeline::populate_audio_entries()
{
    for (const auto& audio_config : config.audio_streams)
    {
        if (!can_add_audio_entry(audio_config))
        {
            continue;
        }

        audio_entry entry;
        if (!try_create_audio_entry(audio_config, entry))
        {
            continue;
        }

        // Book keeping of used filenames and names
        // For hardware streams, check device uniqueness after creation
        // (device info is only available after try_create_audio_entry)
        if (requires_audio_hardware(audio_config.type))
        {
            if (is_duplicate_audio_device(entry.device.id, entry.device.type))
            {
                continue;
            }
        }

        register_audio_entry(entry, audio_config);

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

        ptt_entry entry;
        entry.name = ptt_config.name;
        entry.port_name = ptt_config.serial_port;
        entry.baud_rate = ptt_config.baud_rate;
        entry.config = ptt_config;
        entry.library_path = ptt_config.library_path;
        entry.serial_trigger = parse_ptt_trigger(ptt_config.trigger);
        entry.serial_line = parse_ptt_line(ptt_config.line);

        if (try_create_ptt_control(entry, ptt_config))
        {
            register_ptt_control(entry, ptt_config);
            ptt_controls_.push_back(std::move(entry));
        }
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

        // Find a matching audio_entry representing an output stream for this modem
        auto audio_entry = get_output_stream(modulator_config, audio_entries_);
        if (!audio_entry)
        {
            // Could not find any matching output audio_entry streams, skip this modem
            continue;
        }

        // Setup the modem's audio_entry output stream

        modem_entry entry = create_modem_entry(modulator_config, audio_entry->get());

        audio_entry->get().referenced_by.push_back(entry.name);

        register_modem(entry);

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
    if (platform_name() != ptt_config.platform)
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

// **************************************************************** //
//                                                                  //
//                                                                  //
// utility functions                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool try_create_audio_entry(const audio_stream_config& config, audio_entry& entry)
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
        entry.stream = device.stream();
        entry.device = std::move(device);
        entry.config = config;

        return true;
    }
    else
    {
        auto stream = create_non_hardware_audio_stream(config);
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

std::unique_ptr<audio_stream_base> create_non_hardware_audio_stream(const audio_stream_config& config)
{
    switch (config.type)
    {
        case audio_stream_config_type::wav_audio_input_stream:
            return std::make_unique<wav_audio_input_stream>(config.filename);
        case audio_stream_config_type::wav_audio_output_stream:
            return std::make_unique<wav_audio_output_stream>(config.filename, config.sample_rate);
        case audio_stream_config_type::null_audio_stream:
            return std::make_unique<null_audio_stream>();
        default:
            return nullptr;
    }
}

bool try_create_ptt_control(ptt_entry& entry, const ptt_control_config& config)
{
    switch (config.type)
    {
        case ptt_control_config_type::null_ptt_control:
            entry.type = ptt_control_type::null;
            entry.ptt_control = std::make_unique<null_ptt_control>();
            return true;

        case ptt_control_config_type::serial_port_ptt_control:
            entry.type = ptt_control_type::serial_port;
            entry.serial_port = std::make_unique<serial_port>();
            entry.ptt_control = std::make_unique<serial_port_ptt_control>(*entry.serial_port);
            return true;

        case ptt_control_config_type::library_ptt_control:
            entry.type = ptt_control_type::library;
            entry.ptt_control = std::make_unique<library_ptt_control>(entry.ptt_library);
            return true;

        case ptt_control_config_type::tcp_ptt_control:
            // Not yet implemented
            return false;

        default:
            return false;
    }
}

std::unique_ptr<bitstream_converter_base> create_converter(bitstream_convertor_config_type type)
{
    switch (type)
    {
        case bitstream_convertor_config_type::fx25_bitstream_converter:
            return std::make_unique<fx25_bitstream_converter_adapter>();
        case bitstream_convertor_config_type::ax25_bitstream_convertor:
            return std::make_unique<ax25_bitstream_converter_adapter>();
        default:
            return nullptr;
    }
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

std::optional<std::reference_wrapper<audio_entry>> find_audio_entry(std::vector<audio_entry>& entries, const std::string& name)
{
    for (auto& entry : entries)
    {
        if (entry.name == name)
        {
            return entry;
        }
    }
    return {};
}

std::optional<std::reference_wrapper<audio_entry>> get_output_stream(const modulator_config& config, std::vector<audio_entry>& audio_entries)
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

modem_entry create_modem_entry(const modulator_config& config, struct audio_entry& audio_entry)
{
    modem_entry entry;

    entry.name = config.name;

    entry.modem.output_stream(audio_entry.stream.get());

    entry.converter = create_converter(config.converter);
    entry.modulator = create_modulator(config, entry.modem.output_stream().sample_rate());

    entry.modem.converter(*entry.converter);
    entry.modem.modulator(*entry.modulator);
    entry.modem.baud_rate(config.baud_rate);
    entry.modem.tx_delay(config.tx_delay_ms);
    entry.modem.tx_tail(config.tx_tail_ms);
    entry.modem.gain(config.gain);
    entry.modem.preemphasis(config.preemphasis);
    entry.modem.start_silence(config.begin_silence_ms);
    entry.modem.end_silence(config.end_silence_ms);

    entry.modem.initialize();

    return entry;
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