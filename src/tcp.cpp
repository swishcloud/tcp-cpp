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
    void _receive_size(XTCP::tcp_session *tcp_session, std::shared_ptr<std::stringstream> size_ss, std::function<void(bool success, message &msg)> on_read);
    void _receive_message(XTCP::tcp_session *tcp_session, std::shared_ptr<std::stringstream> msg_ss, size_t size, std::function<void(bool success, message &msg)> on_read);

    char *message::to_json() const
    {
        nlohmann::json j;
        for (auto header : this->headers)
        {
            header.fill_json(j["Header"]);
        }
        j["MsgType"] = this->msg_type;
        j["BodySize"] = this->body_size;
        std::string json_str = j.dump();
        return common::strcpy(json_str.c_str());
    }
    message::operator bool() const
    {
        return this->msg_type > 0;
    }
    message message::parse(std::string json)
    {
        nlohmann::json j = nlohmann::json::parse(json);
        message msg;
        msg.msg_type = j["MsgType"].get<int>();
        msg.body_size = j["BodySize"].get<long>();
        for (auto &header : j["Header"].items())
        {
            nlohmann::json val = header.value();
            if (val.is_number())
            {
                msg.addHeader({header.key(), static_cast<size_t>(val)});
            }
            else
            {
                msg.addHeader({header.key(), static_cast<std::string>(val)});
            }
        }
        return msg;
    }
    void message::addHeader(message_header value)
    {
        headers.push_back(value);
    }
    message_header::message_header(std::string name, std::string v)
    {
        this->name = name;
        t = 0;
        this->str_v = v;
    }
    message_header::message_header(std::string name, size_t v)
    {
        this->name = name;
        t = 1;
        this->int_v = v;
    }
    void message_header::fill_json(json &j)
    {
        if (this->t == 0)
            j[this->name] = this->str_v;
        else if (this->t == 1)
            j[this->name] = this->int_v;
    }
    void send_message(XTCP::tcp_session *session, message &msg, std::function<void(bool success)> on_sent)
    {
        auto json = std::unique_ptr<char[]>{msg.to_json()};
        int json_len = strlen(json.get());
        std::string json_len_str = common::string_format("%x", json_len);
        int buf_len = json_len_str.size() + 1 + json_len;
        std::shared_ptr<char[]> buf{new char[buf_len]};
        char *dest = buf.get();
        memcpy(dest, json_len_str.c_str(), json_len_str.size());
        dest += json_len_str.size();
        memcpy(dest++, "\0", 1);
        memcpy(dest, json.get(), json_len);

        std::promise<std::string> promise;
        session->write(
            buf.get(), buf_len, [buf, on_sent, &promise](size_t read_size, XTCP::tcp_session *session, bool completed, const char *error, void *p) {
                if (error)
                {
                    session->close();
                }
                if ((completed || error))
                {
                    if (on_sent)
                        on_sent(!error);
                    else
                        promise.set_value(error ? error : "");
                }
            },
            NULL);
        if (!on_sent)
        {
            auto err = promise.get_future().get();
            if (!err.empty())
            {
                throw common::exception(err);
            }
        }
    }
    void read_message(XTCP::tcp_session *session, message &msg, std::function<void(bool success, message &msg)> on_read)
    {
        std::shared_ptr<std::stringstream> size_ss{new std::stringstream{}};
        *size_ss << std::hex;
        std::shared_ptr<std::promise<message>> promise{new std::promise<message>{}};
        _receive_size(session, size_ss, [on_read, session, promise](bool success, message &msg) {
            promise->set_value(msg);
            if (!success)
            {
                session->close();
            }
            if (on_read)
            {
                on_read(success, msg);
            }
        });
        if (!on_read)
        {
            msg = promise->get_future().get();
        }
    }
    void _receive_size(XTCP::tcp_session *tcp_session, std::shared_ptr<std::stringstream> size_ss, std::function<void(bool success, message &msg)> on_read)
    {
        tcp_session->read(
            1, [on_read, size_ss](size_t read_size, XTCP::tcp_session *session, bool completed, const char *error, void *p) {
                if (error)
                {
                    message msg;
                    on_read(false, msg);
                    return;
                }
                *size_ss << session->buffer.get()[0];
                if (session->buffer.get()[0] == '\0')
                {
                    int size;
                    *size_ss >> size;
                    common::print_debug(common::string_format("read message SIZE:%d", size));
                    std::shared_ptr<std::stringstream> msg_ss{new std::stringstream{}};
                    _receive_message(session, msg_ss, size, on_read);
                    return;
                }
                else
                {
                    assert(completed); //just one byte.
                    _receive_size(session, size_ss, on_read);
                }
            },
            NULL);
    }
    void _receive_message(XTCP::tcp_session *tcp_session, std::shared_ptr<std::stringstream> msg_ss, size_t size, std::function<void(bool success, message &msg)> on_read)
    {
        tcp_session->read(
            size, [msg_ss, on_read](size_t read_size, XTCP::tcp_session *session, bool completed, const char *error, void *p) {
                std::unique_ptr<char[]> msg_content = std::unique_ptr<char[]>{common::strcpy(session->buffer.get(), read_size)};
                *msg_ss << msg_content.get();

                if (completed)
                {
                    common::print_debug(common::string_format("read message:%s", msg_ss->str().c_str()));
                }
                message msg;
                try
                {
                    msg = message::parse(msg_content.get());
                }
                catch (const std::exception &e)
                {
                    common::print_debug(common::string_format("error reading message:%s", e.what()));
                    on_read(e.what(), msg);
                }

                if (error || completed)
                {
                    on_read(!error, msg);
                }
            },
            NULL);
    }
};