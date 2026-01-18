// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// pipeline.h
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

#include "modem.h"
#include "io.h"
#include "audio_stream.h"
#include "bitstream.h"
#include "config.h"

#include <vector>
#include <memory>
#include <set>

struct audio_entry
{
    std::string name;
    std::string display_name;
    libmodem::audio_device device;
    libmodem::audio_stream stream;
    audio_stream_config config;
};

enum class ptt_control_type
{
    unknown,
    serial_port,
    null,
    library,
    tcp
};

struct ptt_entry
{
    std::string name;
    std::string display_name;
    ptt_control_type type = ptt_control_type::unknown;
    std::unique_ptr<libmodem::ptt_control_base> ptt_control;
    std::unique_ptr<libmodem::serial_port_base> serial_port;
    libmodem::tcp_ptt_control_client tcp_ptt_client;
    libmodem::ptt_control_library ptt_library;
    std::string port_name;
    unsigned int baud_rate = 9600;
    unsigned int data_bits = 8;
    libmodem::parity parity = libmodem::parity::none;
    libmodem::stop_bits stop_bits = libmodem::stop_bits::one;
    libmodem::flow_control flow_control = libmodem::flow_control::none;
    libmodem::serial_port_ptt_line serial_line = libmodem::serial_port_ptt_line::rts;
    libmodem::serial_port_ptt_trigger serial_trigger = libmodem::serial_port_ptt_trigger::on;
    std::string library_path;
    ptt_control_config config;
};

struct modem_entry
{
    std::string name;
    libmodem::modem modem;
    std::unique_ptr<libmodem::bitstream_converter_base> converter;
};

class pipeline
{
public:
    std::vector<modem_entry> modems_;
    std::vector<ptt_entry> ptt_controls_;
    std::vector<audio_entry> audio_entries_;

    void build(const config& c);
    void start();
    void stop();

private:
    void populate_audio_entries(const config& c);
    void populate_ptt_controls(const config& c);
};
