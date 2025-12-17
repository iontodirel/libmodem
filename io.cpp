// **************************************************************** //
// modem - APRS modem                                               // 
// Version 0.1.0                                                    //
// https://github.com/iontodirel/modem                              //
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

#ifdef WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#endif // WIN32

#if __linux__
#include <sys/ioctl.h>
#include <termios.h>
#endif // __linux__

#include <boost/asio.hpp>
#include <boost/asio/serial_port.hpp>

LIBMODEM_NAMESPACE_BEGIN

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
        return false; // Already open
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
            case parity::none: asio_parity = boost::asio::serial_port_base::parity::none; break;
            case parity::odd:  asio_parity = boost::asio::serial_port_base::parity::odd; break;
            case parity::even: asio_parity = boost::asio::serial_port_base::parity::even; break;
        }
        impl_->serial_port.set_option(boost::asio::serial_port_base::parity(asio_parity));

        // Convert stop_bits enum
        boost::asio::serial_port_base::stop_bits::type asio_stop_bits;
        switch (stop_bits)
        {
            case stop_bits::one:          asio_stop_bits = boost::asio::serial_port_base::stop_bits::one; break;
            case stop_bits::onepointfive: asio_stop_bits = boost::asio::serial_port_base::stop_bits::onepointfive; break;
            case stop_bits::two:          asio_stop_bits = boost::asio::serial_port_base::stop_bits::two; break;
        }
        impl_->serial_port.set_option(boost::asio::serial_port_base::stop_bits(asio_stop_bits));

        // Convert flow_control enum
        boost::asio::serial_port_base::flow_control::type asio_flow_control;
        switch (flow_control)
        {
            case flow_control::none:     asio_flow_control = boost::asio::serial_port_base::flow_control::none; break;
            case flow_control::software: asio_flow_control = boost::asio::serial_port_base::flow_control::software; break;
            case flow_control::hardware: asio_flow_control = boost::asio::serial_port_base::flow_control::hardware; break;
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
    if (!is_open_) return;

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
    if (!is_open_) return false;

    try
    {
#ifdef WIN32
        DWORD status;
        ::GetCommModemStatus(impl_->serial_port.native_handle(), &status);
        // Note: GetCommModemStatus doesn't return RTS state directly
        // RTS is an output signal we control, not an input we read
        // You might need to track this state separately if needed
        return false; // Or maintain internal state
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
    if (!is_open_) return;

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
    if (!is_open_) return false;

    try
    {
#ifdef WIN32
        DWORD status;
        ::GetCommModemStatus(impl_->serial_port.native_handle(), &status);
        // Note: GetCommModemStatus doesn't return DTR state directly
        // DTR is an output signal we control, not an input we read
        // You might need to track this state separately if needed
        return false; // Or maintain internal state
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
    if (!is_open_) return false;

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
    if (!is_open_) return false;

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
    if (!is_open_) return false;

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
    if (!is_open_) return 0;

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
    if (!is_open_) return 0;

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
    std::vector<uint8_t> buffer(size);
    if (!is_open_) return buffer;

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
    std::vector<uint8_t> buffer(max_size);
    if (!is_open_) return buffer;

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
    if (!is_open_) return "";

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

bool serial_port::is_open() const
{
    return is_open_;
}

std::size_t serial_port::bytes_available()
{
    if (!is_open_) return 0;

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
    if (!is_open_) return;

#ifdef WIN32
    ::FlushFileBuffers(impl_->serial_port.native_handle());
#endif // WIN32
#ifdef __linux__
    ::tcflush(impl_->serial_port.native_handle(), TCIOFLUSH);
#endif // __linux__
}

void serial_port::timeout(unsigned int milliseconds)
{
    if (!is_open_) return;

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

LIBMODEM_NAMESPACE_END