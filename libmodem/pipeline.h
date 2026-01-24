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
#include "data_stream.h"

#include <vector>
#include <memory>
#include <set>

#ifndef LIBMODEM_NAMESPACE
#define LIBMODEM_NAMESPACE libmodem
#endif
#ifndef LIBMODEM_NAMESPACE_BEGIN
#define LIBMODEM_NAMESPACE_BEGIN namespace LIBMODEM_NAMESPACE {
#endif
#ifndef LIBMODEM_NAMESPACE_REFERENCE
#define LIBMODEM_NAMESPACE_REFERENCE libmodem :: 
#endif
#ifndef LIBMODEM_NAMESPACE_END
#define LIBMODEM_NAMESPACE_END }
#endif

LIBMODEM_NAMESPACE_BEGIN

struct audio_entry
{
    std::string name;
    std::string display_name;
    audio_device device;
    audio_stream stream = nullptr;
    audio_stream_config config;
    std::vector<std::string> referenced_by;
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
    std::unique_ptr<ptt_control_base> ptt_control;
    std::unique_ptr<serial_port_base> serial_port;
    tcp_ptt_control_client tcp_ptt_client;
    ptt_control_library ptt_library;
    std::string port_name;
    unsigned int baud_rate = 9600;
    unsigned int data_bits = 8;
    enum parity parity = parity::none;
    enum stop_bits stop_bits = stop_bits::one;
    enum flow_control flow_control = flow_control::none;
    enum serial_port_ptt_line serial_line = serial_port_ptt_line::rts;
    enum serial_port_ptt_trigger serial_trigger = serial_port_ptt_trigger::on;
    std::string library_path;
    ptt_control_config config;
};

struct modem_entry
{
    std::string name;
    struct modem modem;
    std::unique_ptr<bitstream_converter_base> converter;
    std::unique_ptr<modulator_base> modulator;
    struct modulator_config modulator_config;
};

struct data_stream_entry
{
    std::string name;
    std::unique_ptr<transport> transport;
    std::unique_ptr<formatter> formatter;
    std::unique_ptr<modem_data_stream> data_stream;
    std::vector<std::string> referenced_by;
    data_stream_config config;
};

struct pipeline_events
{
};

class pipeline
{
public:
    pipeline(const config& c);
    ~pipeline();

    void init();
    void start();
    void stop();
    void wait_stopped();

private:
    void populate_audio_entries();
    void populate_ptt_controls();
    void populate_transmit_modems();
    void populate_receive_modems();
    void populate_data_streams();

    void assign_audio_streams();
    void assign_data_streams();
    void assign_ptt_controls();

    bool can_add_audio_entry(const audio_stream_config& audio_config);
    bool is_duplicate_audio_name(const std::string& name);
    bool is_duplicate_audio_device(const std::string& device_id, audio_device_type type);
    bool is_duplicate_audio_file(const std::string& filename);
    bool is_valid_audio_config(const audio_stream_config& audio_config);
    void register_audio_entry(const audio_entry& entry, const audio_stream_config& audio_config);
    bool can_add_ptt_control(const ptt_control_config& ptt_config);
    bool is_duplicate_ptt_name(const std::string& name);
    bool is_duplicate_serial_port(const std::string& port);
    bool is_duplicate_library_file(const std::string& path);
    bool is_valid_ptt_config(const ptt_control_config& ptt_config);
    void register_ptt_control(const ptt_entry& entry, const ptt_control_config& ptt_config);
    bool can_add_modem(const modulator_config& modulator_config);
    bool is_duplicate_modem_name(const std::string& name);
    void register_modem(const modem_entry& entry);
    bool is_audio_stream_referenced(const std::string& name);
    bool is_ptt_control_referenced(const std::string& name);
    bool can_add_data_stream(const data_stream_config& config);
    bool is_duplicate_data_stream_name(const std::string& name);
    bool is_duplicate_tcp_port(int port);
    bool is_valid_data_stream_config(const data_stream_config& config);
    void register_data_stream(const data_stream_entry& entry, const data_stream_config& config);
    bool is_data_stream_referenced(const std::string& name);

    const struct config config;
    std::vector<modem_entry> modems_;
    std::vector<ptt_entry> ptt_controls_;
    std::vector<audio_entry> audio_entries_;
    std::vector<data_stream_entry> data_streams_;

    std::set<std::string> used_audio_names_;
    std::set<std::pair<std::string, audio_device_type>> used_audio_devices_;
    std::set<std::string> used_audio_files_;
    std::set<std::string> used_serial_ports_;
    std::set<std::string> used_library_files_;
    std::set<std::string> used_ptt_names_;
    std::set<std::string> used_modem_names_;
    std::set<std::string> used_data_stream_names_;
    std::set<int> used_tcp_ports_;
};

LIBMODEM_NAMESPACE_END
