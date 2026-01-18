// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// io.cpp
//
// MIT License
//
// Copyright (c) 2025 Ion Todirel
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

#include "io.h"

#include <cassert>

#ifdef WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#endif // WIN32

#if __linux__
#include <sys/ioctl.h>
#include <termios.h>
#include <dlfcn.h>
#endif // __linux__

#include <boost/asio.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/beast/core/detail/base64.hpp>

#include <nlohmann/json.hpp>

LIBMODEM_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// base64                                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::string base64_encode(const std::vector<uint8_t>& data)
{
    if (data.empty())
    {
        return "";
    }

    std::string result;
    result.resize(boost::beast::detail::base64::encoded_size(data.size()));
    result.resize(boost::beast::detail::base64::encode(result.data(), data.data(), data.size()));
    return result;
}

std::string base64_encode(const std::string& data)
{
    if (data.empty())
    {
        return "";
    }

    std::string result;
    result.resize(boost::beast::detail::base64::encoded_size(data.size()));
    result.resize(boost::beast::detail::base64::encode(result.data(), data.data(), data.size()));
    return result;
}

std::vector<uint8_t> base64_decode(const std::string& encoded)
{
    if (encoded.empty())
    {
        return {};
    }

    std::vector<uint8_t> result;
    result.resize(boost::beast::detail::base64::decoded_size(encoded.size()));
    auto decoded = boost::beast::detail::base64::decode(result.data(), encoded.data(), encoded.size());
    result.resize(decoded.first);
    return result;
}

std::string base64_decode_string(const std::string& encoded)
{
    if (encoded.empty())
    {
        return "";
    }

    std::string result;
    result.resize(boost::beast::detail::base64::decoded_size(encoded.size()));
    auto decoded = boost::beast::detail::base64::decode(result.data(), encoded.data(), encoded.size());
    result.resize(decoded.first);
    return result;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// serial_port_impl                                                 //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct serial_port_impl
{
    boost::asio::io_context io_context;
    boost::asio::serial_port serial_port{ io_context };
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// serial_port                                                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

serial_port::serial_port() : impl_(std::make_unique<serial_port_impl>()), is_open_(false)
{
}

serial_port::serial_port(serial_port&&) noexcept = default;
serial_port& serial_port::operator=(serial_port&&) noexcept = default;

serial_port::~serial_port()
{
    close();
}

bool serial_port::open(const std::string& port_name, unsigned int baud_rate, unsigned int data_bits, parity parity, stop_bits stop_bits, flow_control flow_control)
{
    if (is_open_)
    {
        throw std::runtime_error("Serial port is already open");
    }

    try
    {
        // Open the serial port
        impl_->serial_port.open(port_name);

        // Configure serial port options
        impl_->serial_port.set_option(boost::asio::serial_port_base::baud_rate(baud_rate));
        impl_->serial_port.set_option(boost::asio::serial_port_base::character_size(data_bits));

        // Convert parity enum
        boost::asio::serial_port_base::parity::type asio_parity;

        switch (parity)
        {
        case parity::none:
            asio_parity = boost::asio::serial_port_base::parity::none;
            break;
        case parity::odd:
            asio_parity = boost::asio::serial_port_base::parity::odd;
            break;
        case parity::even:
            asio_parity = boost::asio::serial_port_base::parity::even;
            break;
        default:
            asio_parity = boost::asio::serial_port_base::parity::none;
            break;
        }

        impl_->serial_port.set_option(boost::asio::serial_port_base::parity(asio_parity));

        // Convert stop_bits enum
        boost::asio::serial_port_base::stop_bits::type asio_stop_bits;

        switch (stop_bits)
        {
        case stop_bits::one:
            asio_stop_bits = boost::asio::serial_port_base::stop_bits::one;
            break;
        case stop_bits::onepointfive:
            asio_stop_bits = boost::asio::serial_port_base::stop_bits::onepointfive;
            break;
        case stop_bits::two:
            asio_stop_bits = boost::asio::serial_port_base::stop_bits::two;
            break;
        default:
            asio_stop_bits = boost::asio::serial_port_base::stop_bits::one;
            break;
        }

        impl_->serial_port.set_option(boost::asio::serial_port_base::stop_bits(asio_stop_bits));

        // Convert flow_control enum
        boost::asio::serial_port_base::flow_control::type asio_flow_control;

        switch (flow_control)
        {
        case flow_control::none:
            asio_flow_control = boost::asio::serial_port_base::flow_control::none;
            break;
        case flow_control::software:
            asio_flow_control = boost::asio::serial_port_base::flow_control::software;
            break;
        case flow_control::hardware:
            asio_flow_control = boost::asio::serial_port_base::flow_control::hardware;
            break;
        default:
            asio_flow_control = boost::asio::serial_port_base::flow_control::none;
            break;
        }

        impl_->serial_port.set_option(boost::asio::serial_port_base::flow_control(asio_flow_control));

        is_open_ = true;

        return true;
    }
    catch (const boost::system::system_error&)
    {
        is_open_ = false;
        return false;
    }
}

void serial_port::close()
{
    if (is_open_ && impl_)
    {
        is_open_ = false;
        impl_->serial_port.close();
    }
}

void serial_port::rts(bool enable)
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

    try
    {
#ifdef WIN32
        if (enable)
        {
            ::EscapeCommFunction(impl_->serial_port.native_handle(), SETRTS);
        }
        else
        {
            ::EscapeCommFunction(impl_->serial_port.native_handle(), CLRRTS);
        }
        rts_ = enable;
#endif // WIN32
#if __linux__
        int status;
        ::ioctl(impl_->serial_port.native_handle(), TIOCMGET, &status);
        if (enable)
        {
            status |= TIOCM_RTS;
        }
        else
        {
            status &= ~TIOCM_RTS;
        }
        ::ioctl(impl_->serial_port.native_handle(), TIOCMSET, &status);
#endif // __linux__
    }
    catch (...)
    {
    }
}

bool serial_port::rts()
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

    try
    {
#ifdef WIN32
        // Note: GetCommModemStatus doesn't return RTS state directly
        return rts_;
#endif // WIN32
#ifdef __linux__
        int status;
        ::ioctl(impl_->serial_port.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_RTS) != 0;
#endif // __linux__
    }
    catch (...)
    {
        return false;
    }
}

