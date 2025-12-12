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

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#endif

// **************************************************************** //
//                                                                  //
//                                                                  //
// serial_port                                                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

serial_port::serial_port() : io_context_(), serial_port_(io_context_), is_open_(false)
{
}

serial_port::~serial_port()
{
    close();
}

bool serial_port::open(const std::string& port_name, unsigned int baud_rate, unsigned int data_bits, boost::asio::serial_port_base::parity::type parity, boost::asio::serial_port_base::stop_bits::type stop_bits, boost::asio::serial_port_base::flow_control::type flow_control)
{
    if (is_open_)
    {
		return false; // Already open
	}

    try
    {
        // Open the serial port
        serial_port_.open(port_name);

        // Configure serial port options
        serial_port_.set_option(boost::asio::serial_port_base::baud_rate(baud_rate));
        serial_port_.set_option(boost::asio::serial_port_base::character_size(data_bits));
        serial_port_.set_option(boost::asio::serial_port_base::parity(parity));
        serial_port_.set_option(boost::asio::serial_port_base::stop_bits(stop_bits));
        serial_port_.set_option(boost::asio::serial_port_base::flow_control(flow_control));

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
    if (is_open_)
    {
        is_open_ = false;
        serial_port_.close();
    }
}

void serial_port::rts(bool enable)
{
    if (!is_open_) return;

    try
    {
#ifdef _WIN32
        if (enable)
        {
            ::EscapeCommFunction(serial_port_.native_handle(), SETRTS);
        }
        else
        {
            ::EscapeCommFunction(serial_port_.native_handle(), CLRRTS);
        }
#else
        int status;
        ::ioctl(serial_port_.native_handle(), TIOCMGET, &status);
        if (enable)
        {
            status |= TIOCM_RTS;
        }
        else
        {
            status &= ~TIOCM_RTS;
        }
        ::ioctl(serial_port_.native_handle(), TIOCMSET, &status);
#endif
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
#ifdef _WIN32
        DWORD status;
        ::GetCommModemStatus(serial_port_.native_handle(), &status);
        // Note: GetCommModemStatus doesn't return RTS state directly
        // RTS is an output signal we control, not an input we read
        // You might need to track this state separately if needed
        return false; // Or maintain internal state
#else
        int status;
        ::ioctl(serial_port_.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_RTS) != 0;
#endif
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
#ifdef _WIN32
        if (enable)
        {
            ::EscapeCommFunction(serial_port_.native_handle(), SETDTR);
        }
        else
        {
            ::EscapeCommFunction(serial_port_.native_handle(), CLRDTR);
        }
#else
        int status;
        ::ioctl(serial_port_.native_handle(), TIOCMGET, &status);
        if (enable)
        {
            status |= TIOCM_DTR;
        }
        else
        {
            status &= ~TIOCM_DTR;
        }
        ::ioctl(serial_port_.native_handle(), TIOCMSET, &status);
#endif
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
#ifdef _WIN32
        DWORD status;
        ::GetCommModemStatus(serial_port_.native_handle(), &status);
        // Note: GetCommModemStatus doesn't return DTR state directly
        // DTR is an output signal we control, not an input we read
        // You might need to track this state separately if needed
        return false; // Or maintain internal state
#else
        int status;
        ::ioctl(serial_port_.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_DTR) != 0;
#endif
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
#ifdef _WIN32
        DWORD status;
        ::GetCommModemStatus(serial_port_.native_handle(), &status);
        return (status & MS_CTS_ON) != 0;
#else
        int status;
        ::ioctl(serial_port_.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_CTS) != 0;
#endif
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
#ifdef _WIN32
        DWORD status;
        ::GetCommModemStatus(serial_port_.native_handle(), &status);
        return (status & MS_DSR_ON) != 0;
#else
        int status;
        ::ioctl(serial_port_.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_DSR) != 0;
#endif
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
#ifdef _WIN32
        DWORD status;
        ::GetCommModemStatus(serial_port_.native_handle(), &status);
        return (status & MS_RLSD_ON) != 0;
#else
        int status;
        ::ioctl(serial_port_.native_handle(), TIOCMGET, &status);
        return (status & TIOCM_CAR) != 0;
#endif
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
        return boost::asio::write(serial_port_, boost::asio::buffer(data));
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
        return boost::asio::write(serial_port_, boost::asio::buffer(data));
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
        std::size_t bytes_read = boost::asio::read(serial_port_,
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
        std::size_t bytes_read = serial_port_.read_some(boost::asio::buffer(buffer));
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
        boost::asio::read_until(serial_port_, buffer, delimiter);

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
#ifdef _WIN32
        COMSTAT comstat;
        DWORD errors;
        if (::ClearCommError(serial_port_.native_handle(), &errors, &comstat))
        {
            return comstat.cbInQue;
        }
        return 0;
#else
        int bytes_available = 0;
        ::ioctl(serial_port_.native_handle(), FIONREAD, &bytes_available);
        return bytes_available;
#endif
    }
    catch (...)
    {
        return 0;
    }
}

void serial_port::flush()
{
    if (!is_open_) return;

#ifdef _WIN32
    ::FlushFileBuffers(serial_port_.native_handle());
#else
    ::tcflush(serial_port_.native_handle(), TCIOFLUSH);
#endif
}

void serial_port::timeout(unsigned int milliseconds)
{
    if (!is_open_) return;

#ifdef _WIN32
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = milliseconds;
    timeouts.ReadTotalTimeoutConstant = milliseconds;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = milliseconds;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    ::SetCommTimeouts(serial_port_.native_handle(), &timeouts);
#else
    struct termios tty;
    ::tcgetattr(serial_port_.native_handle(), &tty);
    tty.c_cc[VTIME] = milliseconds / 100;  // Convert to deciseconds
    tty.c_cc[VMIN] = 0;
    ::tcsetattr(serial_port_.native_handle(), TCSANOW, &tty);
#endif
}
