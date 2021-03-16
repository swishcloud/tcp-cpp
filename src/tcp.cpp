#include <tcp.h>
namespace GLOBAL_NAMESPACE_NAME
{
    tcp_server::tcp_server(short port) : port(port), accecption_times{0}, buffer{new char[buffer_size]}, session_count_peak{0}
    {
        memset(this->buffer.get(), 0, buffer_size);
    }
    tcp_server::~tcp_server()
    {
        common::print_debug(common::string_format("waiting heartbeat thread to exit..."));
        this->heartbeat_thread.join();
    }
    void tcp_server::accecpt(tcp::acceptor &acceptor)
    {
        acceptor.async_accept(
            [&acceptor, this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec)
                {
                    tcp_session *session = new tcp_session{std::move(socket), this->buffer};
                    this->add_session(session);
                    if (this->on_accepted)
                    {
                        this->on_accepted(session, this);
                    }
                }
                else
                {
                    common::print_debug(common::string_format("async_accept failed:%s", ec.message().c_str()));
                }
                accecption_times++;
                common::print_debug(common::string_format("async_accept %s on the %dth attempt", (ec ? "failed" : "ok"), this->accecption_times));
                this->accecpt(acceptor);
            });
    }
    void tcp_server::listen()
    {
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(boost::asio::ip::address::from_string("0.0.0.0"), port));
        this->accecpt(acceptor);
        auto work = boost::asio::require(io_context.get_executor(), boost::asio::execution::outstanding_work.tracked);
        heartbeat_thread = std::thread([this]() {
            while (1)
            {
                std::this_thread::sleep_for(std::chrono::seconds{1});
                for (int i = 0; i < this->sessions.size(); i++)
                {
                    auto session = this->sessions[i];
                    time_t timer;

                    time(&timer);
                    double seconds = difftime(timer, session->last_read_timer);
                    if (session->read_size > 0 && seconds > 1 * 60 * 3)
                    {
                        //remove this session
                        this->remove_session(i--);
                    }
                }
            }
        });
        std::vector<std::thread> threads;
        for (int i = 0; i <= 50; i++)
        {
            threads.push_back(std::move(std::thread([&io_context]() {
                io_context.run();
            })));
        }
        for (auto &t : threads)
        {
            t.join();
        }
    }
    void tcp_server::add_session(tcp_session *session)
    {
        std::lock_guard<std::mutex> guard(sessions_mtx);
        this->sessions.push_back(session);
        if (this->session_count_peak < this->sessions.size())
        {
            this->session_count_peak = this->sessions.size();
        }
    }
    void tcp_server::remove_session(int index)
    {
        std::lock_guard<std::mutex> guard(sessions_mtx);
        delete this->sessions[index];
        this->sessions.erase(this->sessions.begin() + index);
    }
    //begin tcp_client
    void tcp_client::start(std::string server_ip, std::string server_port)
    {
        try
        {
            tcp::resolver r(session.io_context);
            this->endpoints = r.resolve(server_ip, server_port);
            this->connect(endpoints.begin());
            auto work = boost::asio::require(session.io_context.get_executor(), boost::asio::execution::outstanding_work.tracked);
            this->client_thread = std::thread([this, work]() {
                this->session.io_context.run();
                common::print_debug("A tcp_client thread terminated.");
            });
        }
        catch (const std::exception &e)
        {
            common::print_debug(e.what());
            if (this->on_connect_fail)
            {
                on_connect_fail(this);
            }
        }
    }
    void tcp_client::connect(tcp::resolver::results_type::iterator endpoint_iter)
    {
        if (endpoint_iter == this->endpoints.end())
        {
            common::print_debug("no more endpoint for connection");
            if (this->on_connect_fail)
            {
                on_connect_fail(this);
            }
            return;
        }
        session.socket.async_connect(endpoint_iter->endpoint(), std::bind(&tcp_client::handle_connect, this, _1, endpoint_iter));
    }
    void tcp_client::handle_connect(const boost::system::error_code &error, tcp::resolver::results_type::iterator endpoint_iter)
    {
        if (error)
        {
            common::print_debug(common::string_format("connection failed:%s", error.message().c_str()));
            connect(++endpoint_iter);
            return;
        }
        //connection suceess
        connected = true;
        if (this->on_connect_success)
            on_connect_success(this);
    }
    tcp_client::tcp_client() : connected{false}
    {
    }
    tcp_client::~tcp_client()
    {
        this->session.socket.close();
        this->session.io_context.stop();
        this->client_thread.join();
    }

    tcp_session::tcp_session() : io_context{}, socket{io_context}, buffer{new char[buffer_size]}, read_size{0}, data{NULL}
    {
        memset(this->buffer.get(), 0, buffer_size);
    }
    tcp_session::tcp_session(tcp::socket socket, std::shared_ptr<char[]> buf) : socket{std::move(socket)}, buffer{new char[buffer_size]}, read_size{0}
    {
    }
    void tcp_session::read(size_t size, read_handler on_read, void *p)
    {
        this->socket.async_read_some(boost::asio::buffer(buffer.get(), size > buffer_size ? buffer_size : size), [this, size, on_read, p](const boost::system::error_code &error, std::size_t bytes_transferred) {
            time(&this->last_read_timer);
            this->read_size += bytes_transferred;

            on_read(bytes_transferred, this, size == bytes_transferred, error ? error.message().c_str() : NULL, p);
            if (!error && size > bytes_transferred)
            {
                this->read(size - bytes_transferred, on_read, p);
            }
        });
    }
    void tcp_session::send_stream(std::shared_ptr<std::istream> fs, sent_stream_handler on_sent_stream, void *p)
    {
        static const int BUFFER_SIZE = 1 * 1024 * 1024;
        std::shared_ptr<char[]> buf{new char[BUFFER_SIZE]};
        fs->read(buf.get(), BUFFER_SIZE);
        if (fs->rdstate() & (std::ios_base::badbit)) //failed to read bytes
        {
            throw common::exception("failed to read bytes");
        }
        int read_count = fs->gcount();
        this->write(
            buf.get(), read_count, [this, fs, on_sent_stream, buf](size_t written_size, XTCP::tcp_session *session, bool completed, const char *error, void *p) {
                bool eof = fs->rdstate() & (std::ios_base::eofbit);
                on_sent_stream(written_size, session, eof, error, p);
                if (completed)
                {
                    if (!eof)
                        send_stream(fs, on_sent_stream, p);
                }
            },
            NULL);
    }
    void tcp_session::receive_stream(std::shared_ptr<std::ostream> fs, size_t size, received_stream_handler on_received_stream, void *p)
    {
        std::shared_ptr<int> written{new int{}};
        this->read(
            size, [written, size, fs, on_received_stream](size_t read_size, XTCP::tcp_session *session, bool completed, const char *error, void *p) {
                *written += read_size;
                fs->write(session->buffer.get(), read_size);
                if (!(fs.get()))
                {
                    on_received_stream(read_size, session, false, "Writing failed.", p);
                    return;
                }
                common::print_debug(common::string_format("read %d/%d bytes file content from upstream", *written.get(), size));
                on_received_stream(read_size, session, completed, error, p);
            },
            NULL);
    }
    void tcp_session::close()
    {
        if (on_closed)
        {
            on_closed(this);
        }
        this->socket.close();
    }
    void tcp_session::write(const char *data, size_t size, written_handler on_written, void *p)
    {
        common::print_debug("writing...");
        this->socket.async_write_some(boost::asio::buffer(data, size), [this, data, size, on_written, p](const boost::system::error_code &error, std::size_t bytes_transferred) {
            common::print_debug("writting callback called.");
            time(&this->last_write_timer);
            this->written_size += bytes_transferred;

            on_written(bytes_transferred, this, size == bytes_transferred, error ? error.message().c_str() : NULL, p);
            if (!error && size != bytes_transferred)
            {
                this->write(data + bytes_transferred, size - bytes_transferred, on_written, p);
                return;
            }
        });
    }
};