void serial_port::dtr(bool enable)
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

    try
    {
#ifdef WIN32
        if (enable)
        {
            ::EscapeCommFunction(impl_->serial_port.native_handle(), SETDTR);
        }
        else
        {
            ::EscapeCommFunction(impl_->serial_port.native_handle(), CLRDTR);
        }
        dtr_ = enable;
#endif // WIN32
#ifdef __linux__
        int status;
        ::ioctl(impl_->serial_port.native_handle(), TIOCMGET, &status);
        if (enable)
        {
            status |= TIOCM_DTR;
        }
        else
        {
            status &= ~TIOCM_DTR;
        }
        ::ioctl(impl_->serial_port.native_handle(), TIOCMSET, &status);
#endif // __linux__
    }
    catch (...)
    {
    }
}

bool serial_port::dtr()
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

    try
    {
#ifdef WIN32
        // Note: GetCommModemStatus doesn't return DTR state directly
        return dtr_;
#endif // WIN32
#ifdef __linux__
        int status;
        ::ioctl(impl_->serial_port.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_DTR) != 0;
#endif // __linux__
    }
    catch (...)
    {
        return false;
    }
}

bool serial_port::cts()
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

    try
    {
#ifdef WIN32
        DWORD status;
        ::GetCommModemStatus(impl_->serial_port.native_handle(), &status);
        return (status & MS_CTS_ON) != 0;
#endif // WIN32
#ifdef __linux__
        int status;
        ::ioctl(impl_->serial_port.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_CTS) != 0;
#endif // __linux__
    }
    catch (...)
    {
        return false;
    }
}

bool serial_port::dsr()
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

    try
    {
#ifdef WIN32
        DWORD status;
        ::GetCommModemStatus(impl_->serial_port.native_handle(), &status);
        return (status & MS_DSR_ON) != 0;
#endif // WIN32
#ifdef __linux__
        int status;
        ::ioctl(impl_->serial_port.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_DSR) != 0;
#endif // __linux__
    }
    catch (...)
    {
        return false;
    }
}

bool serial_port::dcd()
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

    try
    {
#ifdef WIN32
        DWORD status;
        ::GetCommModemStatus(impl_->serial_port.native_handle(), &status);
        return (status & MS_RLSD_ON) != 0;
#endif // WIN32
#ifdef __linux__
        int status;
        ::ioctl(impl_->serial_port.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_CAR) != 0;
#endif // __linux__
    }
    catch (...)
    {
        return false;
    }
}

std::size_t serial_port::write(const std::vector<uint8_t>& data)
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

    try
    {
        return boost::asio::write(impl_->serial_port, boost::asio::buffer(data));
    }
    catch (const boost::system::system_error&)
    {
        return 0;
    }
}

std::size_t serial_port::write(const std::string& data)
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

    try
    {
        return boost::asio::write(impl_->serial_port, boost::asio::buffer(data));
    }
    catch (const boost::system::system_error&)
    {
        return 0;
    }
}

std::vector<uint8_t> serial_port::read(std::size_t size)
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

    std::vector<uint8_t> buffer(size);

    try
    {
        std::size_t bytes_read = boost::asio::read(impl_->serial_port,
            boost::asio::buffer(buffer));
        buffer.resize(bytes_read);
        return buffer;
    }
    catch (const boost::system::system_error&)
    {
        buffer.clear();
        return buffer;
    }
}

std::vector<uint8_t> serial_port::read_some(std::size_t max_size)
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

    std::vector<uint8_t> buffer(max_size);

    try
    {
        std::size_t bytes_read = impl_->serial_port.read_some(boost::asio::buffer(buffer));
        buffer.resize(bytes_read);
        return buffer;
    }
    catch (const boost::system::system_error&)
    {
        buffer.clear();
        return buffer;
    }
}

