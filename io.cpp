#include "io.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#endif

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

tcp_server::tcp_server(unsigned short port, size_t thread_pool_size)
    : port_(port)
    , thread_pool_size_(thread_pool_size)
    , is_running_(false)
    , next_client_id_(1)
{
}

tcp_server::~tcp_server()
{
    stop();
}

void tcp_server::start()
{
    if (is_running_) return;

    is_running_ = true;

    // Create io_context
    io_context_ = std::make_unique<boost::asio::io_context>();

    // Create work guard to keep io_context running
    work_guard_ = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
        boost::asio::make_work_guard(*io_context_)
    );

    // Create acceptor
    acceptor_ = std::make_unique<boost::asio::ip::tcp::acceptor>(
        *io_context_,
        boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port_)
    );

    // Start accepting connections
    do_accept();

    // Start thread pool
    for (size_t i = 0; i < thread_pool_size_; ++i)
    {
        thread_pool_.emplace_back([this] { run_io_context(); });
    }
}

void tcp_server::stop()
{
    if (!is_running_) return;

    is_running_ = false;

    // Close all client connections
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& [id, session] : clients_)
        {
            session->close();
        }
        clients_.clear();
    }

    // Stop accepting new connections
    if (acceptor_ && acceptor_->is_open())
    {
        acceptor_->close();
    }

    // Stop work guard to allow io_context to finish
    work_guard_.reset();

    // Stop io_context
    if (io_context_)
    {
        io_context_->stop();
    }

    // Join all threads
    for (auto& thread : thread_pool_)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    thread_pool_.clear();

    // Clean up
    acceptor_.reset();
    io_context_.reset();
}

bool tcp_server::is_running() const
{
    return is_running_;
}

void tcp_server::set_message_handler(message_handler_t handler)
{
    message_handler_ = handler;
}

void tcp_server::set_connection_handler(connection_handler_t handler)
{
    connection_handler_ = handler;
}

void tcp_server::set_disconnect_handler(disconnect_handler_t handler)
{
    disconnect_handler_ = handler;
}

void tcp_server::send_to_client(std::size_t id, const std::string& message)
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = clients_.find(id);
    if (it != clients_.end())
    {
        it->second->send(message);
    }
}

void tcp_server::broadcast(const std::string& message)
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& [id, session] : clients_)
    {
        session->send(message);
    }
}

void tcp_server::disconnect_client(std::size_t id)
{
    std::shared_ptr<client_session> session;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = clients_.find(id);
        if (it != clients_.end())
        {
            session = it->second;
            clients_.erase(it);
        }
    }

    if (session)
    {
        session->close();
    }
}

size_t tcp_server::get_client_count() const
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.size();
}

unsigned short tcp_server::get_port() const
{
    return port_;
}

std::vector<size_t> tcp_server::get_connected_clients() const
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::vector<std::size_t> ids;
    for (const auto& [id, session] : clients_)
    {
        ids.push_back(id);
    }
    return ids;
}

void tcp_server::run_io_context()
{
    while (is_running_)
    {
        try
        {
            io_context_->run();
            break; // Normal exit when work is done
        }
        catch (const std::exception& e)
        {
            assert(false);
        }
    }
}

void tcp_server::do_accept()
{
    if (!is_running_ || !acceptor_ || !acceptor_->is_open()) return;

    std::size_t id = next_client_id_++;
    auto new_session = std::make_shared<client_session>(this, id, *io_context_);

    acceptor_->async_accept(new_session->get_socket(),
        [this, new_session](const boost::system::error_code& error) {
            handle_new_connection(new_session, error);
        });
}

void tcp_server::handle_new_connection(std::shared_ptr<client_session> session, const boost::system::error_code& error)
{
    if (!error && is_running_)
    {
        std::string remote_address = session->get_remote_address();
        std::size_t id = session->get_id();

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_[id] = session;
        }

        // Call connection handler if set
        if (connection_handler_)
        {
            connection_handler_(id, remote_address);
        }

        // Start the session
        session->start();

        // Accept next connection
        do_accept();
    }
}

void tcp_server::handle_client_message(std::size_t id, const std::string& message)
{
    if (message_handler_)
    {
        message_handler_(id, message);
    }
    else
    {
        // Default echo behavior
        send_to_client(id, message);
    }
}

void tcp_server::handle_client_disconnect(std::size_t id)
{
    // Don't lock here - just remove from map
    // The session is already disconnecting

    if (disconnect_handler_)
    {
        disconnect_handler_(id);
    }
}

tcp_server::client_session::client_session(tcp_server* server, std::size_t id, boost::asio::io_context& io_context)
    : server_(server)
    , id_(id)
    , socket_(io_context)
    , writing_(false)
{
}

boost::asio::ip::tcp::socket& tcp_server::client_session::get_socket()
{
    return socket_;
}

size_t tcp_server::client_session::get_id() const
{
    return id_;
}

std::string tcp_server::client_session::get_remote_address() const
{
    try
    {
        auto endpoint = socket_.remote_endpoint();
        return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
    }
    catch (...)
    {
        return "unknown";
    }
}

void tcp_server::client_session::start()
{
    do_read();
}

void tcp_server::client_session::send(const std::string& message)
{
    bool should_write = false;
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        bool write_in_progress = !write_queue_.empty();
        write_queue_.push(message);
        should_write = !write_in_progress;
    }

    if (should_write)
    {
        do_write();  // Call without holding the lock
    }
}

void tcp_server::client_session::close()
{
    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
}

void tcp_server::client_session::do_read()
{
    auto self = shared_from_this();
    boost::asio::async_read_until(socket_, read_buffer_, '\n',
        [this, self](const boost::system::error_code& error, size_t bytes_transferred) {
            if (!error)
            {
                std::istream is(&read_buffer_);
                std::string line;
                std::getline(is, line);

                // Remove carriage return if present
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }

                // Handle the message
                server_->handle_client_message(id_, line);

                // Continue reading
                do_read();
            }
            else
            {
                handle_disconnect();
            }
        });
}

void tcp_server::client_session::do_write()
{
    auto self = shared_from_this();

    std::lock_guard<std::mutex> lock(write_mutex_);
    if (write_queue_.empty()) return;

    boost::asio::async_write(socket_, boost::asio::buffer(write_queue_.front()),
        [this, self](const boost::system::error_code& error, size_t bytes_transferred) {
            if (!error)
            {
                bool should_continue = false;
                {
                    std::lock_guard<std::mutex> lock(write_mutex_);
                    write_queue_.pop();
                    should_continue = !write_queue_.empty();
                }

                if (should_continue)
                {
                    do_write();  // Call without holding the lock
                }
            }
            else
            {
                handle_disconnect();
            }
        });
}

void tcp_server::client_session::handle_disconnect()
{
    // Remove from clients map first
    {
        std::lock_guard<std::mutex> lock(server_->clients_mutex_);
        server_->clients_.erase(id_);
    }

    // Then notify server
    server_->handle_client_disconnect(id_);

    // Close socket
    close();
}