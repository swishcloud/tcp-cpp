#include <tcp.h>
namespace GLOBAL_NAMESPACE_NAME
{
    tcp_server::tcp_server(short port) : port(port), accecption_times{0}, session_count_peak{0}, end{false}
    {
    }
    tcp_server::~tcp_server()
    {
        common::print_debug(common::string_format("releasing server object."));
        for (auto s : sessions)
        {
            s->close();
            common::print_info("released a session during server finishing");
            delete s;
        }
    }
    void tcp_server::accecpt(tcp::acceptor &acceptor)
    {
        acceptor.async_accept(
            [&acceptor, this](boost::system::error_code ec, tcp::socket socket)
            {
                if (!ec)
                {
                    tcp_session *session = new tcp_session{this->io_context, std::move(socket)};
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
                common::print_debug(common::string_format("async_accept %s on the %dth attempt, sustained session count:%d", (ec ? "failed" : "ok"), this->accecption_times, this->sessions.size()));
                this->accecpt(acceptor);
            });
    }
    int tcp_server::listen()
    {
        tcp::acceptor acceptor{io_context};
        boost::system::error_code ec;
        auto endpoint = tcp::endpoint(boost::asio::ip::make_address("0.0.0.0"), port);
        acceptor.open(endpoint.protocol(), ec);
        acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        if (ec)
        {
            common::print_info(ec.message());
            return 1;
        }
        acceptor.bind(endpoint, ec);
        if (ec)
        {
            common::print_info(ec.message());
            return 1;
        }
        acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
        if (ec)
        {
            common::print_info(ec.message());
            return 1;
        }
        this->accecpt(acceptor);
        auto work = boost::asio::require(io_context.get_executor(), boost::asio::execution::outstanding_work.tracked);
        std::vector<std::thread> threads;
        for (int i = 0; i <= 50; i++)
        {
            threads.push_back(std::move(std::thread([this]()
                                                    { io_context.run(); })));
        }
        if (this->on_listen_begin)
        {
            this->on_listen_begin(this);
        }
        for (auto &t : threads)
        {
            t.join();
        }
        if (this->on_listen_end)
        {
            this->on_listen_end(this);
        }
        return 0;
    }
    void tcp_server::add_session(tcp_session *session)
    {
        std::lock_guard<std::mutex> guard(sessions_mtx);
        this->sessions.push_back(session);
        if (this->session_count_peak < this->sessions.size())
        {
            this->session_count_peak = this->sessions.size();
        }
        common::print_debug(common::string_format("sustained session count:%d", this->sessions.size()));
    }
    void tcp_server::remove_session(tcp_session *s)
    {
        std::lock_guard<std::mutex> guard(sessions_mtx);
        for (int i = 0; i < this->sessions.size(); i++)
        {
            auto session = this->sessions[i];
            if (session == s)
            {
                if (!session->closed)
                {
                    common::print_info("closing the un-closed session before releasing");
                    session->close();
                }
                this->sessions.erase(this->sessions.begin() + i);
                delete session;
                common::print_info("server released a session");
                break;
            }
        }
        common::print_debug(common::string_format("sustained session count:%d", this->sessions.size()));
    }
    void tcp_server::shutdown()
    {
        common::print_debug("shutdowning server...");
        this->end = true;
        this->io_context.stop();
        if (this->heartbeat_thread.joinable())
            this->heartbeat_thread.join();
        common::print_debug("server shutdownded!!!");
    }
    short tcp_server::get_port()
    {
        return this->port;
    }
    // begin tcp_client
    void tcp_client::start(std::string server_ip, std::string server_port)
    {
        start(server_ip, server_port, 3);
    }
    void tcp_client::start(std::string server_ip, std::string server_port, int thread_count)
    {
        try
        {
            tcp::resolver r(io_context);
            this->endpoints = r.resolve(server_ip, server_port);
            this->connect(endpoints.begin());
            auto work = boost::asio::require(io_context.get_executor(), boost::asio::execution::outstanding_work.tracked);
            std::vector<std::thread> threads;
            for (int i = 0; i < thread_count; i++)
            {
                threads.push_back(std::move(std::thread([this, i]()
                                                        {
                                                            io_context.run();
                                                            common::print_info(common::string_format("tcp client thread %d exited", i));
                                                            ; })));
            }
            for (auto &t : threads)
            {
                t.join();
            }
            common::print_debug("tcp_client::start finished");
            if (on_disconnected)
            {
                on_disconnected(this);
            }
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
        session.socket.async_connect(endpoint_iter->endpoint(), std::bind(&tcp_client::handle_connect, this, std::placeholders::_1, endpoint_iter));
    }
    void tcp_client::handle_connect(const boost::system::error_code &error, tcp::resolver::results_type::iterator endpoint_iter)
    {
        if (error)
        {
            common::print_debug(common::string_format("connection failed:%s", error.message().c_str()));
            connect(++endpoint_iter);
            return;
        }
        // connection suceess
        connected = true;
        if (this->on_connect_success)
            on_connect_success(this);
    }
    tcp_client::tcp_client() : connected{false}, session{io_context}
    {
        session.on_closed.subscribe([this](tcp_session *session)
                                    { this->shutdown(); });
    }
    tcp_client::~tcp_client()
    {
    }
    void tcp_client::shutdown()
    {
        if (!this->session.closed)
        {
            this->session.close();
        }
        this->io_context.stop();
    }
    void tcp_session::set_expiration()
    {
        std::lock_guard<std::mutex> guard(timer_mutex);
        if (this->timer.expires_after(std::chrono::seconds(timeout)) < 1)
        {
            common::print_debug("set_expiration failed");
        }
        this->timer.async_wait(boost::bind(&XTCP::tcp_session::on_timeout, this, boost::placeholders::_1));
    }
    void tcp_session::on_timeout(const boost::system::error_code &e)
    {
        if (e != boost::asio::error::operation_aborted)
        {
            common::print_debug("this session is time out, closing...");
            this->is_expired = true;
            this->close();
        }
    }
    tcp_session::tcp_session(boost::asio::io_context &io_context) : tcp_session{io_context, tcp::socket{io_context}}
    {
    }
    tcp_session::tcp_session(boost::asio::io_context &io_context, tcp::socket socket) : io_context{io_context}, timer{io_context}, socket{std::move(socket)}, buffer{new char[buffer_size]}, read_size{0}, closed{false}, is_expired{false}, timeout{TCP_SESSION_TIMEOUT}, _running_tasks{0}
    {
        this->timer.expires_after(std::chrono::seconds(timeout));
        this->timer.async_wait(boost::bind(&XTCP::tcp_session::on_timeout, this, boost::placeholders::_1));
        memset(this->buffer.get(), 0, buffer_size);
    }
    void tcp_session::read(size_t size, read_handler on_read, void *p)
    {
        if (this->closed)
        {
            on_read(0, this, false, "session closed", p);
        }
        this->increase_task_num();
        this->socket.async_read_some(boost::asio::buffer(buffer.get(), size > buffer_size ? buffer_size : size), [this, size, on_read, p](const boost::system::error_code &error, std::size_t bytes_transferred)
                                     {
                                         on_read(bytes_transferred, this, size == bytes_transferred, error?error.message():std::string{},p); 
                                         if (!error)
                                         {
                                             this->set_expiration();
                                             time(&this->last_read_timer);
                                             this->read_size += bytes_transferred;
                                             if (size != bytes_transferred)
                                             {
                                                 this->read(size - bytes_transferred, on_read, p);
                                             }
                                         }
                                         else
                                         {
                                             this->close();
                                         }
                                         this->decrease_task_num(); });
    }
    void tcp_session::write(const char *data, size_t size, written_handler on_written, void *p)
    {
        if (this->closed)
        {
            on_written(0, this, false, "session closed", p);
        }
        this->increase_task_num();
        this->socket.async_write_some(boost::asio::buffer(data, size), [this, data, size, on_written, p](const boost::system::error_code &error, std::size_t bytes_transferred)
                                      {
                                          on_written(bytes_transferred, this, size == bytes_transferred,error?error.message():std::string{}, p);
                                          if (!error)
                                          {
                                             this->set_expiration();
                                              time(&this->last_write_timer);
                                              this->written_size += bytes_transferred;
                                              if (size != bytes_transferred)
                                              {
                                                  this->write(data + bytes_transferred, size - bytes_transferred, on_written, p);
                                              }
                                          }
                                          else
                                          {
                                              this->close();
                                          }
                                          this->decrease_task_num(); });
    }
    void tcp_session::send_stream(std::shared_ptr<std::istream> fs, sent_stream_handler on_sent_stream, void *p)
    {
        static const int BUFFER_SIZE = 1 * 1024 * 1024;
        std::shared_ptr<std::array<char, BUFFER_SIZE>> buf{new std::array<char, BUFFER_SIZE>()};
        fs->read(buf.get()->data(), BUFFER_SIZE);
        if (fs->rdstate() & (std::ios_base::badbit))
        {
            throw common::exception("failed to read bytes");
        }
        int read_count = fs->gcount();
        this->write(
            buf.get()->data(), read_count, [this, fs, on_sent_stream, buf](size_t written_size, XTCP::tcp_session *session, bool completed, common::error error, void *p)
            {
                bool eof = fs->rdstate() & (std::ios_base::eofbit);
                on_sent_stream(written_size, session, completed && eof, error, p);
                if (completed)
                {
                    if (!eof)
                        send_stream(fs, on_sent_stream, p);
                } },
            NULL);
    }
    void tcp_session::receive_stream(std::shared_ptr<std::ostream> fs, size_t size, received_stream_handler on_received_stream, void *p)
    {
        this->read(
            size, [size, fs, on_received_stream](size_t read_size, XTCP::tcp_session *session, bool completed, common::error error, void *p)
            {
                if (!error)
                {
                    fs->write(session->buffer.get(), read_size);
                    if (!*fs)
                    {
                        completed = false;
                        error = "Writing failed.";
                    }
                }
                on_received_stream(read_size, session, completed, error, p); },
            NULL);
    }
    void tcp_session::close()
    {
        if (this->closed)
        {
            common::print_info("WARNING:the session already been closed before!");
            return;
        }
        this->timer.cancel();
        this->closed = true;
        this->socket.close();
        on_closed(this);
    }
    void tcp_session::increase_task_num()
    {
        std::lock_guard<std::mutex> guard(running_tasks_counter_mutex);
        _running_tasks++;
    }
    void tcp_session::decrease_task_num()
    {
        std::lock_guard<std::mutex> guard(running_tasks_counter_mutex);
        _running_tasks--;
    }
    void _receive_size(XTCP::tcp_session *tcp_session, std::shared_ptr<std::stringstream> size_ss, std::function<void(common::error error, message &msg)> on_read);
    void _receive_message(XTCP::tcp_session *tcp_session, std::shared_ptr<std::stringstream> msg_ss, size_t size, std::function<void(common::error error, message &msg)> on_read);

    std::shared_ptr<std::vector<char>> message::to_json() const
    {
        nlohmann::json j;
        for (auto header : this->headers)
        {
            header.fill_json(j["Header"]);
        }
        j["MsgType"] = this->msg_type;
        j["BodySize"] = this->body_size;
        std::string json_str = j.dump();
        char *str = common::strcpy(json_str.c_str());
        std::vector<char> *data = new std::vector<char>(str, str + json_str.size() + 1);
        delete[] str;
        std::shared_ptr<std::vector<char>> res{data};
        return res;
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
    void send_message(XTCP::tcp_session *session, message &msg, std::function<void(common::error error)> on_sent)
    {
        auto json = msg.to_json();
        int json_len = json.get()->size();
        std::string json_len_str = common::string_format("%x", json_len);
        int buf_len = json_len_str.size() + 1 + json_len;
        std::shared_ptr<std::vector<char>> buf{new std::vector<char>(buf_len)};
        char *dest = buf.get()->data();
        memcpy(dest, json_len_str.c_str(), json_len_str.size());
        dest += json_len_str.size();
        memcpy(dest++, "\0", 1);
        memcpy(dest, json.get()->data(), json_len);

        session->write(
            buf.get()->data(), buf_len, [buf, on_sent](size_t read_size, XTCP::tcp_session *session, bool completed, common::error error, void *p)
            {
                if ((completed || error))
                {
                    on_sent(error);
                } },
            NULL);
    }
    void send_message(XTCP::tcp_session *session, message &msg, common::error &error)
    {
        std::promise<common::error> promise;
        send_message(session, msg, [&promise](common::error error)
                     { promise.set_value(error); });
        error = promise.get_future().get();
    }
    void read_message(XTCP::tcp_session *session, std::function<void(common::error error, message &msg)> on_read)
    {
        std::shared_ptr<std::stringstream> size_ss{new std::stringstream{}};
        *size_ss << std::hex;
        _receive_size(session, size_ss, [on_read, session](common::error error, message &msg)
                      { on_read(error, msg); });
    }
    void read_message(XTCP::tcp_session *session, message &msg, common::error &error)
    {
        std::promise<common::error> promise;
        read_message(session, [&msg, &promise](common::error error, message &_msg)
                     {
                         msg = _msg;
                         promise.set_value(error); });
        error = promise.get_future().get();
    }
    void _receive_size(XTCP::tcp_session *tcp_session, std::shared_ptr<std::stringstream> size_ss, std::function<void(common::error error, message &msg)> on_read)
    {
        tcp_session->read(
            1, [on_read, size_ss](size_t read_size, XTCP::tcp_session *session, bool completed, common::error error, void *p)
            {
                if (error)
                {
                    message msg;
                    on_read(error, msg);
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
                } },
            NULL);
    }
    void _receive_message(XTCP::tcp_session *tcp_session, std::shared_ptr<std::stringstream> msg_ss, size_t size, std::function<void(common::error error, message &msg)> on_read)
    {
        tcp_session->read(
            size, [msg_ss, on_read](size_t read_size, XTCP::tcp_session *session, bool completed, common::error error, void *p)
            {
                message msg;
                msg_ss->write(session->buffer.get(), read_size);
                if (!msg_ss)
                {
                    session->close();
                    on_read("!!!FAILED TO WRITE TO STRINGSTREAM", msg);
                    return;
                }
                if (completed)
                {
                    common::print_debug(common::string_format("read message:%s", msg_ss->str().c_str()));
                    try
                    {
                        msg = message::parse(msg_ss->str());
                    }
                    catch (const std::exception &e)
                    {
                        on_read(common::string_format("error reading message:%s", e.what()), msg);
                        return;
                    }
                }

                if (error || completed)
                {
                    on_read(error, msg);
                }
                else
                {
                    on_read("Fatal ERROR", msg);
                } },
            NULL);
    }
};