std::string serial_port::read_until(const std::string& delimiter)
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

    try
    {
        boost::asio::streambuf buffer;
        boost::asio::read_until(impl_->serial_port, buffer, delimiter);

        std::istream is(&buffer);
        std::string result;
        std::getline(is, result);
        return result;
    }
    catch (const boost::system::system_error&)
    {
        return "";
    }
}

bool serial_port::is_open()
{
    return is_open_;
}

std::size_t serial_port::bytes_available()
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

    try
    {
#ifdef WIN32
        COMSTAT comstat;
        DWORD errors;
        if (::ClearCommError(impl_->serial_port.native_handle(), &errors, &comstat))
        {
            return comstat.cbInQue;
        }
        return 0;
#endif // WIN32
#ifdef __linux__
        int bytes_available = 0;
        ::ioctl(impl_->serial_port.native_handle(), FIONREAD, &bytes_available);
        return bytes_available;
#endif // __linux__
    }
    catch (...)
    {
        return 0;
    }
}

void serial_port::flush()
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

#ifdef WIN32
    ::FlushFileBuffers(impl_->serial_port.native_handle());
#endif // WIN32
#ifdef __linux__
    ::tcflush(impl_->serial_port.native_handle(), TCIOFLUSH);
#endif // __linux__
}

void serial_port::timeout(unsigned int milliseconds)
{
    if (!is_open_)
    {
        throw std::runtime_error("Serial port not open");
    }

#ifdef WIN32
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = milliseconds;
    timeouts.ReadTotalTimeoutConstant = milliseconds;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = milliseconds;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    ::SetCommTimeouts(impl_->serial_port.native_handle(), &timeouts);
#endif // WIN32
#ifdef __linux__
    struct termios tty;
    ::tcgetattr(impl_->serial_port.native_handle(), &tty);
    tty.c_cc[VTIME] = milliseconds / 100;  // Convert to deciseconds
    tty.c_cc[VMIN] = 0;
    ::tcsetattr(impl_->serial_port.native_handle(), TCSANOW, &tty);
#endif // __linux__
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_serial_port_client_impl                                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_serial_port_client_impl
{
    boost::asio::io_context io_context;
    boost::asio::ip::tcp::socket socket { io_context };
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// request (helper for tcp_serial_port_client)                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

nlohmann::json handle_request(tcp_serial_port_client& client, tcp_serial_port_client_impl& impl, const nlohmann::json& request)
{
    if (!client.connected())
    {
        throw std::runtime_error("Client not connected");
    }

    // Send the request

    std::string data = request.dump();
    uint32_t length = boost::endian::native_to_big(static_cast<uint32_t>(data.size()));

    boost::asio::write(impl.socket, boost::asio::buffer(&length, sizeof(length)));
    boost::asio::write(impl.socket, boost::asio::buffer(data));

    // Receive the response

    boost::asio::read(impl.socket, boost::asio::buffer(&length, sizeof(length)));
    data.resize(boost::endian::big_to_native(length));
    boost::asio::read(impl.socket, boost::asio::buffer(data.data(), data.size()));

    nlohmann::json response = nlohmann::json::parse(data);

    if (response.contains("error"))
    {
        std::string error_message = response["error"].get<std::string>();
        throw std::runtime_error(error_message);
    }

    return response;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_serial_port_client                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

tcp_serial_port_client::tcp_serial_port_client() : impl_(std::make_unique<tcp_serial_port_client_impl>())
{
}

tcp_serial_port_client::~tcp_serial_port_client() = default;

tcp_serial_port_client::tcp_serial_port_client(tcp_serial_port_client&& other) noexcept : impl_(std::move(other.impl_)), connected_(other.connected_)
{
    other.connected_ = false;
}

tcp_serial_port_client& tcp_serial_port_client::operator=(tcp_serial_port_client&& other) noexcept
{
    if (this != &other)
    {
        try
        {
            disconnect();
        }
        catch (...)
        {
            // Ignore
        }

        impl_ = std::move(other.impl_);
        connected_ = other.connected_;
        other.connected_ = false;
    }

    return *this;
}

bool tcp_serial_port_client::connect(const std::string& host, int port)
{
    try
    {
        boost::asio::ip::tcp::resolver resolver(impl_->io_context);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        boost::asio::connect(impl_->socket, endpoints);
        connected_ = true;
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

void tcp_serial_port_client::disconnect()
{
    if (connected_)
    {
        boost::system::error_code ec;
        impl_->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        impl_->socket.close(ec);
        connected_ = false;
    }
}

bool tcp_serial_port_client::connected() const
{
    return connected_;
}

void tcp_serial_port_client::rts(bool enable)
{
    // Send request to set RTS
    //
    // Request: { "command": "set_rts", "value": <bool> }
    // Response: { "value": "ok" }

    handle_request(*this, *impl_, { {"command", "set_rts"}, {"value", enable} });
}

bool tcp_serial_port_client::rts()
{
    // Send request to get RTS
    //
    // Request: { "command": "get_rts" }
    // Response: { "value": "<bool>" }

    return handle_request(*this, *impl_, { {"command", "get_rts"} })["value"].get<bool>();
}

void tcp_serial_port_client::dtr(bool enable)
{
    // Send request to set DTR
    //
    // Request: { "command": "set_dtr", "value": <bool> }
    // Response: { "value": "ok" }

    handle_request(*this, *impl_, { {"command", "set_dtr"}, {"value", enable} });
}

bool tcp_serial_port_client::dtr()
{
    // Send request to get DTR
    //
    // Request: { "command": "get_dtr" }
    // Response: { "value": "<bool>" }

    return handle_request(*this, *impl_, { {"command", "get_dtr"} })["value"].get<bool>();
}

bool tcp_serial_port_client::cts()
{
    // Send request to get CTS
    //
    // Request: { "command": "get_cts" }
    // Response: { "value": "<bool>" }

    return handle_request(*this, *impl_, { {"command", "get_cts"} })["value"].get<bool>();
}

bool tcp_serial_port_client::dsr()
{
    // Send request to get DSR
    //
    // Request: { "command": "get_dsr" }
    // Response: { "value": "<bool>" }

    return handle_request(*this, *impl_, { {"command", "get_dsr"} })["value"].get<bool>();
}

bool tcp_serial_port_client::dcd()
{
    // Send request to get DCD
    //
    // Request: { "command": "get_dcd" }
    // Response: { "value": "<bool>" }

    return handle_request(*this, *impl_, { {"command", "get_dcd"} })["value"].get<bool>();
}

std::size_t tcp_serial_port_client::write(const std::vector<uint8_t>& data)
{
    // Send request to write data
    //
    // Request: { "command": "write", "data": "<base64_encoded_data>" }
    // Response: { "value": <number_of_bytes_written> }

    return handle_request(*this, *impl_, { {"command", "write"}, {"data", base64_encode(data)} })["value"].get<std::size_t>();
}

std::size_t tcp_serial_port_client::write(const std::string& data)
{
    // Send request to write string data
    //
    // Request: { "command": "write_string", "data": "<base64_encoded_data>" }
    // Response: { "value": <number_of_bytes_written> }

    return handle_request(*this, *impl_, { {"command", "write_string"}, {"data", base64_encode(data)} })["value"].get<std::size_t>();
}

std::vector<uint8_t> tcp_serial_port_client::read(std::size_t size)
{
    // Send request to read data
    //
    // Request: { "command": "read", "size": <number_of_bytes_to_read> }
    // Response: { "value": "<base64_encoded_data>" }

    return base64_decode(handle_request(*this, *impl_, { {"command", "read"}, {"size", size} })["value"].get<std::string>());
}

std::vector<uint8_t> tcp_serial_port_client::read_some(std::size_t max_size)
{
    // Send request to read some data
    //
    // Request: { "command": "read_some", "max_size": <maximum_number_of_bytes_to_read> }
    // Response: { "value": "<base64_encoded_data>" }

    return base64_decode(handle_request(*this, *impl_, { {"command", "read_some"}, {"max_size", max_size} })["value"].get<std::string>());
}

std::string tcp_serial_port_client::read_until(const std::string& delimiter)
{
    // Send request to read until delimiter
    //
    // Request: { "command": "read_until", "delimiter": "<base64_encoded_delimiter>" }
    // Response: { "value": "<base64_encoded_data>" }

    return base64_decode_string(handle_request(*this, *impl_, { {"command", "read_until"}, {"delimiter", base64_encode(delimiter)} })["value"].get<std::string>());
}

bool tcp_serial_port_client::is_open()
{
    if (!connected_)
    {
        return false;
    }

    // Send request to check if port is open
    //
    // Request: { "command": "is_open" }
    // Response: { "value": "<bool>" }

    return handle_request(*this, *impl_, { {"command", "is_open"} })["value"].get<bool>();
}

std::size_t tcp_serial_port_client::bytes_available()
{
    // Send request to get bytes available
    //
    // Request: { "command": "bytes_available" }
    // Response: { "value": <number_of_bytes_available> }

    return handle_request(*this, *impl_, { {"command", "bytes_available"} })["value"].get<std::size_t>();
}

void tcp_serial_port_client::flush()
{
    // Send request to flush
    //
    // Request: { "command": "flush" }
    // Response: { "value": "ok" }

    handle_request(*this, *impl_, { {"command", "flush"} });
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_server_base_impl                                             //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_server_base_impl
{
    boost::asio::io_context io_context;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_client_connection_impl                                       //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_client_connection_impl
{
    explicit tcp_client_connection_impl(boost::asio::ip::tcp::socket s) : socket(std::move(s))
    {
    }

    boost::asio::ip::tcp::socket socket;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// close_socket                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

void close_socket(boost::asio::ip::tcp::socket& socket)
{
    boost::system::error_code ec;
    socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket.close(ec);
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_server_base                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

tcp_server_base::tcp_server_base() : impl_(std::make_unique<tcp_server_base_impl>())
{
}

tcp_server_base::tcp_server_base(tcp_server_base&& other) noexcept : impl_(std::make_unique<tcp_server_base_impl>())
{
    assert(!other.running_);
}

tcp_server_base& tcp_server_base::operator=(tcp_server_base&& other) noexcept
{
    if (this != &other)
    {
        assert(!running_);
        assert(!other.running_);

        impl_ = std::make_unique<tcp_server_base_impl>();
    }

    return *this;
}

tcp_server_base::~tcp_server_base()
{
    stop();
}

bool tcp_server_base::start(const std::string& host, int port)
{
    try
    {
        ready_ = false;

        impl_->io_context.restart();

        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::make_address(host), static_cast<unsigned short>(port));

        impl_->acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(impl_->io_context);

        impl_->acceptor->open(endpoint.protocol());
        impl_->acceptor->set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        impl_->acceptor->bind(endpoint);
        impl_->acceptor->listen();

        running_ = true;

        thread_ = std::jthread(&tcp_server_base::run, this);

        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return ready_; });

        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

void tcp_server_base::stop()
{
    if (!running_)
    {
        return;
    }
    running_ = false;

    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& conn : connections_)
        {
            close_socket(conn->socket);
        }
        connections_.clear();
    }

    boost::asio::post(impl_->io_context, [] { /* cancel all pending ops if needed */ });

    impl_->io_context.stop();

    if (impl_->acceptor && impl_->acceptor->is_open())
    {
        boost::system::error_code ec;
        impl_->acceptor->close(ec);
    }

    if (thread_.joinable())
    {
        thread_.join();
    }
}

bool tcp_server_base::running() const
{
    return running_;
}

void tcp_server_base::throw_if_faulted()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (exception_)
    {
        std::exception_ptr ex = exception_;
        exception_ = nullptr;
        std::rethrow_exception(ex);
    }
}

bool tcp_server_base::faulted()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return exception_ != nullptr;
}

void tcp_server_base::run()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = true;
    }
    cv_.notify_one();

    try
    {
        run_internal();
    }
    catch (const boost::system::system_error& e)
    {
        running_ = false;

        if (e.code() != boost::asio::error::operation_aborted)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            exception_ = std::make_exception_ptr(e);
            cv_.notify_all();
        }
    }
    catch (...)
    {
        running_ = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            exception_ = std::current_exception();
        }
        cv_.notify_all();
    }

    running_ = false;
}

