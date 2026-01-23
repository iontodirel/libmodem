// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// data_source.cpp
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

#include "data_source.h"

LIBMODEM_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_transport                                                    //
//                                                                  //
//                                                                  //
// **************************************************************** //

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

void tcp_transport::on_client_disconnected(const tcp_client_connection& connection)
{
    std::lock_guard<std::mutex> lock(mutex_);
    client_buffers_.erase(connection.id);
    client_ids_.erase(std::remove(client_ids_.begin(), client_ids_.end(), connection.id), client_ids_.end());
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
    (void)timeout_ms;
    return true;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// formatter                                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

void formatter::invoke_on_command(const kiss::frame& frame)
{
    if (on_command_callable_)
    {
        on_command_callable_->invoke(frame);
    }
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// ax25_kiss_formatter                                              //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> ax25_kiss_formatter::encode(packet p)
{
    std::vector<uint8_t> kiss_bytes;
    std::vector<uint8_t> ax25_frame_bytes = ax25::encode_frame(p);
    kiss::encode(0, ax25_frame_bytes.begin(), ax25_frame_bytes.end() - 2, std::back_inserter(kiss_bytes));
    return kiss_bytes;
}

bool ax25_kiss_formatter::try_decode(const std::vector<uint8_t>& data, packet& p)
{
    kiss::frame frame;
    if (try_decode(data, frame))
    {
        if (frame.command_byte == 0)
        {
            return ax25::try_decode_frame_no_fcs(frame.data, p);
        }
        else
        {
            invoke_on_command(frame);
            return false;
        }
    }
    return false;
}

bool ax25_kiss_formatter::try_decode(const std::vector<uint8_t>& data, kiss::frame& p)
{
    if (!data.empty())
    {
        kiss_decoder_.decode(data.begin(), data.end());
        for (const auto& frame : kiss_decoder_.frames())
        {
            pending_frames_.push(frame);
        }
        kiss_decoder_.clear();
    }

    if (pending_frames_.empty())
    {
        return false;
    }

    p = std::move(pending_frames_.front());
    pending_frames_.pop();

    return true;
}

std::unique_ptr<formatter> ax25_kiss_formatter::clone() const
{
    return std::make_unique<ax25_kiss_formatter>(*this);
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// data_source                                                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

void data_source::transport(struct transport& t)
{
    transport_ = t;
}

void data_source::formatter(struct formatter& f)
{
    formatter_ = f;
}

void data_source::send(packet p)
{
    std::vector<uint8_t> data = formatter_.value().get().encode(p);
    transport_.value().get().write(data);
}

bool data_source::try_receive(packet& p)
{
    const auto& client_ids = transport_->get().clients();

    for (std::size_t client_id : client_ids)
    {
        auto& client_formatter = client_formatters_[client_id];
        if (!client_formatter)
        {
            client_formatter = formatter_->get().clone();
        }

        std::vector<uint8_t> read_buffer;
        read_buffer.resize(4096);
        std::size_t bytes_read = transport_->get().read(client_id, read_buffer, read_buffer.size());
        if (bytes_read == 0)
        {
            continue;
        }
        read_buffer.resize(bytes_read);

        if (client_formatter->try_decode(read_buffer, p))
        {
            return true;
        }
    }

    // Cleanup disconnected clients - drain pending first
    const auto& current_clients = transport_->get().clients();
    for (auto it = client_formatters_.begin(); it != client_formatters_.end(); )
    {
        if (std::find(current_clients.begin(), current_clients.end(), it->first) == current_clients.end())
        {
            if (it->second->try_decode({}, p))
            {
                return true;  // Cleanup continues next call
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

bool data_source::wait_data_received(int timeout_ms)
{
    (void)timeout_ms;
    return false;
}
LIBMODEM_NAMESPACE_END
