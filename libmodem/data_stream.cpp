// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// data_stream.cpp
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

#include "data_stream.h"

#include <boost/circular_buffer.hpp>

LIBMODEM_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// transport                                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

void transport::write(std::size_t client_id, const std::vector<uint8_t>& data)
{
    (void)client_id;
    write(data);
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_transport                                                    //
//                                                                  //
//                                                                  //
// **************************************************************** //

tcp_transport::tcp_transport()
{
}

tcp_transport::tcp_transport(const std::string& hostname, int port) : hostname_(hostname), port_(port)
{
}

tcp_transport::~tcp_transport()
{
    stop();
}

void tcp_transport::on_data_received(const tcp_client_connection& connection, const std::vector<uint8_t>& data)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& buffer = client_buffers_[connection.id];
        buffer.insert(buffer.end(), data.begin(), data.end());
        client_ids_.push_back(connection.id);
    }
    cv_.notify_one();
}

void tcp_transport::on_client_connected(const tcp_client_connection& connection)
{
    if (on_client_connected_callable_)
    {
        on_client_connected_callable_->invoke(connection);
    }
}

void tcp_transport::on_client_disconnected(const tcp_client_connection& connection)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        client_buffers_.erase(connection.id);
        client_ids_.erase(std::remove(client_ids_.begin(), client_ids_.end(), connection.id), client_ids_.end());
    }
    if (on_client_disconnected_callable_)
    {
        on_client_disconnected_callable_->invoke(connection);
    }
}

void tcp_transport::start()
{
    tcp_server_base::start(hostname_, port_);
}

void tcp_transport::stop()
{
    tcp_server_base::stop();
}

void tcp_transport::write(const std::vector<uint8_t>& data)
{
    tcp_server_base::broadcast(data);
}

void tcp_transport::write(std::size_t client_id, const std::vector<uint8_t>& data)
{
    tcp_client_connection conn;
    conn.id = client_id;
    tcp_server_base::send(conn, std::vector<uint8_t>(data));
}

size_t tcp_transport::read(std::size_t client_id, std::vector<uint8_t>& data, size_t size)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = client_buffers_.find(client_id);
    if (it == client_buffers_.end())
    {
        return 0;
    }

    auto& buffer = it->second;
    std::size_t to_read = std::min(size, buffer.size());
    if (to_read == 0)
    {
        return 0;
    }

    data.assign(buffer.begin(), buffer.begin() + to_read);
    buffer.erase(buffer.begin(), buffer.begin() + to_read);
    return to_read;
}

std::vector<std::size_t> tcp_transport::clients()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return client_ids_;
}

void tcp_transport::flush()
{
    tcp_server_base::flush();
}

bool tcp_transport::wait_data_received(int timeout_ms)
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (timeout_ms < 0)
    {
        cv_.wait(lock, [this]() { return std::any_of(client_buffers_.begin(), client_buffers_.end(), [](const auto& pair) { return !pair.second.empty(); }); });
        return true;
    }
    else
    {
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]() { return std::any_of(client_buffers_.begin(), client_buffers_.end(), [](const auto& pair) { return !pair.second.empty(); }); });
    }
}

void tcp_transport::enabled(bool enable)
{
    enabled_ = enable;
}

bool tcp_transport::enabled()
{
    return enabled_;
}

