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
#include <cstring>

#ifdef WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>

#endif // WIN32

#if defined(__linux__) || defined(__APPLE__)
#include <sys/ioctl.h>
#include <termios.h>
#include <dlfcn.h>
#endif // defined(__linux__) || defined(__APPLE__)

#ifdef __linux__
#include <netinet/tcp.h>
#endif

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
// io_exception                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

io_exception::io_exception() : message_(), error_(io_error::none)
{
}

io_exception::io_exception(const std::string& message) : message_(message), error_(io_error::none)
{
}

io_exception::io_exception(const std::string& message, io_error error) : message_(message), error_(error)
{
}

io_exception::io_exception(io_error error) : message_(), error_(error)
{
}

io_exception::io_exception(const io_exception& other) : std::exception(other), message_(other.message_), error_(other.error_)
{
}

io_exception& io_exception::operator=(const io_exception& other)
{
    if (this != &other)
    {
        std::exception::operator=(other);
        message_ = other.message_;
        error_ = other.error_;
    }
    return *this;
}

io_exception::~io_exception() = default;

const char* io_exception::what() const noexcept
{
    return message_.c_str();
}

io_error io_exception::error() const noexcept
{
    return error_;
}

const std::string& io_exception::message() const noexcept
{
    return message_;
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
        throw io_exception("Serial port is already open", io_error::invalid_state);
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
        throw io_exception("Serial port not open", io_error::not_initialized);
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
#if defined(__linux__) || defined(__APPLE__)
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
#endif // defined(__linux__) || defined(__APPLE__)
    }
    catch (...)
    {
    }
}

bool serial_port::rts()
{
    if (!is_open_)
    {
        throw io_exception("Serial port not open", io_error::not_initialized);
    }

    try
    {
#ifdef WIN32
        // Note: GetCommModemStatus doesn't return RTS state directly
        return rts_;
#endif // WIN32
#if defined(__linux__) || defined(__APPLE__)
        int status;
        ::ioctl(impl_->serial_port.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_RTS) != 0;
#endif // defined(__linux__) || defined(__APPLE__)
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
        throw io_exception("Serial port not open", io_error::not_initialized);
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
#if defined(__linux__) || defined(__APPLE__)
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
#endif // defined(__linux__) || defined(__APPLE__)
    }
    catch (...)
    {
    }
}

bool serial_port::dtr()
{
    if (!is_open_)
    {
        throw io_exception("Serial port not open", io_error::not_initialized);
    }

    try
    {
#ifdef WIN32
        // Note: GetCommModemStatus doesn't return DTR state directly
        return dtr_;
#endif // WIN32
#if defined(__linux__) || defined(__APPLE__)
        int status;
        ::ioctl(impl_->serial_port.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_DTR) != 0;
#endif // defined(__linux__) || defined(__APPLE__)
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
        throw io_exception("Serial port not open", io_error::not_initialized);
    }

    try
    {
#ifdef WIN32
        DWORD status;
        ::GetCommModemStatus(impl_->serial_port.native_handle(), &status);
        return (status & MS_CTS_ON) != 0;
#endif // WIN32
#if defined(__linux__) || defined(__APPLE__)
        int status;
        ::ioctl(impl_->serial_port.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_CTS) != 0;
#endif // defined(__linux__) || defined(__APPLE__)
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
        throw io_exception("Serial port not open", io_error::not_initialized);
    }

    try
    {
#ifdef WIN32
        DWORD status;
        ::GetCommModemStatus(impl_->serial_port.native_handle(), &status);
        return (status & MS_DSR_ON) != 0;
#endif // WIN32
#if defined(__linux__) || defined(__APPLE__)
        int status;
        ::ioctl(impl_->serial_port.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_DSR) != 0;
#endif // defined(__linux__) || defined(__APPLE__)
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
        throw io_exception("Serial port not open", io_error::not_initialized);
    }

    try
    {
#ifdef WIN32
        DWORD status;
        ::GetCommModemStatus(impl_->serial_port.native_handle(), &status);
        return (status & MS_RLSD_ON) != 0;
#endif // WIN32
#if defined(__linux__) || defined(__APPLE__)
        int status;
        ::ioctl(impl_->serial_port.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_CAR) != 0;
#endif // defined(__linux__) || defined(__APPLE__)
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
        throw io_exception("Serial port not open", io_error::not_initialized);
    }

    try
    {
        return boost::asio::write(impl_->serial_port, boost::asio::buffer(data));
    }
    catch (const boost::system::system_error& e)
    {
        throw io_exception(e.what(), io_error::io_error);
    }
}

