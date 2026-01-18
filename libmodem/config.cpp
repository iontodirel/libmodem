// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// config.cpp
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

#include "config.h"

#include <nlohmann/json.hpp>
#include <fstream>

audio_stream_config_type parse_audio_stream_type(const std::string& type_str);
audio_stream_config parse_audio_stream(const nlohmann::json& j);
ptt_control_config_type parse_ptt_control_type(const std::string& type_str);
ptt_control_config parse_ptt_control(const nlohmann::json& j);

config read_config(const std::string& filename)
{
    config c;

    std::ifstream file(filename);
    if (!file.is_open())
    {
        return c;
    }

    nlohmann::json j = nlohmann::json::parse(file, nullptr, true, true);

    if (j.contains("audio_streams") && j["audio_streams"].is_array())
    {
        for (const auto& stream : j["audio_streams"])
        {
            c.audio_streams.push_back(parse_audio_stream(stream));
        }
    }

    if (j.contains("ptt_controls") && j["ptt_controls"].is_array())
    {
        for (const auto& ptt : j["ptt_controls"])
        {
            c.ptt_controls.push_back(parse_ptt_control(ptt));
        }
    }

    return c;
}

audio_stream_config parse_audio_stream(const nlohmann::json& j)
{
    audio_stream_config c;

    c.name = j.value("name", "");
    c.type = parse_audio_stream_type(j.value("type", ""));
    c.device_name = j.value("device_name", "");
    c.device_id = j.value("device_id", "");
    c.volume = j.value("volume", 100);
    c.host = j.value("host", "");
    c.audio_port = j.value("audio_port", 0);
    c.control_port = j.value("control_port", 0);
    c.filename = j.value("file_path", "");

    return c;
}

ptt_control_config parse_ptt_control(const nlohmann::json& j)
{
    ptt_control_config c;

    c.name = j.value("name", "");
    c.type = parse_ptt_control_type(j.value("type", ""));
    c.serial_port = j.value("port", "");
    c.baud_rate = j.value("baud_rate", 9600);
    c.host = j.value("host", "");
    c.port = j.value("pin_number", 0);
    c.line = j.value("line", "rts");
    c.trigger = j.value("active", "on");
    c.library_path = j.value("path", "");
    c.platform = j.value("platform", "");

    return c;
}

audio_stream_config_type parse_audio_stream_type(const std::string& type_str)
{
    if (type_str == "null_audio_stream") return audio_stream_config_type::null_audio_stream;
    if (type_str == "wasapi_audio_output_stream") return audio_stream_config_type::wasapi_audio_output_stream;
    if (type_str == "wasapi_audio_input_stream") return audio_stream_config_type::wasapi_audio_input_stream;
    if (type_str == "alsa_audio_output_stream") return audio_stream_config_type::alsa_audio_output_stream;
    if (type_str == "alsa_audio_input_stream") return audio_stream_config_type::alsa_audio_input_stream;
    if (type_str == "wav_audio_input_stream") return audio_stream_config_type::wav_audio_input_stream;
    if (type_str == "wav_audio_output_stream") return audio_stream_config_type::wav_audio_output_stream;

    return audio_stream_config_type::unknown;
}

ptt_control_config_type parse_ptt_control_type(const std::string& type_str)
{
    if (type_str == "serial_ptt_stream") return ptt_control_config_type::serial_port_ptt_control;
    if (type_str == "library_ptt_control") return ptt_control_config_type::library_ptt_control;
    if (type_str == "tcp_ptt_stream") return ptt_control_config_type::tcp_ptt_control;
    if (type_str == "null_ptt_stream") return ptt_control_config_type::null_ptt_control;

    return ptt_control_config_type::unknown;
}
