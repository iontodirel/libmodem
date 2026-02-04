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
#include <atomic>
#include <filesystem>

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

struct audio_entry;
struct ptt_entry;
struct modem_entry;
struct data_stream_entry;
struct logger_entry;

class audio_stream_no_throw;
class ptt_control_no_throw;
class transport_no_throw;
class serial_port_no_throw;
class logger_base;

// **************************************************************** //
//                                                                  //
//                                                                  //
// setup_console                                                    //
//                                                                  //
//                                                                  //
// **************************************************************** //

void setup_console();

// **************************************************************** //
//                                                                  //
//                                                                  //
// error_info                                                       //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct error_info
{
    std::string message;
    std::exception_ptr exception;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// error_events                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct error_events
{
    virtual ~error_events() = default;

    virtual void on_error(audio_stream_no_throw& component, audio_entry& entry, const error_info& error) = 0;
    virtual void on_error(ptt_control_no_throw& component, ptt_entry& entry, const error_info& error) = 0;
    virtual void on_error(serial_port_no_throw& component, ptt_entry& entry, const error_info& error) = 0;
    virtual void on_error(transport_no_throw& component, data_stream_entry& entry, const error_info& error) = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// file_write_policy                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

enum class rotation_mode
{
    new_file_per_write,
    rotate_on_size
};

struct file_policy_config
{
    std::string base_path;
    rotation_mode mode = rotation_mode::rotate_on_size;
    size_t max_file_size = 10 * 1024 * 1024; // 10 MB
    size_t max_file_count = 5;
};

class file_write_policy
{
public:
    virtual ~file_write_policy() = default;

    virtual std::string resolve_write_path(size_t bytes_to_write) = 0;
    virtual void on_write_complete(size_t bytes_written) = 0;

    virtual size_t current_file_index() const = 0;
    virtual size_t current_file_size() const = 0;
    virtual bool should_truncate() const = 0;

    virtual void reset() = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// rotating_file_policy                                             //
//                                                                  //
//                                                                  //
// **************************************************************** //

class rotating_file_policy : public file_write_policy
{
public:
    explicit rotating_file_policy(file_policy_config config);

    std::string resolve_write_path(size_t bytes_to_write) override;
    void on_write_complete(size_t bytes_written) override;

    size_t current_file_index() const override;
    size_t current_file_size() const override;
    bool should_truncate() const override;

    void reset() override;

    const file_policy_config& config() const;

private:
    void parse_base_path();
    std::string build_path(size_t index) const;
    void scan_existing_files();
    void rotate_to_next_file();

    file_policy_config config_;
    std::filesystem::path stem_;
    std::string extension_;

    mutable std::mutex mutex_;
    size_t current_index_ = 0;
    size_t current_file_size_ = 0;
    bool should_truncate_ = false;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// audio_entry                                                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct audio_entry
{
    std::string name;
    bool enabled = true;
    std::atomic<bool> faulted{ false };
    std::string display_name;
    audio_device device;
    audio_stream stream = nullptr;
    audio_stream_config config;
    std::vector<std::string> referenced_by;
    std::optional<std::reference_wrapper<modem_entry>> associated_modem_entry;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// ptt_entry                                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

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
    bool enabled = true;
    std::atomic<bool> faulted{ false };
    std::atomic<bool> serial_port_faulted{ false };
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
    std::vector<std::string> referenced_by;
    ptt_control_config config;
    std::optional<std::reference_wrapper<modem_entry>> associated_modem_entry;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// modem_entry                                                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct modem_entry
{
    std::string name;
    bool enabled = true;
    struct modem modem;
    std::unique_ptr<bitstream_converter_base> converter;
    std::string converter_name;
    std::unique_ptr<modulator_base> modulator;
    struct modulator_config modulator_config;
    std::optional<std::reference_wrapper<data_stream_entry>> associated_data_stream_entry;
    std::optional<std::reference_wrapper<audio_entry>> associated_audio_entry;
    std::optional<std::reference_wrapper<ptt_entry>> associated_ptt_entry;
    std::vector<std::reference_wrapper<logger_entry>> associated_loggers;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// data_stream_entry                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct data_stream_entry
{
    std::string name;
    bool enabled = true;
    std::atomic<bool> faulted{ false };
    std::unique_ptr<struct transport> transport;
    std::unique_ptr<struct formatter> formatter;
    std::unique_ptr<modem_data_stream> data_stream;
    std::vector<std::string> referenced_by;
    data_stream_config config;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// logger_entry                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct logger_entry
{
    std::string name;
    logger_config config;
    std::unique_ptr<file_write_policy> file_policy;
    std::unique_ptr<logger_base> logger;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// pipeline_events                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct pipeline_events
{
    virtual ~pipeline_events() = default;
    virtual void on_started(uint64_t seq) = 0;
    virtual void on_stopped(uint64_t seq) = 0;
    virtual void on_log(uint64_t seq, logger_base& logger, uint64_t id) = 0;
    virtual void on_audio_stream_created(uint64_t seq, audio_entry& entry) = 0;
    virtual void on_audio_stream_init_failed(uint64_t seq, const audio_stream_config& config, const std::string& reason) = 0;
    virtual void on_audio_stream_faulted(uint64_t seq, audio_entry& entry, const error_info& error) = 0;
    virtual void on_audio_stream_recovery_started(uint64_t seq, audio_entry& entry) = 0;
    virtual void on_audio_stream_recovery_attempt(uint64_t seq, audio_entry& entry, int attempt, int max_attempts) = 0;
    virtual void on_audio_stream_recovered(uint64_t seq, audio_entry& entry) = 0;
    virtual void on_audio_stream_recovery_failed(uint64_t seq, audio_entry& entry) = 0;
    virtual void on_ptt_control_created(uint64_t seq, ptt_entry& entry) = 0;
    virtual void on_ptt_control_init_failed(uint64_t seq, const ptt_control_config& config, const std::string& reason) = 0;
    virtual void on_ptt_control_faulted(uint64_t seq, ptt_entry& entry, const error_info& error) = 0;
    virtual void on_ptt_control_recovery_started(uint64_t seq, ptt_entry& entry) = 0;
    virtual void on_ptt_control_recovery_attempt(uint64_t seq, ptt_entry& entry, int attempt, int max_attempts) = 0;
    virtual void on_ptt_control_recovered(uint64_t seq, ptt_entry& entry) = 0;
    virtual void on_ptt_control_recovery_failed(uint64_t seq, ptt_entry& entry) = 0;
    virtual void on_ptt_activated(uint64_t seq, ptt_entry& entry) = 0;
    virtual void on_ptt_deactivated(uint64_t seq, ptt_entry& entry) = 0;
    virtual void on_serial_port_faulted(uint64_t seq, ptt_entry& entry, const error_info& error) = 0;
    virtual void on_serial_port_recovery_started(uint64_t seq, ptt_entry& entry) = 0;
    virtual void on_serial_port_recovery_attempt(uint64_t seq, ptt_entry& entry, int attempt, int max_attempts) = 0;
    virtual void on_serial_port_recovered(uint64_t seq, ptt_entry& entry) = 0;
    virtual void on_serial_port_recovery_failed(uint64_t seq, ptt_entry& entry) = 0;
    virtual void on_transport_created(uint64_t seq, data_stream_entry& entry) = 0;
    virtual void on_transport_init_failed(uint64_t seq, const data_stream_config& config, const std::string& reason) = 0;
    virtual void on_transport_faulted(uint64_t seq, data_stream_entry& entry, const error_info& error) = 0;
    virtual void on_transport_recovery_started(uint64_t seq, data_stream_entry& entry) = 0;
    virtual void on_transport_recovery_attempt(uint64_t seq, data_stream_entry& entry, int attempt, int max_attempts) = 0;
    virtual void on_transport_recovered(uint64_t seq, data_stream_entry& entry) = 0;
    virtual void on_transport_recovery_failed(uint64_t seq, data_stream_entry& entry) = 0;
    virtual void on_client_connected(uint64_t seq, data_stream_entry& entry, const tcp_client_connection& connection) = 0;
    virtual void on_client_disconnected(uint64_t seq, data_stream_entry& entry, const tcp_client_connection& connection) = 0;
    virtual void on_data_stream_created(uint64_t seq, data_stream_entry& entry) = 0;
    virtual void on_data_stream_started(uint64_t seq, data_stream_entry& entry) = 0;
    virtual void on_data_stream_stopped(uint64_t seq, data_stream_entry& entry) = 0;
    virtual void on_data_stream_enabled(uint64_t seq, data_stream_entry& entry) = 0;
    virtual void on_data_stream_disabled(uint64_t seq, data_stream_entry& entry) = 0;
    virtual void on_modem_created(uint64_t seq, modem_entry& entry) = 0;
    virtual void on_modem_init_failed(uint64_t seq, const modulator_config& config, const std::string& reason) = 0;
    virtual void on_modem_initialized(uint64_t seq, modem_entry& entry) = 0;
    virtual void on_modem_transmit(uint64_t seq, const packet& p, uint64_t id) = 0;
    virtual void on_modem_transmit(uint64_t seq, modem_entry& entry, const std::vector<uint8_t>& bitstream, uint64_t id) = 0;
    virtual void on_modem_before_start_render_audio(uint64_t seq, audio_entry& entry, uint64_t id) = 0;
    virtual void on_modem_end_render_audio(uint64_t seq, audio_entry& entry, const std::vector<double>& samples, size_t count, uint64_t id) = 0;
    virtual void on_modem_ptt(uint64_t seq, bool state, uint64_t id) = 0;
    virtual void on_volume_changed(uint64_t seq, audio_entry& entry, int previous_volume, int new_volume, uint64_t id) = 0;
    virtual void on_begin_packet_received(modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) = 0;
    virtual void on_packet_received(uint64_t seq, modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) = 0;
    virtual void on_packet_transmit_started(uint64_t seq, modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) = 0;
    virtual void on_packet_transmit_completed(uint64_t seq, modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// pipeline_events_default                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct pipeline_events_default : public pipeline_events
{
    void on_started(uint64_t seq) override;
    void on_stopped(uint64_t seq) override;
    void on_log(uint64_t seq, logger_base& logger, uint64_t id) override;
    void on_audio_stream_created(uint64_t seq, audio_entry& entry) override;
    void on_audio_stream_init_failed(uint64_t seq, const audio_stream_config& config, const std::string& reason) override;
    void on_audio_stream_faulted(uint64_t seq, audio_entry& entry, const error_info& error) override;
    void on_audio_stream_recovery_started(uint64_t seq, audio_entry& entry) override;
    void on_audio_stream_recovery_attempt(uint64_t seq, audio_entry& entry, int attempt, int max_attempts) override;
    void on_audio_stream_recovered(uint64_t seq, audio_entry& entry) override;
    void on_audio_stream_recovery_failed(uint64_t seq, audio_entry& entry) override;
    void on_ptt_control_created(uint64_t seq, ptt_entry& entry) override;
    void on_ptt_control_init_failed(uint64_t seq, const ptt_control_config& config, const std::string& reason) override;
    void on_ptt_control_faulted(uint64_t seq, ptt_entry& entry, const error_info& error) override;
    void on_ptt_control_recovery_started(uint64_t seq, ptt_entry& entry) override;
    void on_ptt_control_recovery_attempt(uint64_t seq, ptt_entry& entry, int attempt, int max_attempts) override;
    void on_ptt_control_recovered(uint64_t seq, ptt_entry& entry) override;
    void on_ptt_control_recovery_failed(uint64_t seq, ptt_entry& entry) override;
    void on_ptt_activated(uint64_t seq, ptt_entry& entry) override;
    void on_ptt_deactivated(uint64_t seq, ptt_entry& entry) override;
    void on_serial_port_faulted(uint64_t seq, ptt_entry& entry, const error_info& error) override;
    void on_serial_port_recovery_started(uint64_t seq, ptt_entry& entry) override;
    void on_serial_port_recovery_attempt(uint64_t seq, ptt_entry& entry, int attempt, int max_attempts) override;
    void on_serial_port_recovered(uint64_t seq, ptt_entry& entry) override;
    void on_serial_port_recovery_failed(uint64_t seq, ptt_entry& entry) override;
    void on_transport_created(uint64_t seq, data_stream_entry& entry) override;
    void on_transport_init_failed(uint64_t seq, const data_stream_config& config, const std::string& reason) override;
    void on_transport_faulted(uint64_t seq, data_stream_entry& entry, const error_info& error) override;
    void on_transport_recovery_started(uint64_t seq, data_stream_entry& entry) override;
    void on_transport_recovery_attempt(uint64_t seq, data_stream_entry& entry, int attempt, int max_attempts) override;
    void on_transport_recovered(uint64_t seq, data_stream_entry& entry) override;
    void on_transport_recovery_failed(uint64_t seq, data_stream_entry& entry) override;
    void on_client_connected(uint64_t seq, data_stream_entry& entry, const tcp_client_connection& connection) override;
    void on_client_disconnected(uint64_t seq, data_stream_entry& entry, const tcp_client_connection& connection) override;
    void on_data_stream_created(uint64_t seq, data_stream_entry& entry) override;
    void on_data_stream_started(uint64_t seq, data_stream_entry& entry) override;
    void on_data_stream_stopped(uint64_t seq, data_stream_entry& entry) override;
    void on_data_stream_enabled(uint64_t seq, data_stream_entry& entry) override;
    void on_data_stream_disabled(uint64_t seq, data_stream_entry& entry) override;
    void on_modem_created(uint64_t seq, modem_entry& entry) override;
    void on_modem_init_failed(uint64_t seq, const modulator_config& config, const std::string& reason) override;
    void on_modem_initialized(uint64_t seq, modem_entry& entry) override;
    void on_modem_transmit(uint64_t seq, const packet& p, uint64_t id) override;
    void on_modem_transmit(uint64_t seq, modem_entry& entry, const std::vector<uint8_t>& bitstream, uint64_t id) override;
    void on_modem_before_start_render_audio(uint64_t seq, audio_entry& entry, uint64_t id) override;
    void on_modem_end_render_audio(uint64_t seq, audio_entry& entry, const std::vector<double>& samples, size_t count, uint64_t id) override;
    void on_modem_ptt(uint64_t seq, bool state, uint64_t id) override;
    void on_volume_changed(uint64_t seq, audio_entry& entry, int previous_volume, int new_volume, uint64_t id) override;
    void on_begin_packet_received(modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) override;
    void on_packet_received(uint64_t seq, modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) override;
    void on_packet_transmit_started(uint64_t seq, modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) override;
    void on_packet_transmit_completed(uint64_t seq, modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) override;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// pipeline_events_rich                                             //
//                                                                  //
//                                                                  //
// **************************************************************** //

class pipeline_events_rich : public pipeline_events
{
public:
    void on_started(uint64_t seq) override;
    void on_stopped(uint64_t seq) override;
    void on_log(uint64_t seq, logger_base& logger, uint64_t id) override;
    void on_audio_stream_created(uint64_t seq, audio_entry& entry) override;
    void on_audio_stream_init_failed(uint64_t seq, const audio_stream_config& config, const std::string& reason) override;
    void on_audio_stream_faulted(uint64_t seq, audio_entry& entry, const error_info& error) override;
    void on_audio_stream_recovery_started(uint64_t seq, audio_entry& entry) override;
    void on_audio_stream_recovery_attempt(uint64_t seq, audio_entry& entry, int attempt, int max_attempts) override;
    void on_audio_stream_recovered(uint64_t seq, audio_entry& entry) override;
    void on_audio_stream_recovery_failed(uint64_t seq, audio_entry& entry) override;
    void on_ptt_control_created(uint64_t seq, ptt_entry& entry) override;
    void on_ptt_control_init_failed(uint64_t seq, const ptt_control_config& config, const std::string& reason) override;
    void on_ptt_control_faulted(uint64_t seq, ptt_entry& entry, const error_info& error) override;
    void on_ptt_control_recovery_started(uint64_t seq, ptt_entry& entry) override;
    void on_ptt_control_recovery_attempt(uint64_t seq, ptt_entry& entry, int attempt, int max_attempts) override;
    void on_ptt_control_recovered(uint64_t seq, ptt_entry& entry) override;
    void on_ptt_control_recovery_failed(uint64_t seq, ptt_entry& entry) override;
    void on_ptt_activated(uint64_t seq, ptt_entry& entry) override;
    void on_ptt_deactivated(uint64_t seq, ptt_entry& entry) override;
    void on_serial_port_faulted(uint64_t seq, ptt_entry& entry, const error_info& error) override;
    void on_serial_port_recovery_started(uint64_t seq, ptt_entry& entry) override;
    void on_serial_port_recovery_attempt(uint64_t seq, ptt_entry& entry, int attempt, int max_attempts) override;
    void on_serial_port_recovered(uint64_t seq, ptt_entry& entry) override;
    void on_serial_port_recovery_failed(uint64_t seq, ptt_entry& entry) override;
    void on_transport_created(uint64_t seq, data_stream_entry& entry) override;
    void on_transport_init_failed(uint64_t seq, const data_stream_config& config, const std::string& reason) override;
    void on_transport_faulted(uint64_t seq, data_stream_entry& entry, const error_info& error) override;
    void on_transport_recovery_started(uint64_t seq, data_stream_entry& entry) override;
    void on_transport_recovery_attempt(uint64_t seq, data_stream_entry& entry, int attempt, int max_attempts) override;
    void on_transport_recovered(uint64_t seq, data_stream_entry& entry) override;
    void on_transport_recovery_failed(uint64_t seq, data_stream_entry& entry) override;
    void on_client_connected(uint64_t seq, data_stream_entry& entry, const tcp_client_connection& connection) override;
    void on_client_disconnected(uint64_t seq, data_stream_entry& entry, const tcp_client_connection& connection) override;
    void on_data_stream_created(uint64_t seq, data_stream_entry& entry) override;
    void on_data_stream_started(uint64_t seq, data_stream_entry& entry) override;
    void on_data_stream_stopped(uint64_t seq, data_stream_entry& entry) override;
    void on_data_stream_enabled(uint64_t seq, data_stream_entry& entry) override;
    void on_data_stream_disabled(uint64_t seq, data_stream_entry& entry) override;
    void on_modem_created(uint64_t seq, modem_entry& entry) override;
    void on_modem_initialized(uint64_t seq, modem_entry& entry) override;
    void on_modem_init_failed(uint64_t seq, const modulator_config& config, const std::string& reason) override;
    void on_modem_transmit(uint64_t seq, const packet& p, uint64_t id) override;
    void on_modem_transmit(uint64_t seq, modem_entry& entry, const std::vector<uint8_t>& bitstream, uint64_t id) override;
    void on_modem_before_start_render_audio(uint64_t seq, audio_entry& entry, uint64_t id) override;
    void on_modem_end_render_audio(uint64_t seq, audio_entry& entry, const std::vector<double>& audio, uint64_t sample_rate, uint64_t id) override;
    void on_modem_ptt(uint64_t seq, bool state, uint64_t id) override;
    void on_volume_changed(uint64_t seq, audio_entry& entry, int previous_volume, int new_volume, uint64_t id) override;
    void on_begin_packet_received(modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) override;
    void on_packet_received(uint64_t seq, modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) override;
    void on_packet_transmit_started(uint64_t seq, modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) override;
    void on_packet_transmit_completed(uint64_t seq, modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) override;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// logger_events                                                    //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct logger_events
{
    virtual ~logger_events() = default;
    virtual void on_log(logger_base& logger, uint64_t id) = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// logger_base                                                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

class logger_base : public pipeline_events
{
public:
    virtual ~logger_base() = default;

    virtual void start();
    virtual void stop();

    const std::string& name() const
    {
        return name_;
    }
    void name(const std::string& n)
    {
        name_ = n;
    }

    virtual std::string target() const
    {
        return "";
    }

    void events(logger_events& e)
    {
        events_ = e;
    }

    virtual void on_started(uint64_t seq) override;
    virtual void on_stopped(uint64_t seq) override;
    virtual void on_log(uint64_t seq, logger_base& logger, uint64_t id) override;
    virtual void on_audio_stream_created(uint64_t seq, audio_entry& entry) override;
    virtual void on_audio_stream_init_failed(uint64_t seq, const audio_stream_config& config, const std::string& reason) override;
    virtual void on_audio_stream_faulted(uint64_t seq, audio_entry& entry, const error_info& error) override;
    virtual void on_audio_stream_recovery_started(uint64_t seq, audio_entry& entry) override;
    virtual void on_audio_stream_recovery_attempt(uint64_t seq, audio_entry& entry, int attempt, int max_attempts) override;
    virtual void on_audio_stream_recovered(uint64_t seq, audio_entry& entry) override;
    virtual void on_audio_stream_recovery_failed(uint64_t seq, audio_entry& entry) override;
    virtual void on_ptt_control_created(uint64_t seq, ptt_entry& entry) override;
    virtual void on_ptt_control_init_failed(uint64_t seq, const ptt_control_config& config, const std::string& reason) override;
    virtual void on_ptt_control_faulted(uint64_t seq, ptt_entry& entry, const error_info& error) override;
    virtual void on_ptt_control_recovery_started(uint64_t seq, ptt_entry& entry) override;
    virtual void on_ptt_control_recovery_attempt(uint64_t seq, ptt_entry& entry, int attempt, int max_attempts) override;
    virtual void on_ptt_control_recovered(uint64_t seq, ptt_entry& entry) override;
    virtual void on_ptt_control_recovery_failed(uint64_t seq, ptt_entry& entry) override;
    virtual void on_ptt_activated(uint64_t seq, ptt_entry& entry) override;
    virtual void on_ptt_deactivated(uint64_t seq, ptt_entry& entry) override;
    virtual void on_serial_port_faulted(uint64_t seq, ptt_entry& entry, const error_info& error) override;
    virtual void on_serial_port_recovery_started(uint64_t seq, ptt_entry& entry) override;
    virtual void on_serial_port_recovery_attempt(uint64_t seq, ptt_entry& entry, int attempt, int max_attempts) override;
    virtual void on_serial_port_recovered(uint64_t seq, ptt_entry& entry) override;
    virtual void on_serial_port_recovery_failed(uint64_t seq, ptt_entry& entry) override;
    virtual void on_transport_created(uint64_t seq, data_stream_entry& entry) override;
    virtual void on_transport_init_failed(uint64_t seq, const data_stream_config& config, const std::string& reason) override;
    virtual void on_transport_faulted(uint64_t seq, data_stream_entry& entry, const error_info& error) override;
    virtual void on_transport_recovery_started(uint64_t seq, data_stream_entry& entry) override;
    virtual void on_transport_recovery_attempt(uint64_t seq, data_stream_entry& entry, int attempt, int max_attempts) override;
    virtual void on_transport_recovered(uint64_t seq, data_stream_entry& entry) override;
    virtual void on_transport_recovery_failed(uint64_t seq, data_stream_entry& entry) override;
    virtual void on_client_connected(uint64_t seq, data_stream_entry& entry, const tcp_client_connection& connection) override;
    virtual void on_client_disconnected(uint64_t seq, data_stream_entry& entry, const tcp_client_connection& connection) override;
    virtual void on_data_stream_created(uint64_t seq, data_stream_entry& entry) override;
    virtual void on_data_stream_started(uint64_t seq, data_stream_entry& entry) override;
    virtual void on_data_stream_stopped(uint64_t seq, data_stream_entry& entry) override;
    virtual void on_data_stream_enabled(uint64_t seq, data_stream_entry& entry) override;
    virtual void on_data_stream_disabled(uint64_t seq, data_stream_entry& entry) override;
    virtual void on_modem_created(uint64_t seq, modem_entry& entry) override;
    virtual void on_modem_init_failed(uint64_t seq, const modulator_config& config, const std::string& reason) override;
    virtual void on_modem_initialized(uint64_t seq, modem_entry& entry) override;
    virtual void on_modem_transmit(uint64_t seq, const packet& p, uint64_t id) override;
    virtual void on_modem_transmit(uint64_t seq, modem_entry& entry, const std::vector<uint8_t>& bitstream, uint64_t id) override;
    virtual void on_modem_before_start_render_audio(uint64_t seq, audio_entry& entry, uint64_t id) override;
    virtual void on_modem_end_render_audio(uint64_t seq, audio_entry& entry, const std::vector<double>& samples, size_t count, uint64_t id) override;
    virtual void on_modem_ptt(uint64_t seq, bool state, uint64_t id) override;
    virtual void on_volume_changed(uint64_t seq, audio_entry& entry, int previous_volume, int new_volume, uint64_t id) override;
    virtual void on_begin_packet_received(modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) override;
    virtual void on_packet_received(uint64_t seq, modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) override;
    virtual void on_packet_transmit_started(uint64_t seq, modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) override;
    virtual void on_packet_transmit_completed(uint64_t seq, modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) override;

protected:
    void notify_logged(uint64_t id);

private:
    std::string name_;
    std::optional<std::reference_wrapper<logger_events>> events_;
};

class bitstream_file_logger : public logger_base
{
public:
    explicit bitstream_file_logger(file_write_policy& policy);
    ~bitstream_file_logger() override;

    std::string target() const override;

    void on_modem_transmit(uint64_t seq, modem_entry& entry, const std::vector<uint8_t>& bitstream, uint64_t id) override;

private:
    void write_bitstream(const std::vector<uint8_t>& bitstream);

    file_write_policy& policy_;
    std::string last_written_path_;
    std::mutex mutex_;
};

class packet_file_logger : public logger_base
{
public:
    explicit packet_file_logger(file_write_policy& policy);
    ~packet_file_logger() override;

    std::string target() const override;

    void on_packet_transmit_started(uint64_t seq, modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t id) override;

private:
    void write_packet(const packet& p);

    file_write_policy& policy_;
    std::string last_written_path_;
    std::mutex mutex_;
};

class audio_file_logger : public logger_base
{
public:
    explicit audio_file_logger(file_write_policy& policy);
    ~audio_file_logger() override;

    std::string target() const override;

    void on_modem_end_render_audio(uint64_t seq, audio_entry& entry, const std::vector<double>& samples, size_t count, uint64_t id) override;

private:
    void write_audio(const std::vector<double>& samples, size_t count, int sample_rate);

    file_write_policy& policy_;
    std::string last_written_path_;
    std::mutex mutex_;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// pipeline                                                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct pipeline_impl;

class pipeline : public error_events, public logger_events
{
public:
    pipeline(const config& c);
    ~pipeline();

    void init();
    void start();
    void stop();
    void wait_stopped();

    void on_events(pipeline_events& e);
    pipeline_events& on_events();

    void add_logger(logger_base& logger);
    void remove_logger(logger_base& logger);
    const std::vector<std::reference_wrapper<logger_base>>& loggers() const;

private:
    void populate_audio_entries();
    void populate_ptt_controls();
    void populate_transmit_modems();
    void populate_receive_modems();
    void populate_data_streams();
    void populate_loggers();

    void assign_audio_streams();
    void assign_modems();
    void assign_ptt_controls();
    void assign_loggers();

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
    bool try_create_ptt_control(ptt_entry& entry, const ptt_control_config& config);
    bool can_add_modem(const modulator_config& modulator_config);
    bool is_duplicate_modem_name(const std::string& name);
    void register_modem(const modem_entry& entry);
    bool is_audio_stream_referenced(const std::string& name);
    bool is_ptt_control_referenced(const std::string& name);
    bool can_add_data_stream(const data_stream_config& data_config);
    bool is_duplicate_data_stream_name(const std::string& name);
    bool is_duplicate_tcp_port(int port);
    bool is_valid_data_stream_config(const data_stream_config& data_config);
    void register_data_stream(const data_stream_entry& entry, const data_stream_config& data_config);
    bool is_data_stream_referenced(const std::string& name);
    void validate_entries();

    bool can_add_logger(const logger_config& logger_config);
    bool is_duplicate_logger_name(const std::string& name);
    bool is_duplicate_logger_tcp_port(int port);
    bool is_valid_logger_config(const logger_config& logger_config);
    void register_logger(const logger_entry& entry, const logger_config& logger_config);
    std::optional<std::reference_wrapper<logger_entry>> find_logger_entry(const std::string& name);
    std::optional<std::reference_wrapper<modem_entry>> find_modem_entry(const std::string& name);

    bool try_recover_audio_stream(audio_entry& entry, modem_entry& modem_entry);
    void schedule_audio_recovery(audio_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry);
    void attempt_audio_recovery(audio_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry);
    bool try_recover_ptt_control(ptt_entry& entry, modem_entry& modem_entry);
    void schedule_ptt_recovery(ptt_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry);
    void attempt_ptt_recovery(ptt_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry);
    bool try_recover_serial_port(ptt_entry& entry, modem_entry& modem_entry);
    void schedule_serial_port_recovery(ptt_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry);
    void attempt_serial_port_recovery(ptt_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry);
    bool try_recover_transport(data_stream_entry& entry);
    void schedule_transport_recovery(data_stream_entry& entry);
    void attempt_transport_recovery(data_stream_entry& entry);
    void try_reenable_data_stream(modem_entry& modem_entry, data_stream_entry& ds_entry);

    void on_error(audio_stream_no_throw& component, audio_entry& entry, const error_info& error) override;
    void on_error(ptt_control_no_throw& component, ptt_entry& entry, const error_info& error) override;
    void on_error(serial_port_no_throw& component, ptt_entry& entry, const error_info& error) override;
    void on_error(transport_no_throw& component, data_stream_entry& entry, const error_info& error) override;

    void on_log(logger_base& logger, uint64_t id) override;

    std::pair<int, int> adjust_audio_volume(modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p, uint64_t global_id);

    template<typename Func>
    void invoke_async(Func&& fn);

    template<typename Func>
    void invoke_loggers_async(Func&& fn);

    template<typename Func>
    void invoke_modem_loggers_async(modem_entry& modem, Func&& fn);

    template<typename Func>
    void invoke_audio_entry_loggers_async(audio_entry& entry, Func&& fn);

    template<typename Func>
    void invoke_ptt_entry_loggers_async(ptt_entry& entry, Func&& fn);

    template<typename Func>
    void invoke_data_stream_loggers_async(data_stream_entry& entry, Func&& fn);

    void invoke_ptt_event_async(bool enabled, ptt_entry& entry);

    uint64_t next_seq();

    const struct config& config;
    std::optional<std::reference_wrapper<pipeline_events>> events_;
    std::vector<std::reference_wrapper<logger_base>> loggers_;
    std::vector<std::unique_ptr<logger_entry>> owned_loggers_;
    std::vector<std::unique_ptr<audio_entry>> audio_entries_;
    std::vector<std::unique_ptr<ptt_entry>> ptt_controls_;
    std::vector<std::unique_ptr<modem_entry>> modems_;
    std::vector<std::unique_ptr<data_stream_entry>> data_streams_;

    std::set<std::string> used_audio_names_;
    std::set<std::pair<std::string, audio_device_type>> used_audio_devices_;
    std::set<std::string> used_audio_files_;
    std::set<std::string> used_serial_ports_;
    std::set<std::string> used_library_files_;
    std::set<std::string> used_ptt_names_;
    std::set<std::string> used_modem_names_;
    std::set<std::string> used_data_stream_names_;
    std::set<int> used_tcp_ports_;
    std::set<std::string> used_logger_names_;
    std::set<int> used_logger_tcp_ports_;

    std::unique_ptr<pipeline_impl> impl_;
    std::atomic<uint64_t> event_sequence_{ 0 };
};

LIBMODEM_NAMESPACE_END