std::size_t serial_port::write(const std::string& data)
{
    if (!is_open_)
    {
        throw io_exception("Serial port not open", io_error::not_initialized);
    }

    try
    {
        return boost::asio::write(impl_->serial_port, boost::asio::buffer(data));
    }
    catch (const boost::system::system_error& e)
    {
        throw io_exception(e.what(), io_error::io_error);
    }
}

std::vector<uint8_t> serial_port::read(std::size_t size)
{
    if (!is_open_)
    {
        throw io_exception("Serial port not open", io_error::not_initialized);
    }

    std::vector<uint8_t> buffer(size);

    try
    {
        std::size_t bytes_read = boost::asio::read(impl_->serial_port, boost::asio::buffer(buffer));
        buffer.resize(bytes_read);
        return buffer;
    }
    catch (const boost::system::system_error& e)
    {
        throw io_exception(e.what(), io_error::io_error);
    }
}

std::vector<uint8_t> serial_port::read_some(std::size_t max_size)
{
    if (!is_open_)
    {
        throw io_exception("Serial port not open", io_error::not_initialized);
    }

    std::vector<uint8_t> buffer(max_size);

    try
    {
        std::size_t bytes_read = impl_->serial_port.read_some(boost::asio::buffer(buffer));
        buffer.resize(bytes_read);
        return buffer;
    }
    catch (const boost::system::system_error& e)
    {
        throw io_exception(e.what(), io_error::io_error);
    }
}

