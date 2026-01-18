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

bool is_output_stream(audio_stream_config_type type);
bool is_input_stream(audio_stream_config_type type);
bool requires_audio_device(audio_stream_config_type type);

void pipeline::build(const config& c)
{
    populate_audio_entries(c);
    populate_ptt_controls(c);
}

void pipeline::start()
{
}

void pipeline::populate_audio_entries(const config& c)
{
    std::set<std::string> used_names;
    std::set<std::pair<std::string, libmodem::audio_device_type>> used_devices;
    std::set<std::string> used_files;

    for (const auto& audio_config : c.audio_streams)
    {
        if (used_names.count(audio_config.name) > 0)
        {
            // Skip duplicate config names
            continue;
        }

        libmodem::audio_device device;

        if (requires_audio_device(audio_config.type))
        {
            auto device_type = is_output_stream(audio_config.type) ? libmodem::audio_device_type::render : libmodem::audio_device_type::capture;

            bool found = false;
            if (audio_config.device_name == "default")
            {
                found = libmodem::try_get_default_audio_device(device, device_type);
            }
            else if (!audio_config.device_id.empty())
            {
                found = libmodem::try_get_audio_device_by_id(audio_config.device_id, device);
            }
            else if (!audio_config.device_name.empty())
            {
                found = libmodem::try_get_audio_device_by_name(audio_config.device_name, device, device_type, libmodem::audio_device_state::active);
            }

            if (!found)
            {
                continue;
            }

            auto device_key = std::make_pair(device.id, device_type);
            if (used_devices.count(device_key))
            {
                continue;
            }

            used_devices.insert(device_key);
            used_names.insert(audio_config.name);

            auto stream = device.stream();

            audio_entries_.emplace_back(audio_entry{
                audio_config.name,
                device.name,
                std::move(device),
                std::move(stream),
                audio_config
            });
        }
        else
        {
            if (!audio_config.filename.empty() && used_files.count(audio_config.filename) > 0)
            {
                continue;
            }

            std::unique_ptr<libmodem::audio_stream_base> stream;

            switch (audio_config.type)
            {
                case audio_stream_config_type::wav_audio_input_stream:
                    stream = std::make_unique<libmodem::wav_audio_input_stream>(audio_config.filename);
                    break;
                case audio_stream_config_type::wav_audio_output_stream:
                    stream = std::make_unique<libmodem::wav_audio_output_stream>(audio_config.filename);
                    break;
                case audio_stream_config_type::null_audio_stream:
                    stream = std::make_unique<libmodem::null_audio_stream>();
                    break;
                default:
                    continue;
            }

            if (!audio_config.filename.empty())
            {
                used_files.insert(audio_config.filename);
            }

            used_names.insert(audio_config.name);

            audio_entries_.emplace_back(audio_entry{
                audio_config.name,
                stream->name(),
                std::move(device),
                libmodem::audio_stream(std::move(stream)),
                audio_config
            });
        }
    }
}

void pipeline::populate_ptt_controls(const config& c)
{
    std::set<std::string> used_serial_ports;
    std::set<std::string> used_names;
    std::set<std::string> used_files;

    for (const auto& ptt_config : c.ptt_controls)
    {
        if (used_names.count(ptt_config.name) > 0)
        {
            // Skip duplicate config names
            continue;
        }

        ptt_entry entry;
        entry.name = ptt_config.name;
        entry.port_name = ptt_config.serial_port;
        entry.baud_rate = ptt_config.baud_rate;
        entry.config = ptt_config;
        entry.data_bits = 8;
        entry.stop_bits = libmodem::stop_bits::one;
        entry.parity = libmodem::parity::none;
        entry.flow_control = libmodem::flow_control::none;
        entry.library_path = ptt_config.library_path;

        if (ptt_config.trigger == "off")
        {
            entry.serial_trigger = libmodem::serial_port_ptt_trigger::off;
        }
        else if (ptt_config.trigger == "on")
        {
            entry.serial_trigger = libmodem::serial_port_ptt_trigger::on;
        }

        if (ptt_config.line == "rts")
        {
            entry.serial_line = libmodem::serial_port_ptt_line::rts;
        }
        else if (ptt_config.line == "dtr")
        {
            entry.serial_line = libmodem::serial_port_ptt_line::dtr;
        }

        if (ptt_config.type == ptt_control_config_type::null_ptt_control)
        {
            entry.ptt_control = std::make_unique<libmodem::null_ptt_control>();
            entry.type = ptt_control_type::null;
        }
        else if (ptt_config.type == ptt_control_config_type::serial_port_ptt_control)
        {
            if (used_serial_ports.find(ptt_config.serial_port) != used_serial_ports.end())
            {
                // Serial port already used, skip this entry
                continue;
            }
            used_serial_ports.insert(ptt_config.serial_port);

            entry.serial_port = std::make_unique<libmodem::serial_port>();
            entry.ptt_control = std::make_unique<libmodem::serial_port_ptt_control>(*entry.serial_port);
            entry.type = ptt_control_type::serial_port;
        }
        else if (ptt_config.type == ptt_control_config_type::library_ptt_control)
        {
            if (!ptt_config.library_path.empty() && used_files.count(ptt_config.library_path) > 0)
            {
                continue;
            }

            entry.ptt_control = std::make_unique<libmodem::library_ptt_control>(entry.ptt_library);
            entry.type = ptt_control_type::library;

            if (!ptt_config.library_path.empty())
            {
                used_files.insert(ptt_config.library_path);
            }
        }
        else if (ptt_config.type == ptt_control_config_type::tcp_ptt_control)
        {

        }

        if (entry.ptt_control)
        {
            ptt_controls_.push_back(std::move(entry));
            used_names.insert(ptt_config.name);
        }
    }
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

bool requires_audio_device(audio_stream_config_type type)
{
    return type != audio_stream_config_type::wav_audio_input_stream &&
        type != audio_stream_config_type::wav_audio_output_stream &&
        type != audio_stream_config_type::null_audio_stream;
}