tcp_server_base& tcp_transport::server()
{
    return *this;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// serial_transport                                                 //
//                                                                  //
//                                                                  //
// **************************************************************** //

void serial_transport::start()
{
}

void serial_transport::stop()
{
}

void serial_transport::write(const std::vector<uint8_t>& data)
{
    port_.write(data);
}

size_t serial_transport::read(std::size_t client_id, std::vector<uint8_t>& data, size_t size)
{
    (void)client_id;
    data = port_.read(size);
    return data.size();
}

std::vector<std::size_t> serial_transport::clients()
{
    return { 0 };
}

void serial_transport::flush()
{
}

bool serial_transport::wait_data_received(int timeout_ms)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (timeout_ms < 0 || std::chrono::steady_clock::now() < deadline)
    {
        if (port_.bytes_available() > 0)
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

void serial_transport::enabled(bool enable)
{
    enabled_ = enable;
}

bool serial_transport::enabled()
{
    return enabled_;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// data_stream                                                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

data_stream::data_stream()
{
    read_buffer_.resize(4096);
}

data_stream::~data_stream()
{
    stop();
}

void data_stream::transport(struct transport& t)
{
    transport_ = t;
}

void data_stream::formatter(struct formatter& f)
{
    formatter_ = f;
}

void data_stream::start()
{
    transport_->get().start();
}

void data_stream::stop()
{
}

void data_stream::send(packet p)
{
    if (!transport_->get().enabled())
    {
        return;
    }
    for (auto& [client_id, client_formatter] : client_formatters_)
    {
        auto bytes = client_formatter->encode(p);
        if (!bytes.empty())
        {
            transport_->get().write(client_id, bytes);
        }
    }
}

bool data_stream::try_receive(packet& p)
{
    received_data d;
    if (!try_receive(d))
    {
        return false;
    }
    p = std::move(d.p);
    return true;
}

bool data_stream::try_receive(received_data& d)
{
    if (!transport_->get().enabled())
    {
        return false;
    }

    const auto& client_ids = transport_->get().clients();

    for (std::size_t client_id : client_ids)
    {
        auto& client_formatter = client_formatters_[client_id];
        if (!client_formatter)
        {
            client_formatter = formatter_->get().clone();
        }

        std::size_t bytes_read = transport_->get().read(client_id, read_buffer_, read_buffer_.size());
        if (bytes_read == 0)
        {
            continue;
        }

        decode_result result;
        if (client_formatter->try_decode(read_buffer_, bytes_read, result))
        {
            if (!result.response_bytes.empty())
            {
                transport_->get().write(client_id, result.response_bytes);
            }

            if (result.decoded_packet)
            {
                d.p = std::move(*result.decoded_packet);
                d.port = result.port;
                return true;
            }
        }
    }

    // Cleanup disconnected clients - drain pending first
    const auto& current_clients = transport_->get().clients();
    for (auto it = client_formatters_.begin(); it != client_formatters_.end(); )
    {
        if (std::find(current_clients.begin(), current_clients.end(), it->first) == current_clients.end())
        {
            decode_result result;
            if (it->second->try_decode({}, 0, result))
            {
                if (!result.response_bytes.empty())
                {
                    transport_->get().write(it->first, result.response_bytes);
                }

                if (result.decoded_packet)
                {
                    d.p = std::move(*result.decoded_packet);
                    d.port = result.port;
                    return true;
                }
            }
            it = client_formatters_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    return false;
}

bool data_stream::wait_data_received(int timeout_ms)
{
    if (!transport_->get().enabled())
    {
        return false;
    }
    return transport_->get().wait_data_received(timeout_ms);
}

bool data_stream::wait_stopped(int timeout_ms)
{
    (void)timeout_ms;
    return false;
}

void data_stream::enabled(bool enable)
{
    enabled_.store(enable);
}

bool data_stream::enabled()
{
    return enabled_.load();
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// modem_data_stream_impl                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct modem_data_stream_impl
{
    boost::circular_buffer<std::tuple<libmodem::packet, uint64_t, uint64_t>> packets;  // packet, packet_id, receive_id
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// modem_data_stream                                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

modem_data_stream::modem_data_stream() : impl_(std::make_unique<modem_data_stream_impl>())
{
    impl_->packets.set_capacity(100);
}

modem_data_stream::~modem_data_stream()
{
    stop();
}

void modem_data_stream::add_modem(struct modem& m)
{
    modems_.push_back(m);
}

void modem_data_stream::modem(struct modem& m)
{
    modems_.clear();
    modems_.push_back(m);
}

void modem_data_stream::start()
{
    if (modems_.empty())
    {
        throw std::runtime_error("modem_data_stream: no modems set");
    }

    if (running_.exchange(true))
    {
        return;
    }

    data_stream::start();

    receive_thread_ = std::jthread([this](std::stop_token stop_token) {
        receive_callback(stop_token);
    });
}

void modem_data_stream::stop()
{
    if (!running_.exchange(false))
    {
        return;
    }

    if (receive_thread_.joinable())
    {
        receive_thread_.request_stop();
        enabled_cv_.notify_all();
        receive_thread_.join();
    }

    stop_cv_.notify_all();
}

void modem_data_stream::enabled(bool enable)
{
    data_stream::enabled(enable);
    if (enable)
    {
        enabled_cv_.notify_all();
    }
}

size_t modem_data_stream::audio_stream_error_count(size_t count)
{
    return audio_stream_error_count_.exchange(count);
}

size_t modem_data_stream::audio_stream_error_count()
{
    return audio_stream_error_count_.load();
}

bool modem_data_stream::wait_transmit_idle(int timeout_ms)
{
    std::unique_lock<std::mutex> lock(transmit_mutex_);

    if (!transmitting_.load())
    {
        return true;
    }

    if (timeout_ms < 0)
    {
        transmit_cv_.wait(lock, [this]() { return !transmitting_.load(); });
        return true;
    }
    else
    {
        return transmit_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]() { return !transmitting_.load(); });
    }
}

void modem_data_stream::receive_callback(std::stop_token stop_token)
{
    while (!stop_token.stop_requested())
    {
        received_data d;
        if (try_receive(d))
        {
            uint64_t packet_id = next_packet_id_.fetch_add(1);
            uint64_t receive_id = next_receive_id_.fetch_add(1);

            impl_->packets.push_back({ d.p, packet_id, receive_id });

            if (on_packet_received_)
            {
                on_packet_received_(d.p, packet_id, receive_id);
            }
        }

        if (enabled())
        {
            if (!impl_->packets.empty())
            {
                std::lock_guard<std::mutex> lock(transmit_mutex_);
                transmitting_.store(true);
            }

            while (!impl_->packets.empty() && !stop_token.stop_requested())
            {
                try
                {
                    auto [p, packet_id, receive_id] = impl_->packets.front();
                    current_packet_id_ = packet_id;
                    current_receive_id_ = receive_id;

                    for (auto& modem_ref : modems_)
                    {
                        uint64_t transmit_id = next_transmit_id_.fetch_add(1);
                        current_transmit_id_ = transmit_id;
                        current_modem_ = &modem_ref.get();

                        modem_ref.get().on_events(*this);

                        if (on_transmit_started_)
                        {
                            on_transmit_started_(*current_modem_, p, packet_id, receive_id, transmit_id);
                        }

                        modem_ref.get().transmit(p);

                        if (on_transmit_completed_)
                        {
                            on_transmit_completed_(*current_modem_, p, packet_id, receive_id, transmit_id);
                        }

                        current_modem_ = nullptr;
                    }

                    impl_->packets.pop_front();
                    current_packet_id_ = 0;
                    current_receive_id_ = 0;
                    current_transmit_id_ = 0;
                }
                catch (...)
                {
                    current_packet_id_ = 0;
                    current_receive_id_ = 0;
                    current_transmit_id_ = 0;
                    enabled(false);
                    break;
                }
            }

            {
                std::lock_guard<std::mutex> lock(transmit_mutex_);
                transmitting_.store(false);
            }
            transmit_cv_.notify_all();
        }

        wait_data_received(10);
    }
}

bool modem_data_stream::wait_stopped(int timeout_ms)
{
    std::unique_lock<std::mutex> lock(stop_mutex_);

    if (!running_.load())
    {
        return true;
    }

    if (timeout_ms < 0)
    {
        stop_cv_.wait(lock, [this]() { return !running_.load(); });
        return true;
    }
    else
    {
        return stop_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]() { return !running_.load(); });
    }
}

void modem_data_stream::transmit(const packet& p, uint64_t id)
{
    (void)id;

    if (on_modem_transmit_packet_ && current_modem_)
    {
        on_modem_transmit_packet_(*current_modem_, p, current_packet_id_, current_receive_id_, current_transmit_id_);
    }
}

void modem_data_stream::receive(const packet& p, uint64_t id)
{
    (void)p;
    (void)id;
}

void modem_data_stream::transmit(const std::vector<uint8_t>& bitstream, uint64_t id)
{
    (void)id;

    if (on_modem_transmit_bitstream_ && current_modem_)
    {
        on_modem_transmit_bitstream_(*current_modem_, bitstream, current_packet_id_, current_receive_id_, current_transmit_id_);
    }
}

void modem_data_stream::receive(const std::vector<uint8_t>& bitstream, uint64_t id)
{
    (void)bitstream;
    (void)id;
}

void modem_data_stream::ptt(bool state, uint64_t id)
{
    (void)id;

    if (on_modem_ptt_ && current_modem_)
    {
        on_modem_ptt_(*current_modem_, state, current_packet_id_, current_receive_id_, current_transmit_id_);
    }
}

void modem_data_stream::data_carrier_detected(uint64_t id)
{
    (void)id;
}

void modem_data_stream::before_start_render_audio(uint64_t id)
{
    (void)id;

    if (on_modem_before_start_render_audio_ && current_modem_)
    {
        on_modem_before_start_render_audio_(*current_modem_, current_packet_id_, current_receive_id_, current_transmit_id_);
    }
}

void modem_data_stream::end_render_audio(const std::vector<double>& samples, size_t count, uint64_t id)
{
    (void)id;

    if (on_modem_end_render_audio_ && current_modem_)
    {
        on_modem_end_render_audio_(*current_modem_, samples, count, current_packet_id_, current_receive_id_, current_transmit_id_);
    }
}

void modem_data_stream::capture_audio(const std::vector<double>& samples, uint64_t id)
{
    (void)samples;
    (void)id;
}

void modem_data_stream::modulate(const std::vector<uint8_t>& bitstream, const std::vector<double>& audio_buffer, uint64_t id)
{
    (void)bitstream;
    (void)audio_buffer;
    (void)id;
}

LIBMODEM_NAMESPACE_END