std::string serial_port::read_until(const std::string& delimiter)
{
    if (!is_open_)
    {
        throw io_exception("Serial port not open", io_error::not_initialized);
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
    catch (const boost::system::system_error& e)
    {
        throw io_exception(e.what(), io_error::io_error);
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
        throw io_exception("Serial port not open", io_error::not_initialized);
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
#if defined(__linux__) || defined(__APPLE__)
        int bytes_available = 0;
        ::ioctl(impl_->serial_port.native_handle(), FIONREAD, &bytes_available);
        return bytes_available;
#endif // defined(__linux__) || defined(__APPLE__)
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
        throw io_exception("Serial port not open", io_error::not_initialized);
    }

#ifdef WIN32
    ::FlushFileBuffers(impl_->serial_port.native_handle());
#endif // WIN32
#if defined(__linux__) || defined(__APPLE__)
    ::tcflush(impl_->serial_port.native_handle(), TCIOFLUSH);
#endif // defined(__linux__) || defined(__APPLE__)
}

void serial_port::timeout(unsigned int milliseconds)
{
    if (!is_open_)
    {
        throw io_exception("Serial port not open", io_error::not_initialized);
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
#if defined(__linux__) || defined(__APPLE__)
    struct termios tty;
    ::tcgetattr(impl_->serial_port.native_handle(), &tty);
    tty.c_cc[VTIME] = milliseconds / 100;  // Convert to deciseconds
    tty.c_cc[VMIN] = 0;
    ::tcsetattr(impl_->serial_port.native_handle(), TCSANOW, &tty);
#endif // defined(__linux__) || defined(__APPLE__)
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_client_impl                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct tcp_client_impl
{
    boost::asio::io_context io_context;
    boost::asio::ip::tcp::socket socket { io_context };
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_client                                                       //
//                                                                  //
//                                                                  //
// **************************************************************** //

tcp_client::tcp_client() : impl_(std::make_unique<tcp_client_impl>())
{
}

tcp_client::tcp_client(tcp_client&& other) noexcept : impl_(std::move(other.impl_)), connected_(other.connected_)
{
    other.connected_ = false;
}

tcp_client& tcp_client::operator=(tcp_client&& other) noexcept
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

tcp_client::~tcp_client()
{
    disconnect();
}

bool tcp_client::connect(const std::string& host, int port)
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

void tcp_client::disconnect()
{
    if (connected_)
    {
        boost::system::error_code ec;
        impl_->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        impl_->socket.close(ec);
        connected_ = false;
    }
}

bool tcp_client::connected() const
{
    return connected_;
}

std::size_t tcp_client::write(const std::vector<uint8_t>& data)
{
    if (!connected_)
    {
        throw io_exception("Client not connected", io_error::not_initialized);
    }

    try
    {
        return boost::asio::write(impl_->socket, boost::asio::buffer(data));
    }
    catch (const boost::system::system_error& e)
    {
        throw io_exception(e.what(), io_error::io_error);
    }
}

std::size_t tcp_client::write(const std::string& data)
{
    if (!connected_)
    {
        throw io_exception("Client not connected", io_error::not_initialized);
    }

    try
    {
        return boost::asio::write(impl_->socket, boost::asio::buffer(data));
    }
    catch (const boost::system::system_error& e)
    {
        throw io_exception(e.what(), io_error::io_error);
    }
}

std::vector<uint8_t> tcp_client::read(std::size_t size)
{
    if (!connected_)
    {
        throw io_exception("Client not connected", io_error::not_initialized);
    }

    std::vector<uint8_t> buffer(size);

    try
    {
        std::size_t bytes_read = boost::asio::read(impl_->socket, boost::asio::buffer(buffer));
        buffer.resize(bytes_read);
        return buffer;
    }
    catch (const boost::system::system_error& e)
    {
        throw io_exception(e.what(), io_error::io_error);
    }
}

std::vector<uint8_t> tcp_client::read_some(std::size_t max_size)
{
    if (!connected_)
    {
        throw io_exception("Client not connected", io_error::not_initialized);
    }

    std::vector<uint8_t> buffer(max_size);

    try
    {
        std::size_t bytes_read = impl_->socket.read_some(boost::asio::buffer(buffer));
        buffer.resize(bytes_read);
        return buffer;
    }
    catch (const boost::system::system_error& e)
    {
        throw io_exception(e.what(), io_error::io_error);
    }
}

std::size_t tcp_client::bytes_available()
{
    if (!connected_)
    {
        return 0;
    }

    boost::system::error_code ec;
    std::size_t available = impl_->socket.available(ec);
    if (ec)
    {
        return 0;
    }
    return available;
}

bool tcp_client::wait_data_received(int timeout_ms)
{
    if (!connected_)
    {
        return false;
    }

    auto native_handle = impl_->socket.native_handle();

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(native_handle, &read_fds);

    timeval tv;
    timeval* tv_ptr = nullptr;

    if (timeout_ms >= 0)
    {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tv_ptr = &tv;
    }

#ifdef WIN32
    int result = ::select(0, &read_fds, nullptr, nullptr, tv_ptr);
#else
    int result = ::select(native_handle + 1, &read_fds, nullptr, nullptr, tv_ptr);
#endif

    return result > 0;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// json_request (helper for tcp clients)                            //
//                                                                  //
//                                                                  //
// **************************************************************** //

nlohmann::json json_request(tcp_client& client, const nlohmann::json& request)
{
    if (!client.connected())
    {
        throw io_exception("Client not connected", io_error::not_initialized);
    }

    try
    {
        // Send the request with length prefix
        std::string request_string = request.dump();
        uint32_t length = boost::endian::native_to_big(static_cast<uint32_t>(request_string.size()));

        std::vector<uint8_t> length_bytes(sizeof(length));
        std::memcpy(length_bytes.data(), &length, sizeof(length));
        client.write(length_bytes);
        client.write(request_string);

        // Receive the response with length prefix
        std::vector<uint8_t> response_length_bytes = client.read(sizeof(uint32_t));
        std::memcpy(&length, response_length_bytes.data(), sizeof(length));
        length = boost::endian::big_to_native(length);

        std::vector<uint8_t> response_data = client.read(length);
        std::string response_str(response_data.begin(), response_data.end());
        nlohmann::json response = nlohmann::json::parse(response_str);

        if (response.contains("error"))
        {
            std::string error_message = response["error"].get<std::string>();
            throw io_exception(error_message, io_error::io_error);
        }

        return response;
    }
    catch (const io_exception&)
    {
        throw;
    }
    catch (const std::exception& e)
    {
        throw io_exception(e.what(), io_error::internal_error);
    }
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_serial_port_client                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

void tcp_serial_port_client::rts(bool enable)
{
    json_request(*this, { {"command", "set_rts"}, {"value", enable} });
}

bool tcp_serial_port_client::rts()
{
    return json_request(*this, { {"command", "get_rts"} })["value"].get<bool>();
}

void tcp_serial_port_client::dtr(bool enable)
{
    json_request(*this, { {"command", "set_dtr"}, {"value", enable} });
}

bool tcp_serial_port_client::dtr()
{
    return json_request(*this, { {"command", "get_dtr"} })["value"].get<bool>();
}

bool tcp_serial_port_client::cts()
{
    return json_request(*this, { {"command", "get_cts"} })["value"].get<bool>();
}

bool tcp_serial_port_client::dsr()
{
    return json_request(*this, { {"command", "get_dsr"} })["value"].get<bool>();
}

bool tcp_serial_port_client::dcd()
{
    return json_request(*this, { {"command", "get_dcd"} })["value"].get<bool>();
}

std::size_t tcp_serial_port_client::write(const std::vector<uint8_t>& data)
{
    return json_request(*this, { {"command", "write"}, {"data", base64_encode(data)} })["value"].get<std::size_t>();
}

std::size_t tcp_serial_port_client::write(const std::string& data)
{
    return json_request(*this, { {"command", "write_string"}, {"data", base64_encode(data)} })["value"].get<std::size_t>();
}

std::vector<uint8_t> tcp_serial_port_client::read(std::size_t size)
{
    return base64_decode(json_request(*this, { {"command", "read"}, {"size", size} })["value"].get<std::string>());
}

std::vector<uint8_t> tcp_serial_port_client::read_some(std::size_t max_size)
{
    return base64_decode(json_request(*this, { {"command", "read_some"}, {"max_size", max_size} })["value"].get<std::string>());
}

std::string tcp_serial_port_client::read_until(const std::string& delimiter)
{
    return base64_decode_string(json_request(*this, { {"command", "read_until"}, {"delimiter", base64_encode(delimiter)} })["value"].get<std::string>());
}

bool tcp_serial_port_client::is_open()
{
    if (!connected())
    {
        return false;
    }

    return json_request(*this, { {"command", "is_open"} })["value"].get<bool>();
}

std::size_t tcp_serial_port_client::bytes_available()
{
    return json_request(*this, { {"command", "bytes_available"} })["value"].get<std::size_t>();
}

void tcp_serial_port_client::flush()
{
    json_request(*this, { {"command", "flush"} });
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

struct tcp_client_connection_impl : public std::enable_shared_from_this<tcp_client_connection_impl>
{
    explicit tcp_client_connection_impl(boost::asio::io_context& io_context) : strand(boost::asio::make_strand(io_context)), socket(strand)
    {
    }

    boost::asio::strand<boost::asio::io_context::executor_type> strand;
    boost::asio::ip::tcp::socket socket;
    size_t id;
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
    (void)other;
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
        impl_->acceptor->listen(boost::asio::socket_base::max_listen_connections);

        running_ = true;

        threads_.clear();

        for (std::size_t i = 0; i < thread_count_; ++i)
        {
            threads_.emplace_back(&tcp_server_base::run, this);
        }

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
        for (auto& connection : connections_)
        {
            close_socket(connection.second->socket);
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

    for (auto& thread : threads_)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    threads_.clear();
}

void tcp_server_base::thread_count(std::size_t size)
{
    thread_count_ = size;
}

size_t tcp_server_base::thread_count() const
{
    return thread_count_;
}

void tcp_server_base::no_delay(bool enable)
{
    no_delay_ = enable;
}

bool tcp_server_base::no_delay() const
{
    return no_delay_;
}

void tcp_server_base::keep_alive(bool enable)
{
    keep_alive_ = enable;
}

bool tcp_server_base::keep_alive() const
{
    return keep_alive_;
}

#ifdef __linux__
void tcp_server_base::keep_alive_idle(int seconds)
{
    keep_alive_idle_ = seconds;
}

int tcp_server_base::keep_alive_idle() const
{
    return keep_alive_idle_;
}

void tcp_server_base::keep_alive_interval(int seconds)
{
    keep_alive_interval_ = seconds;
}

int tcp_server_base::keep_alive_interval() const
{
    return keep_alive_interval_;
}

void tcp_server_base::keep_alive_count(int count)
{
    keep_alive_count_ = count;
}

int tcp_server_base::keep_alive_count() const
{
    return keep_alive_count_;
}
#endif

void tcp_server_base::linger(bool enable)
{
    linger_ = enable;
}

bool tcp_server_base::linger() const
{
    return linger_;
}

void tcp_server_base::linger_time(int seconds)
{
    linger_time_ = seconds;
}

int tcp_server_base::linger_time() const
{
    return linger_time_;
}

bool tcp_server_base::running() const
{
    return running_;
}

void tcp_server_base::flush()
{
}

bool tcp_server_base::faulted()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return exception_ != nullptr;
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

bool tcp_server_base::wait_stopped(int timeout_ms)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (timeout_ms < 0)
    {
        cv_.wait(lock, [this]() { return !running_; });
        return true;
    }
    else
    {
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]() { return !running_; });
    }
}

void tcp_server_base::broadcast(const std::vector<uint8_t>& message)
{
    std::lock_guard<std::mutex> lock(connections_mutex_);
    for (auto& connection : connections_)
    {
        write_async(connection.second, message);
    }
}

void tcp_server_base::send(const tcp_client_connection& connection, std::vector<uint8_t> data)
{
    std::shared_ptr<tcp_client_connection_impl> impl;

    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(connection.id);
        if (it == connections_.end())
        {
            return;
        }
        impl = it->second;
    }

    write_async(impl, std::move(data));
}

void tcp_server_base::run()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ready_)
        {
            accept_async();
            ready_ = true;
        }
    }
    cv_.notify_one();

    try
    {
        impl_->io_context.run();
    }
    catch (const boost::system::system_error& e)
    {
        running_ = false;

        if (e.code() != boost::asio::error::operation_aborted)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            exception_ = std::make_exception_ptr(io_exception(e.what(), io_error::io_error));
            cv_.notify_all();
        }
    }
    catch (const std::exception& e)
    {
        running_ = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            exception_ = std::make_exception_ptr(io_exception(e.what(), io_error::internal_error));
        }
        cv_.notify_all();
    }
    catch (...)
    {
        running_ = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            exception_ = std::make_exception_ptr(io_exception("Unknown error", io_error::internal_error));
        }
        cv_.notify_all();
    }

    running_ = false;
}

