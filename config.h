// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// config.h
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

#pragma once

#include <string>
#include <vector>

enum class audio_stream_config_type
{
    unknown,
    null_audio_stream,
    wasapi_audio_output_stream,
    wasapi_audio_input_stream,
    alsa_audio_output_stream,
    alsa_audio_input_stream,
    wav_audio_input_stream,
    wav_audio_output_stream
};

struct audio_stream_config
{
    std::string name;
    audio_stream_config_type type;
    std::string device_name;
    std::string device_id;
    int volume = 100;
    std::string host;
    int audio_port = 0;
    int control_port = 0;
    std::string filename;
};

enum class ptt_control_config_type
{
    unknown,
    serial_port_ptt_control,
    null_ptt_control,
    library_ptt_control,
    tcp_ptt_control
};

struct ptt_control_config
{
    std::string name;
    ptt_control_config_type type;
    std::string serial_port;
    int baud_rate = 9600;
    std::string host;
    int port = 0;
    std::string line = "rts";
    std::string trigger = "on";
    std::string library_path;
    std::string platform = "";
};

struct config
{
    std::vector<audio_stream_config> audio_streams;
    std::vector<ptt_control_config> ptt_controls;
};

config read_config(const std::string& filename);