void tcp_server_base::run_internal()
{
    accept_async();
    impl_->io_context.run();
}

void tcp_server_base::accept_async()
{
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(impl_->io_context);

    impl_->acceptor->async_accept(*socket, [this, socket](boost::system::error_code ec) {
        if (!running_)
        {
            return;
        }

        if (!ec)
        {
            auto connection = std::make_shared<tcp_client_connection_impl>(std::move(*socket));

            {
                std::lock_guard<std::mutex> lock(connections_mutex_);
                connections_.insert(connection);
            }

            read_async(connection);
        }
        else if (ec != boost::asio::error::operation_aborted)
        {
            return;
        }

        accept_async();
    });
}

void tcp_server_base::read_async(std::shared_ptr<tcp_client_connection_impl> connection)
{
    auto length_buffer = std::make_shared<uint32_t>(0); // Buffer for the 4-byte length prefix

    boost::asio::async_read(
        connection->socket,
        boost::asio::buffer(length_buffer.get(), sizeof(uint32_t)),
        [this, connection, length_buffer](boost::system::error_code ec, std::size_t) {
            if (ec || !running_)
            {
                close_socket(connection->socket);

                {
                    std::lock_guard<std::mutex> lock(connections_mutex_);
                    connections_.erase(connection);
                }

                return;
            }

            auto request = std::make_shared<std::string>(boost::endian::big_to_native(*length_buffer), '\0');

            boost::asio::async_read(
                connection->socket,
                boost::asio::buffer(request->data(), request->size()),
                [this, connection, request](boost::system::error_code ec, std::size_t) {
                    if (ec || !running_)
                    {
                        close_socket(connection->socket);

                        {
                            std::lock_guard<std::mutex> lock(connections_mutex_);
                            connections_.erase(connection);
                        }

                        return;
                    }

                    std::string response;
                    try
                    {
                        response = handle_request(*request);
                    }
                    catch (const std::exception& e)
                    {
                        response = nlohmann::json{ {"error", e.what()} }.dump();
                    }

                    write_async(connection, std::move(response));
                }
            );
        }
    );
}