void tcp_server_base::accept_async()
{
    auto connection = std::make_shared<tcp_client_connection_impl>(impl_->io_context);

    impl_->acceptor->async_accept(connection->socket, [this, connection](boost::system::error_code ec) {
        if (!running_)
        {
            return;
        }

        if (ec)
        {
            if (ec != boost::asio::error::operation_aborted)
            {
                return;
            }
            accept_async();
            return;
        }

        if (no_delay_)
        {
            connection->socket.set_option(boost::asio::ip::tcp::no_delay(true));
        }

        if (keep_alive_)
        {
            connection->socket.set_option(boost::asio::socket_base::keep_alive(true));
        }

#ifdef __linux__
        if (keep_alive_idle_ > 0)
        {
            int fd = connection->socket.native_handle();
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keep_alive_idle_, sizeof(keep_alive_idle_));
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keep_alive_interval_, sizeof(keep_alive_interval_));
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keep_alive_count_, sizeof(keep_alive_count_));
        }
#endif

        if (linger_)
        {
            connection->socket.set_option(boost::asio::socket_base::linger(true, linger_time_));
        }

        std::size_t id = next_connection_id_++;
        connection->id = id;

        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_[id] = connection;
        }

        read_async(connection);

        accept_async();
        });
}

void tcp_server_base::read_async(std::shared_ptr<tcp_client_connection_impl> connection)
{
    auto buffer = std::make_shared<std::array<uint8_t, 4096>>();

    connection->socket.async_read_some(
        boost::asio::buffer(*buffer),
        boost::asio::bind_executor(connection->strand,
            [this, connection, buffer](boost::system::error_code ec, std::size_t bytes_read) {
                if (ec || !running_)
                {
                    on_client_disconnected(connection);

                    close_socket(connection->socket);

                    {
                        std::lock_guard<std::mutex> lock(connections_mutex_);
                        connections_.erase(connection->id);
                    }

                    return;
                }

                on_data_received(connection, std::vector<uint8_t>(buffer->begin(), buffer->begin() + bytes_read));

                read_async(connection);
            }
        )
    );
}

