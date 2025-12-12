#pragma once

#include <string>
#include <vector>
#include <queue>
#include <map>

#include <boost/asio.hpp>
#include <boost/asio/serial_port.hpp>

class serial_port
{
public:
    serial_port();
    ~serial_port();

    bool open(const std::string& port_name,
        unsigned int baud_rate = 9600,
        unsigned int data_bits = 8,
        boost::asio::serial_port_base::parity::type parity = boost::asio::serial_port_base::parity::none,
        boost::asio::serial_port_base::stop_bits::type stop_bits = boost::asio::serial_port_base::stop_bits::one,
        boost::asio::serial_port_base::flow_control::type flow_control = boost::asio::serial_port_base::flow_control::none);

    void close();

    void rts(bool enable);
    bool rts();
    void dtr(bool enable);
    bool dtr();
    bool cts();
    bool dsr();
    bool dcd();

    std::size_t write(const std::vector<uint8_t>& data);
    std::size_t write(const std::string& data);
    std::vector<uint8_t> read(std::size_t size);
    std::vector<uint8_t> read_some(std::size_t max_size);
    std::string read_until(const std::string& delimiter);

    bool is_open() const;
    std::size_t bytes_available();
    void flush();
    void timeout(unsigned int milliseconds);

private:
    boost::asio::io_context io_context_;
    boost::asio::serial_port serial_port_;
    bool is_open_;
};

class tcp_server
{
public:
    using message_handler_t = std::function<void(std::size_t, const std::string&)>;
    using connection_handler_t = std::function<void(std::size_t, const std::string&)>;
    using disconnect_handler_t = std::function<void(std::size_t)>;

    tcp_server(unsigned short port, size_t thread_pool_size = 4);
    ~tcp_server();

    void start();
    void stop();
    bool is_running() const;

    void set_message_handler(message_handler_t handler);
    void set_connection_handler(connection_handler_t handler);
    void set_disconnect_handler(disconnect_handler_t handler);

    void send_to_client(std::size_t id, const std::string& message);
    void broadcast(const std::string& message);
    void disconnect_client(std::size_t id);

    size_t get_client_count() const;
    unsigned short get_port() const;
    std::vector<std::size_t> get_connected_clients() const;

private:
    class client_session : public std::enable_shared_from_this<client_session>
    {
    public:
        client_session(tcp_server* server, std::size_t id, boost::asio::io_context& io_context);

        boost::asio::ip::tcp::socket& get_socket();
        std::size_t get_id() const;
        std::string get_remote_address() const;

        void start();
        void send(const std::string& message);
        void close();

    private:
        void do_read();
        void do_write();
        void handle_disconnect();

        tcp_server* server_;
        std::size_t id_;
        boost::asio::ip::tcp::socket socket_;
        boost::asio::streambuf read_buffer_;
        std::queue<std::string> write_queue_;
        std::mutex write_mutex_;
        bool writing_;
    };

    void run_io_context();
    void do_accept();
    void handle_new_connection(std::shared_ptr<client_session> session, const boost::system::error_code& error);
    void handle_client_message(std::size_t id, const std::string& message);
    void handle_client_disconnect(std::size_t id);

    unsigned short port_;
    size_t thread_pool_size_;
    std::atomic<bool> is_running_;
    std::atomic<std::size_t> next_client_id_;

    std::unique_ptr<boost::asio::io_context> io_context_;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_guard_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;

    std::vector<std::thread> thread_pool_;
    std::map<std::size_t, std::shared_ptr<client_session>> clients_;

    message_handler_t message_handler_;
    connection_handler_t connection_handler_;
    disconnect_handler_t disconnect_handler_;

    mutable std::mutex clients_mutex_;
};