void tcp_server_base::write_async(std::shared_ptr<tcp_client_connection_impl> connection, std::string response)
{
    auto data_buffer = std::make_shared<std::string>(std::move(response));

    uint32_t length = boost::endian::native_to_big(static_cast<uint32_t>(data_buffer->size()));
    auto length_buffer = std::make_shared<uint32_t>(length);

    std::array<boost::asio::const_buffer, 2> buffers = {
        boost::asio::buffer(length_buffer.get(), sizeof(uint32_t)),
        boost::asio::buffer(*data_buffer)
    };

    boost::asio::async_write(
        connection->socket,
        buffers,
        [this, connection, data_buffer, length_buffer](boost::system::error_code ec, std::size_t) {
            if (ec || !running_)
            {
                close_socket(connection->socket);

                {
                    std::lock_guard<std::mutex> lock(connections_mutex_);
                    connections_.erase(connection);
                }

                return;
            }
            read_async(connection);
        }
    );
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_serial_port_server                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

tcp_serial_port_server::tcp_serial_port_server() : tcp_server_base()
{
}

tcp_serial_port_server::tcp_serial_port_server(serial_port_base& serial_port) : tcp_server_base(), serial_port_(serial_port)
{
}

tcp_serial_port_server::tcp_serial_port_server(tcp_serial_port_server&& other) noexcept : tcp_server_base(std::move(other)), serial_port_(std::move(other.serial_port_))
{
    assert(!other.running());
}

tcp_serial_port_server& tcp_serial_port_server::operator=(tcp_serial_port_server&& other) noexcept
{
    if (this != &other)
    {
        assert(!running());
        assert(!other.running());

        tcp_server_base::operator=(std::move(other));
        serial_port_ = std::move(other.serial_port_);
    }

    return *this;
}

tcp_serial_port_server::~tcp_serial_port_server() = default;

bool tcp_serial_port_server::start(const std::string& host, int port)
{
    if (!serial_port_.has_value())
    {
        return false;
    }

    return tcp_server_base::start(host, port);
}

std::string tcp_serial_port_server::handle_request(const std::string& data)
{
    serial_port_base& serial_port = serial_port_->get();

    std::lock_guard<std::mutex> lock(serial_port_mutex_);

    nlohmann::json request = nlohmann::json::parse(data);

    std::string command = request.value("command", "");

    nlohmann::json response;

    if (command == "set_rts")
    {
        // Set RTS
        //
        // Request: { "command": "set_rts", "value": <bool> }
        // Response: { "value": "ok" }

        serial_port.rts(request.value("value", false));
        response["value"] = "ok";
    }
    else if (command == "get_rts")
    {
        // Get RTS
        //
        // Request: { "command": "get_rts" }
        // Response: { "value": <bool> }

        response["value"] = serial_port.rts();
    }
    else if (command == "set_dtr")
    {
        // Set DTR
        //
        // Request: { "command": "set_dtr", "value": <bool> }
        // Response: { "value": "ok" }

        serial_port.dtr(request.value("value", false));
        response["value"] = "ok";
    }
    else if (command == "get_dtr")
    {
        // Get DTR
        //
        // Request: { "command": "get_dtr" }
        // Response: { "value": <bool> }

        response["value"] = serial_port.dtr();
    }
    else if (command == "get_cts")
    {
        // Get CTS
        //
        // Request: { "command": "get_cts" }
        // Response: { "value": <bool> }

        response["value"] = serial_port.cts();
    }
    else if (command == "get_dsr")
    {
        // Get DSR
        //
        // Request: { "command": "get_dsr" }
        // Response: { "value": <bool> }

        response["value"] = serial_port.dsr();
    }
    else if (command == "get_dcd")
    {
        // Get DCD
        //
        // Request: { "command": "get_dcd" }
        // Response: { "value": <bool> }

        response["value"] = serial_port.dcd();
    }
    else if (command == "write")
    {
        // Write data
        //
        // Request: { "command": "write", "data": "<base64_encoded_data>" }
        // Response: { "value": <number_of_bytes_written> }

        std::string encoded_data = request.value("data", std::string());
        std::vector<uint8_t> write_data = base64_decode(encoded_data);
        response["value"] = serial_port.write(write_data);
    }
    else if (command == "write_string")
    {
        // Write string data
        //
        // Request: { "command": "write_string", "data": "<base64_encoded_string>" }
        // Response: { "value": <number_of_bytes_written> }

        std::string encoded_data = request.value("data", std::string());
        std::string write_data = base64_decode_string(encoded_data);
        response["value"] = serial_port.write(write_data);
    }
    else if (command == "read")
    {
        // Read data
        //
        // Request: { "command": "read", "size": <number_of_bytes_to_read> }
        // Response: { "value": "<base64_encoded_data>" }

        std::size_t size = request.value("size", static_cast<std::size_t>(0));
        response["value"] = base64_encode(serial_port.read(size));
    }
    else if (command == "read_some")
    {
        // Read some data
        //
        // Request: { "command": "read_some", "max_size": <maximum_number_of_bytes_to_read> }
        // Response: { "value": "<base64_encoded_data>" }

        std::size_t max_size = request.value("max_size", static_cast<std::size_t>(0));
        response["value"] = base64_encode(serial_port.read_some(max_size));
    }
    else if (command == "read_until")
    {
        // Read until delimiter
        //
        // Request: { "command": "read_until", "delimiter": "<base64_encoded_delimiter>" }
        // Response: { "value": "<base64_encoded_data>" }

        std::string encoded_delimiter = request.value("delimiter", std::string());
        std::string delimiter = base64_decode_string(encoded_delimiter);
        response["value"] = base64_encode(serial_port.read_until(delimiter));
    }
    else if (command == "is_open")
    {
        // Is open
        //
        // Request: { "command": "is_open" }
        // Response: { "value": <bool> }

        response["value"] = serial_port.is_open();
    }
    else if (command == "bytes_available")
    {
        // Bytes available
        //
        // Request: { "command": "bytes_available" }
        // Response: { "value": <number_of_bytes_available> }

        response["value"] = serial_port.bytes_available();
    }
    else if (command == "flush")
    {
        // Flush
        //
        // Request: { "command": "flush" }
        // Response: { "value": "ok" }

        serial_port.flush();
        response["value"] = "ok";
    }
    else
    {
        // Unknown command
        //
        // Response: { "error": "unknown command: <command>" }

        response["error"] = "unknown command: " + command;
    }

    std::string response_string = response.dump();

    return response_string;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// ptt_control_library_impl                                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct ptt_control_library_impl
{
#if WIN32
    HMODULE handle = nullptr;
#endif // WIN32
#ifdef __linux__
    void* handle = nullptr;
#endif
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// ptt_control_library                                              //
//                                                                  //
//                                                                  //
// **************************************************************** //

ptt_control_library::ptt_control_library() : pimpl_(std::make_unique<ptt_control_library_impl>())
{
}

ptt_control_library::ptt_control_library(ptt_control_library&& other) noexcept = default;
ptt_control_library& ptt_control_library::operator=(ptt_control_library&& other) noexcept = default;

ptt_control_library::~ptt_control_library()
{
    uninit();
}

void ptt_control_library::load(const std::string& library_path)
{
    load(library_path, nullptr);
}

void ptt_control_library::load(const std::string& library_path, void* context)
{
    if (loaded_)
    {
        throw std::runtime_error("Library already loaded");
    }

#if WIN32

    pimpl_->handle = LoadLibraryA(library_path.c_str());

    if (pimpl_->handle == nullptr)
    {
        throw std::runtime_error("Failed to load library: " + library_path);
    }

    init_fptr_ = reinterpret_cast<init_fptr>(GetProcAddress(pimpl_->handle, "init"));
    uninit_fptr_ = reinterpret_cast<uninit_fptr>(GetProcAddress(pimpl_->handle, "uninit"));
    set_ptt_fptr_ = reinterpret_cast<set_ptt_fptr>(GetProcAddress(pimpl_->handle, "set_ptt"));
    get_ptt_fptr_ = reinterpret_cast<get_ptt_fptr>(GetProcAddress(pimpl_->handle, "get_ptt"));

#endif
#ifdef __linux__

    pimpl_->handle = dlopen(library_path.c_str(), RTLD_NOW);

    if (pimpl_->handle == nullptr)
    {
        throw std::runtime_error("Failed to load library: " + library_path);
    }

    init_fptr_ = reinterpret_cast<init_fptr>(dlsym(pimpl_->handle, "init"));
    uninit_fptr_ = reinterpret_cast<uninit_fptr>(dlsym(pimpl_->handle, "uninit"));
    set_ptt_fptr_ = reinterpret_cast<set_ptt_fptr>(dlsym(pimpl_->handle, "set_ptt"));
    get_ptt_fptr_ = reinterpret_cast<get_ptt_fptr>(dlsym(pimpl_->handle, "get_ptt"));

#endif

    loaded_ = true;

    if (set_ptt_fptr_ == nullptr || get_ptt_fptr_ == nullptr)
    {
        throw std::runtime_error("Failed to resolve PTT functions");
    }

    if (init_fptr_ != nullptr)
    {
        if (init_fptr_(context) != 0)
        {
            throw std::runtime_error("Library init failed");
        }
    }
}

void ptt_control_library::unload()
{
    if (!loaded_)
    {
        return;
    }

    if (pimpl_ == nullptr || pimpl_->handle == nullptr)
    {
        return;
    }

    if (uninit_fptr_ != nullptr)
    {
        uninit_fptr_();
    }

#if WIN32
    FreeLibrary(pimpl_->handle);
#endif
#ifdef __linux__
    dlclose(pimpl_->handle);
#endif

    pimpl_->handle = nullptr;
    set_ptt_fptr_ = nullptr;
    get_ptt_fptr_ = nullptr;
    init_fptr_ = nullptr;
    uninit_fptr_ = nullptr;
}

void ptt_control_library::uninit()
{
    if (uninit_fptr_ != nullptr)
    {
        if (uninit_fptr_() != 0)
        {
            throw std::runtime_error("Library uninit failed");
        }
    }
}

void ptt_control_library::ptt(bool enable)
{
    if (set_ptt_fptr_ != nullptr)
    {
        set_ptt_fptr_(enable ? 1 : 0);
    }
}

bool ptt_control_library::ptt()
{
    if (get_ptt_fptr_ != nullptr)
    {
        int ptt_state = 0;
        if (get_ptt_fptr_(&ptt_state) == 0)
        {
            return ptt_state != 0;
        }
    }
    return false;
}

ptt_control_library::operator bool() const
{
    return loaded_;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_ptt_control_client_impl                                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_ptt_control_client_impl
{
    boost::asio::io_context io_context;
    boost::asio::ip::tcp::socket socket{ io_context };
};


// **************************************************************** //
//                                                                  //
//                                                                  //
// request (helper for tcp_ptt_control_client)                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

nlohmann::json handle_request(tcp_ptt_control_client& client, tcp_ptt_control_client_impl& impl, const nlohmann::json& request)
{
    if (!client.connected())
    {
        throw std::runtime_error("Client not connected");
    }

    // Send the request

    std::string data = request.dump();
    uint32_t length = boost::endian::native_to_big(static_cast<uint32_t>(data.size()));

    boost::asio::write(impl.socket, boost::asio::buffer(&length, sizeof(length)));
    boost::asio::write(impl.socket, boost::asio::buffer(data));

    // Receive the response

    boost::asio::read(impl.socket, boost::asio::buffer(&length, sizeof(length)));
    data.resize(boost::endian::big_to_native(length));
    boost::asio::read(impl.socket, boost::asio::buffer(data.data(), data.size()));

    nlohmann::json response = nlohmann::json::parse(data);

    if (response.contains("error"))
    {
        std::string error_message = response["error"].get<std::string>();
        throw std::runtime_error(error_message);
    }

    return response;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_ptt_control_client                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

tcp_ptt_control_client::tcp_ptt_control_client() : impl_(std::make_unique<tcp_ptt_control_client_impl>())
{
}

tcp_ptt_control_client& tcp_ptt_control_client::operator=(tcp_ptt_control_client&& other) noexcept
{
    if (this != &other)
    {
        try
        {
            disconnect();
        }
        catch (...)
        {
            // Ignore
        }

        impl_ = std::move(other.impl_);
        connected_ = other.connected_;
        other.connected_ = false;
    }

    return *this;
}

tcp_ptt_control_client::tcp_ptt_control_client(tcp_ptt_control_client&& other) noexcept : impl_(std::move(other.impl_)), connected_(other.connected_)
{
    other.connected_ = false;
}

tcp_ptt_control_client::~tcp_ptt_control_client() = default;

bool tcp_ptt_control_client::connect(const std::string& host, int port)
{
    try
    {
        boost::asio::ip::tcp::resolver resolver(impl_->io_context);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        boost::asio::connect(impl_->socket, endpoints);
        connected_ = true;
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

void tcp_ptt_control_client::disconnect()
{
    if (connected_)
    {
        boost::system::error_code ec;
        impl_->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        impl_->socket.close(ec);
        connected_ = false;
    }
}

bool tcp_ptt_control_client::connected() const
{
    return connected_;
}

void tcp_ptt_control_client::ptt(bool ptt_state)
{
    // Send request to set PTT
    //
    // Request: { "command": "set_ptt", "value": <bool> }
    // Response: { "value": "ok" }

    handle_request(*this, *impl_, { {"command", "set_ptt"}, {"value", ptt_state} });
}

bool tcp_ptt_control_client::ptt()
{
    // Send request to get PTT state
    //
    // Request: { "command": "get_ptt" }
    // Response: { "value": "<bool>" }

    return handle_request(*this, *impl_, { {"command", "get_ptt"} })["value"].get<bool>();
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_ptt_control_server                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

tcp_ptt_control_server::tcp_ptt_control_server() : tcp_server_base()
{
}

tcp_ptt_control_server::tcp_ptt_control_server(tcp_ptt_control_server&& other) noexcept : tcp_server_base(std::move(other)), ptt_callable_(std::move(other.ptt_callable_))
{
    assert(!other.running());
    other.ptt_callable_ = nullptr;
}

tcp_ptt_control_server& tcp_ptt_control_server::operator=(tcp_ptt_control_server&& other) noexcept
{
    if (this != &other)
    {
        assert(!running());
        assert(!other.running());

        tcp_server_base::operator=(std::move(other));

        ptt_callable_ = std::move(other.ptt_callable_);
        other.ptt_callable_ = nullptr;
    }

    return *this;
}

tcp_ptt_control_server::~tcp_ptt_control_server() = default;

bool tcp_ptt_control_server::start(const std::string& host, int port)
{
    if (!ptt_callable_)
    {
        return false;
    }

    return tcp_server_base::start(host, port);
}

std::string tcp_ptt_control_server::handle_request(const std::string& data)
{
    nlohmann::json request = nlohmann::json::parse(data);

    std::string command = request.value("command", "");

    nlohmann::json response;

    if (command == "set_ptt")
    {
        // Set PTT
        //
        // Request: { "command": "set_ptt", "value": <bool> }
        // Response: { "value": "ok" }

        ptt_callable_->invoke(request.value("value", false));
        response["value"] = "ok";
    }
    else if (command == "get_ptt")
    {
        // Get PTT
        //
        // Request: { "command": "get_ptt" }
        // Response: { "value": <bool> }

        response["value"] = false;
    }
    else
    {
        // Unknown command
        //
        // Response: { "error": "unknown command: <command>" }

        response["error"] = "unknown command: " + command;
    }

    std::string response_string = response.dump();

    return response_string;
}

LIBMODEM_NAMESPACE_END