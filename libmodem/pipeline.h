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

class audio_stream_no_throw;
class ptt_control_no_throw;
class transport_no_throw;
class serial_port_no_throw;

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
    std::unique_ptr<modulator_base> modulator;
    struct modulator_config modulator_config;
    std::optional<std::reference_wrapper<data_stream_entry>> associated_data_stream_entry;
    std::optional<std::reference_wrapper<audio_entry>> associated_audio_entry;
    std::optional<std::reference_wrapper<ptt_entry>> associated_ptt_entry;
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
// pipeline_events                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct pipeline_events
{
    virtual ~pipeline_events() = default;
    virtual void started() = 0;
    virtual void stopped() = 0;
    virtual void on_audio_stream_created(audio_entry& entry) = 0;
    virtual void on_audio_stream_init_failed(const audio_stream_config& config, const std::string& reason) = 0;
    virtual void on_audio_stream_faulted(audio_entry& entry, const error_info& error) = 0;
    virtual void on_audio_stream_recovery_started(audio_entry& entry) = 0;
    virtual void on_audio_stream_recovery_attempt(audio_entry& entry, int attempt, int max_attempts) = 0;
    virtual void on_audio_stream_recovered(audio_entry& entry) = 0;
    virtual void on_audio_stream_recovery_failed(audio_entry& entry) = 0;
    virtual void on_ptt_control_created(ptt_entry& entry) = 0;
    virtual void on_ptt_control_init_failed(const ptt_control_config& config, const std::string& reason) = 0;
    virtual void on_ptt_control_faulted(ptt_entry& entry, const error_info& error) = 0;
    virtual void on_ptt_control_recovery_started(ptt_entry& entry) = 0;
    virtual void on_ptt_control_recovery_attempt(ptt_entry& entry, int attempt, int max_attempts) = 0;
    virtual void on_ptt_control_recovered(ptt_entry& entry) = 0;
    virtual void on_ptt_control_recovery_failed(ptt_entry& entry) = 0;
    virtual void on_ptt_activated(ptt_entry& entry) = 0;
    virtual void on_ptt_deactivated(ptt_entry& entry) = 0;
    virtual void on_serial_port_faulted(ptt_entry& entry, const error_info& error) = 0;
    virtual void on_serial_port_recovery_started(ptt_entry& entry) = 0;
    virtual void on_serial_port_recovery_attempt(ptt_entry& entry, int attempt, int max_attempts) = 0;
    virtual void on_serial_port_recovered(ptt_entry& entry) = 0;
    virtual void on_serial_port_recovery_failed(ptt_entry& entry) = 0;
    virtual void on_transport_created(data_stream_entry& entry) = 0;
    virtual void on_transport_init_failed(const data_stream_config& config, const std::string& reason) = 0;
    virtual void on_transport_faulted(data_stream_entry& entry, const error_info& error) = 0;
    virtual void on_transport_recovery_started(data_stream_entry& entry) = 0;
    virtual void on_transport_recovery_attempt(data_stream_entry& entry, int attempt, int max_attempts) = 0;
    virtual void on_transport_recovered(data_stream_entry& entry) = 0;
    virtual void on_transport_recovery_failed(data_stream_entry& entry) = 0;
    virtual void on_client_connected(data_stream_entry& entry, const tcp_client_connection& connection) = 0;
    virtual void on_client_disconnected(data_stream_entry& entry, const tcp_client_connection& connection) = 0;
    virtual void on_data_stream_created(data_stream_entry& entry) = 0;
    virtual void on_data_stream_started(data_stream_entry& entry) = 0;
    virtual void on_data_stream_stopped(data_stream_entry& entry) = 0;
    virtual void on_data_stream_enabled(data_stream_entry& entry) = 0;
    virtual void on_data_stream_disabled(data_stream_entry& entry) = 0;
    virtual void on_modem_created(modem_entry& entry) = 0;
    virtual void on_modem_init_failed(const modulator_config& config, const std::string& reason) = 0;
    virtual void on_modem_initialized(modem_entry& entry) = 0;
    virtual void on_modem_transmit(const packet& p, uint64_t id) = 0;
    virtual void on_modem_transmit(const std::vector<uint8_t>& bitstream, uint64_t id) = 0;
    virtual void on_modem_render_audio(const std::vector<double>& samples, size_t count, uint64_t id) = 0;
    virtual void on_modem_ptt(bool state, uint64_t id) = 0;
    virtual void on_volume_changed(audio_entry& entry, int previous_volume, int new_volume) = 0;
    virtual void on_packet_received(modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p) = 0;
    virtual void on_packet_transmit_started(modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p) = 0;
    virtual void on_packet_transmit_completed(modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p) = 0;
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
    void started() override;
    void stopped() override;
    void on_audio_stream_created(audio_entry& entry) override;
    void on_audio_stream_init_failed(const audio_stream_config& config, const std::string& reason) override;
    void on_audio_stream_faulted(audio_entry& entry, const error_info& error) override;
    void on_audio_stream_recovery_started(audio_entry& entry) override;
    void on_audio_stream_recovery_attempt(audio_entry& entry, int attempt, int max_attempts) override;
    void on_audio_stream_recovered(audio_entry& entry) override;
    void on_audio_stream_recovery_failed(audio_entry& entry) override;
    void on_ptt_control_created(ptt_entry& entry) override;
    void on_ptt_control_init_failed(const ptt_control_config& config, const std::string& reason) override;
    void on_ptt_control_faulted(ptt_entry& entry, const error_info& error) override;
    void on_ptt_control_recovery_started(ptt_entry& entry) override;
    void on_ptt_control_recovery_attempt(ptt_entry& entry, int attempt, int max_attempts) override;
    void on_ptt_control_recovered(ptt_entry& entry) override;
    void on_ptt_control_recovery_failed(ptt_entry& entry) override;
    void on_ptt_activated(ptt_entry& entry) override;
    void on_ptt_deactivated(ptt_entry& entry) override;
    void on_serial_port_faulted(ptt_entry& entry, const error_info& error) override;
    void on_serial_port_recovery_started(ptt_entry& entry) override;
    void on_serial_port_recovery_attempt(ptt_entry& entry, int attempt, int max_attempts) override;
    void on_serial_port_recovered(ptt_entry& entry) override;
    void on_serial_port_recovery_failed(ptt_entry& entry) override;
    void on_transport_created(data_stream_entry& entry) override;
    void on_transport_init_failed(const data_stream_config& config, const std::string& reason) override;
    void on_transport_faulted(data_stream_entry& entry, const error_info& error) override;
    void on_transport_recovery_started(data_stream_entry& entry) override;
    void on_transport_recovery_attempt(data_stream_entry& entry, int attempt, int max_attempts) override;
    void on_transport_recovered(data_stream_entry& entry) override;
    void on_transport_recovery_failed(data_stream_entry& entry) override;
    void on_client_connected(data_stream_entry& entry, const tcp_client_connection& connection) override;
    void on_client_disconnected(data_stream_entry& entry, const tcp_client_connection& connection) override;
    void on_data_stream_created(data_stream_entry& entry) override;
    void on_data_stream_started(data_stream_entry& entry) override;
    void on_data_stream_stopped(data_stream_entry& entry) override;
    void on_data_stream_enabled(data_stream_entry& entry) override;
    void on_data_stream_disabled(data_stream_entry& entry) override;
    void on_modem_created(modem_entry& entry) override;
    void on_modem_init_failed(const modulator_config& config, const std::string& reason) override;
    void on_modem_initialized(modem_entry& entry) override;
    void on_modem_transmit(const packet& p, uint64_t id) override;
    void on_modem_transmit(const std::vector<uint8_t>& bitstream, uint64_t id) override;
    void on_modem_render_audio(const std::vector<double>& samples, size_t count, uint64_t id) override;
    void on_modem_ptt(bool state, uint64_t id) override;
    void on_volume_changed(audio_entry& entry, int previous_volume, int new_volume) override;
    void on_packet_received(modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p) override;
    void on_packet_transmit_started(modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p) override;
    void on_packet_transmit_completed(modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p) override;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// pipeline                                                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct pipeline_impl;

class pipeline : public error_events, public modem_events
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

private:
    void populate_audio_entries();
    void populate_ptt_controls();
    void populate_transmit_modems();
    void populate_receive_modems();
    void populate_data_streams();

    void assign_audio_streams();
    void assign_modems();
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
    bool try_create_ptt_control(ptt_entry& entry, const ptt_control_config& config);
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
    void validate_entries();

    bool try_recover_audio_stream(audio_entry& entry, modem_entry& modem);
    void schedule_audio_recovery(audio_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry);
    void attempt_audio_recovery(audio_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry);

    bool try_recover_ptt_control(ptt_entry& entry, modem_entry& modem);
    void schedule_ptt_recovery(ptt_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry);
    void attempt_ptt_recovery(ptt_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry);

    bool try_recover_serial_port(ptt_entry& entry, modem_entry& modem);
    void schedule_serial_port_recovery(ptt_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry);
    void attempt_serial_port_recovery(ptt_entry& entry, modem_entry& modem_entry, data_stream_entry& ds_entry);

    bool try_recover_transport(data_stream_entry& entry);
    void schedule_transport_recovery(data_stream_entry& ds_entry);
    void attempt_transport_recovery(data_stream_entry& ds_entry);

    void try_reenable_data_stream(modem_entry& modem_entry, data_stream_entry& ds_entry);

    void invoke_ptt_event_async(bool enabled, ptt_entry& entry);

    void on_transmit_starting(modem_entry& modem_entry, data_stream_entry& ds_entry, const packet& p);

    template<typename Func>
    void invoke_async(Func&& fn);

    void on_error(audio_stream_no_throw& component, audio_entry& entry, const error_info& error) override;
    void on_error(ptt_control_no_throw& component, ptt_entry& entry, const error_info& error) override;
    void on_error(serial_port_no_throw& component, ptt_entry& entry, const error_info& error) override;
    void on_error(transport_no_throw& component, data_stream_entry& entry, const error_info& error) override;

    void transmit(const packet& packet, uint64_t id) override;
    void receive(const packet& packet, uint64_t id) override;
    void transmit(const std::vector<uint8_t>& bitstream, uint64_t id) override;
    void receive(const std::vector<uint8_t>& bitstream, uint64_t id) override;
    void ptt(bool state, uint64_t id) override;
    void data_carrier_detected(uint64_t id) override;
    void render_audio(const std::vector<double>& samples, size_t count, uint64_t id) override;
    void capture_audio(const std::vector<double>& samples, uint64_t id) override;

    const struct config config;
    std::vector<std::unique_ptr<modem_entry>> modems_;
    std::vector<std::unique_ptr<ptt_entry>> ptt_controls_;
    std::vector<std::unique_ptr<audio_entry>> audio_entries_;
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
    std::unique_ptr<pipeline_impl> impl_;
    std::optional<std::reference_wrapper<pipeline_events>> events_;
};

LIBMODEM_NAMESPACE_END