void tcp_server_base::write_async(std::shared_ptr<tcp_client_connection_impl> connection, std::vector<uint8_t> data)
{
    auto buffer = std::make_shared<std::vector<uint8_t>>(std::move(data));

    boost::asio::async_write(
        connection->socket,
        boost::asio::buffer(*buffer),
        boost::asio::bind_executor(connection->strand,
            [this, connection, buffer](boost::system::error_code ec, std::size_t) {
                if (ec || !running_)
                {
                    on_client_disconnected(connection);

                    close_socket(connection->socket);

                    {
                        std::lock_guard<std::mutex> lock(connections_mutex_);
                        connections_.erase(connection->id);
                    }

                    return;
                }
            }
        )
    );
}

void tcp_server_base::on_data_received(std::shared_ptr<tcp_client_connection_impl> connection, const std::vector<uint8_t>& data)
{
    auto endpoint = connection->socket.remote_endpoint();

    tcp_client_connection client_connection;
    client_connection.id = connection->id;
    client_connection.remote_address = endpoint.address().to_string();
    client_connection.remote_port = endpoint.port();

    on_data_received(client_connection, data);
}

void tcp_server_base::on_client_disconnected(std::shared_ptr<tcp_client_connection_impl> connection)
{
    auto endpoint = connection->socket.remote_endpoint();

    tcp_client_connection client_connection;
    client_connection.id = connection->id;
    client_connection.remote_address = endpoint.address().to_string();
    client_connection.remote_port = endpoint.port();

    on_client_disconnected(client_connection);
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

tcp_serial_port_server::~tcp_serial_port_server()
{
    stop();
}

bool tcp_serial_port_server::start(const std::string& host, int port)
{
    if (!serial_port_.has_value())
    {
        return false;
    }

    return tcp_server_base::start(host, port);
}

void tcp_serial_port_server::on_data_received(const tcp_client_connection& connection, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t>* buffer;

    {
        std::lock_guard<std::mutex> lock(buffers_mutex_);
        buffer = &buffers_[connection.id];
    }

    buffer->insert(buffer->end(), data.begin(), data.end());

    while (buffer->size() >= sizeof(uint32_t))
    {
        uint32_t length;
        std::memcpy(&length, buffer->data(), sizeof(uint32_t));
        length = boost::endian::big_to_native(length);

        if (buffer->size() < sizeof(uint32_t) + length)
        {
            break;
        }

        std::vector<uint8_t> request(buffer->begin() + sizeof(uint32_t), buffer->begin() + sizeof(uint32_t) + length);
        buffer->erase(buffer->begin(), buffer->begin() + sizeof(uint32_t) + length);

        std::vector<uint8_t> response;
        try
        {
            response = handle_request(request);
        }
        catch (const std::exception& e)
        {
            std::string error_response = nlohmann::json{ {"error", e.what()} }.dump();
            response = std::vector<uint8_t>(error_response.begin(), error_response.end());
        }

        uint32_t response_length = boost::endian::native_to_big(static_cast<uint32_t>(response.size()));
        std::vector<uint8_t> framed_response(sizeof(uint32_t) + response.size());
        std::memcpy(framed_response.data(), &response_length, sizeof(uint32_t));
        std::memcpy(framed_response.data() + sizeof(uint32_t), response.data(), response.size());

        send(connection, std::move(framed_response));
    }
}

void tcp_serial_port_server::on_client_disconnected(const tcp_client_connection& connection)
{
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    buffers_.erase(connection.id);
}

std::vector<uint8_t> tcp_serial_port_server::handle_request(const std::vector<uint8_t>& data)
{
    serial_port_base& serial_port = serial_port_->get();

    std::lock_guard<std::mutex> lock(serial_port_mutex_);

    std::string data_str(data.begin(), data.end());

    nlohmann::json request = nlohmann::json::parse(data_str);

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

    return std::vector<uint8_t>(response_string.begin(), response_string.end());
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
#if defined(__linux__) || defined(__APPLE__)
    void* handle = nullptr;
#endif // __linux__ || defined(__APPLE__)
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
        throw io_exception("Library already loaded", io_error::invalid_state);
    }

#if WIN32

    pimpl_->handle = LoadLibraryA(library_path.c_str());

    if (pimpl_->handle == nullptr)
    {
        throw io_exception("Failed to load library: " + library_path, io_error::load_failed);
    }

    init_fptr_ = reinterpret_cast<init_fptr>(GetProcAddress(pimpl_->handle, "init"));
    uninit_fptr_ = reinterpret_cast<uninit_fptr>(GetProcAddress(pimpl_->handle, "uninit"));
    set_ptt_fptr_ = reinterpret_cast<set_ptt_fptr>(GetProcAddress(pimpl_->handle, "set_ptt"));
    get_ptt_fptr_ = reinterpret_cast<get_ptt_fptr>(GetProcAddress(pimpl_->handle, "get_ptt"));

#endif // WIN32

#if defined(__linux__) || defined(__APPLE__)

    pimpl_->handle = dlopen(library_path.c_str(), RTLD_NOW);

    if (pimpl_->handle == nullptr)
    {
        throw io_exception("Failed to load library: " + library_path, io_error::load_failed);
    }

    init_fptr_ = reinterpret_cast<init_fptr>(dlsym(pimpl_->handle, "init"));
    uninit_fptr_ = reinterpret_cast<uninit_fptr>(dlsym(pimpl_->handle, "uninit"));
    set_ptt_fptr_ = reinterpret_cast<set_ptt_fptr>(dlsym(pimpl_->handle, "set_ptt"));
    get_ptt_fptr_ = reinterpret_cast<get_ptt_fptr>(dlsym(pimpl_->handle, "get_ptt"));

#endif // __linux__ || defined(__APPLE__)

    loaded_ = true;

    if (set_ptt_fptr_ == nullptr || get_ptt_fptr_ == nullptr)
    {
        throw io_exception("Failed to resolve PTT functions", io_error::load_failed);
    }

    if (init_fptr_ != nullptr)
    {
        if (init_fptr_(context) != 0)
        {
            throw io_exception("Library init failed", io_error::load_failed);
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
#if defined(__linux__) || defined(__APPLE__)
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
            throw io_exception("Library uninit failed", io_error::io_error);
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
// tcp_ptt_control_client                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

void tcp_ptt_control_client::ptt(bool ptt_state)
{
    // Send request to set PTT
    //
    // Request: { "command": "set_ptt", "value": <bool> }
    // Response: { "value": "ok" }

    json_request(*this, { {"command", "set_ptt"}, {"value", ptt_state} });
}

bool tcp_ptt_control_client::ptt()
{
    // Send request to get PTT state
    //
    // Request: { "command": "get_ptt" }
    // Response: { "value": "<bool>" }

    return json_request(*this, { {"command", "get_ptt"} })["value"].get<bool>();
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

tcp_ptt_control_server::~tcp_ptt_control_server()
{
    stop();
}

bool tcp_ptt_control_server::start(const std::string& host, int port)
{
    if (!ptt_callable_)
    {
        return false;
    }

    return tcp_server_base::start(host, port);
}

void tcp_ptt_control_server::on_data_received(const tcp_client_connection& connection, const std::vector<uint8_t>& data)
{
    std::lock_guard<std::mutex> lock(buffers_mutex_);

    auto& buffer = buffers_[connection.id];
    buffer.insert(buffer.end(), data.begin(), data.end());

    while (buffer.size() >= sizeof(uint32_t))
    {
        uint32_t length;
        std::memcpy(&length, buffer.data(), sizeof(uint32_t));
        length = boost::endian::big_to_native(length);

        if (buffer.size() < sizeof(uint32_t) + length)
        {
            break;
        }

        std::vector<uint8_t> request(buffer.begin() + sizeof(uint32_t), buffer.begin() + sizeof(uint32_t) + length);
        buffer.erase(buffer.begin(), buffer.begin() + sizeof(uint32_t) + length);

        std::vector<uint8_t> response;
        try
        {
            response = handle_request(request);
        }
        catch (const std::exception& e)
        {
            std::string error_response = nlohmann::json{ {"error", e.what()} }.dump();
            response = std::vector<uint8_t>(error_response.begin(), error_response.end());
        }

        uint32_t response_length = boost::endian::native_to_big(static_cast<uint32_t>(response.size()));
        std::vector<uint8_t> framed_response(sizeof(uint32_t) + response.size());
        std::memcpy(framed_response.data(), &response_length, sizeof(uint32_t));
        std::memcpy(framed_response.data() + sizeof(uint32_t), response.data(), response.size());

        send(connection, std::move(framed_response));
    }
}

void tcp_ptt_control_server::on_client_disconnected(const tcp_client_connection& connection)
{
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    buffers_.erase(connection.id);
}

std::vector<uint8_t> tcp_ptt_control_server::handle_request(const std::vector<uint8_t>& data)
{
    std::string data_str(data.begin(), data.end());

    nlohmann::json request = nlohmann::json::parse(data_str);

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

    return std::vector<uint8_t>(response_string.begin(), response_string.end());
}

LIBMODEM_NAMESPACE_END