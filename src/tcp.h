#ifndef XTCP_H
#define XTCP_H
#include <iostream>
#include "internal.h"
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <common.h>
#include <nlohmann/json.hpp>
using namespace nlohmann;
using boost::asio::ip::tcp;
namespace GLOBAL_NAMESPACE_NAME
{
    class tcp_session
    {
    private:
        typedef std::function<void(size_t written_size, tcp_session *session, bool completed, common::error error, void *p)> written_handler;
        typedef std::function<void(size_t read_size, tcp_session *session, bool completed, common::error error, void *p)> read_handler;
        typedef std::function<void(size_t written_size, tcp_session *session, bool completed, common::error error, void *p)> sent_stream_handler;
        typedef std::function<void(size_t read_size, tcp_session *session, bool completed, common::error serror, void *p)> received_stream_handler;
        typedef std::function<void(tcp_session *session)> close_handler;
        boost::asio::deadline_timer timer;
        boost::asio::io_context &io_context;
        void *data;
        constexpr static int buffer_size = 1024 * 1024 * 1;
        time_t last_read_timer;
        time_t last_write_timer;
        size_t read_size;
        size_t written_size;
        std::mutex running_tasks_counter_mutex;
        bool set_expiration();
        void on_timeout(const boost::system::error_code &e);

    public:
        std::unique_ptr<char[]> buffer;
        close_handler on_closed;
        tcp::socket socket;
        bool closed;
        bool is_expired;
        int timeout;
        int _running_tasks;
        tcp_session(boost::asio::io_context &io_context);
        tcp_session(boost::asio::io_context &io_context, tcp::socket socket);
        void write(const char *data, size_t size, written_handler on_written, void *p);
        void read(size_t size, read_handler on_read, void *p);
        void send_stream(std::shared_ptr<std::istream> fs, sent_stream_handler on_sent_stream, void *p);
        void receive_stream(std::shared_ptr<std::ostream> fs, size_t size, received_stream_handler on_received_stream, void *p);
        void close();
        void increase_task_num();
        void decrease_task_num();
    };

    class tcp_server
    {
    private:
        boost::asio::io_context io_context;
        std::thread heartbeat_thread;
        short port;
        size_t accecption_times;
        void accecpt(tcp::acceptor &acceptor);
        bool end;

    public:
        std::function<void(tcp_session *session, tcp_server *server)> on_accepted;
        std::function<void(tcp_server *server)> on_listen_end;
        std::function<void(tcp_server *server)> on_listen_begin;
        std::mutex sessions_mtx;
        std::vector<tcp_session *> sessions;
        int session_count_peak;
        tcp_server(short port);
        ~tcp_server();
        int listen();
        void add_session(tcp_session *);
        void remove_session(tcp_session *);
        void shutdown();
    };

    class tcp_client
    {
    private:
        boost::asio::io_context io_context;
        tcp::resolver::results_type endpoints;

    public:
        typedef std::function<void(tcp_client *tcp_client)> connect_success_handler;
        typedef std::function<void(tcp_client *tcp_client)> connect_fail_handler;
        typedef std::function<void(tcp_client *tcp_client)> disconnected_handler;
        connect_success_handler on_connect_success;
        connect_fail_handler on_connect_fail;
        disconnected_handler on_disconnected;
        tcp_session session;
        bool connected;
        void start(std::string server_ip, std::string server_port);
        void connect(tcp::resolver::results_type::iterator endpoint_iter);
        void handle_connect(const boost::system::error_code &error, tcp::resolver::results_type::iterator endpoint_iter);
        void shutdown();
        tcp_client();
        ~tcp_client();
    };

    struct message_header
    {
    private:
        std::string str_v;
        size_t int_v;
        int t;

    public:
        std::string name;
        message_header(std::string name, std::string v);
        message_header(std::string name, size_t v);
        void fill_json(json &j);
        template <class T>
        T getValue() const
        {
            if (std::is_integral<T>::value)
            {
                return static_cast<T>(*(T *)(&this->int_v));
            }
            else
            {
                return static_cast<T>(*(T *)(&this->str_v));
            }
        }
    };
    class message
    {
    private:
        std::vector<message_header> headers{};

    public:
        //message();
        //message(message &&msg);
        int msg_type{0};
        size_t body_size{0};
        std::shared_ptr<std::vector<char>> to_json() const;
        operator bool() const;
        static message parse(std::string json);
        void addHeader(message_header value);

        template <typename T>
        T getHeaderValue(std::string name) const
        {
            for (auto &h : this->headers)
            {
                if (strcmp(h.name.c_str(), name.c_str()) == 0)
                {
                    return h.getValue<T>();
                }
            }
            return T();
        }
    };

    void send_message(XTCP::tcp_session *session, message &msg, std::function<void(common::error error)> on_sent);
    void send_message(XTCP::tcp_session *session, message &msg, common::error &error);
    void read_message(XTCP::tcp_session *session, std::function<void(common::error error, message &msg)> on_read);
    void read_message(XTCP::tcp_session *session, message &msg, common::error &error);
};
#endif