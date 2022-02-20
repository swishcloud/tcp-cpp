#define BOOST_TEST_MODULE TEST_IMSERVER
#include <boost/test/included/unit_test.hpp>
#include <tcp.h>
#include <common.h>
void send_msg(XTCP::tcp_session *session);
void read_msg(XTCP::tcp_session *session);
void read_message(XTCP::tcp_session *session)
{
    XTCP::read_message(session, [session](common::error error, XTCP::message &msg)
                       {
                           if (error)
                           {
                               common::print_info("a client connection broken");
                           }
                           else
                           {
                               XTCP::send_message(session, msg, [session](common::error error)
                                                  {
                                                      if (error)
                                                      {
                                                          common::print_info("failed to send a message");
                                                      }
                                                      else
                                                      {
                                                          common::print_info("sent a message");
                                                          read_message(session);
                                                      }
                                                  });
                           }
                       });
}

int serve()
{
    int port = 8080;
    XTCP::tcp_server tcp_server(port);
    tcp_server.on_accepted = [](XTCP::tcp_session *session, XTCP::tcp_server *server)
    {
        common::print_debug("accepted a connection");
        read_message(session);
    };
    tcp_server.on_listen_end = [](XTCP::tcp_server *)
    {
        common::print_debug("server not listening");
    };
    tcp_server.on_listen_begin = [&tcp_server](XTCP::tcp_server *)
    {
        const char *c_address;
        c_address = "192.168.29.3";

        XTCP::tcp_client *client = new XTCP::tcp_client{};
        client->on_connect_fail = [](XTCP::tcp_client *client)
        {
            common::print_info("Failed to connect server");
        };
        client->on_connect_success = [](XTCP::tcp_client *client)
        {
            common::print_info("Connected to server");
            //read_msg(&client->session);
            send_msg(&client->session);
        };
        client->session.on_closed = [&tcp_server, client](XTCP::tcp_session *)
        {
            client->shutdown();
        };
        client->on_disconnected = [&tcp_server](XTCP::tcp_client *client)
        {
            tcp_server.shutdown();
        };
        client->start(c_address, "8080");
        delete client;
    };
    common::print_info(common::string_format("Listen on port %d", port));
    return tcp_server.listen();
}
int i = 0;
void send_msg(XTCP::tcp_session *session)
{
    std::this_thread::sleep_for(std::chrono::seconds{1});
    XTCP::message m{};
    i++;
    if (i > 10)
    {
        session->close();
        return;
    }
    m.addHeader(XTCP::message_header{"content", common::string_format("#%d", i)});
    XTCP::send_message(session, m, [session](common::error error)
                       {
                           if (error)
                           {
                               common::print_info(common::string_format("Failed to send message::%s", error.message()));
                           }
                           else
                           {
                               read_msg(session);
                           }
                       });
}
void read_msg(XTCP::tcp_session *session)
{
    XTCP::read_message(session, [session](common::error error, XTCP::message &msg)
                       {
                           if (error)
                           {
                               common::print_info(common::string_format("failed to read:%s", error.message()));
                           }
                           else
                           {
                               common::print_info(common::string_format("client message:%s", msg.to_json().get()->data()));
                               send_msg(session);
                           }
                       });
}
BOOST_AUTO_TEST_CASE(first_test)
{
    serve();
    // while (true)
    // {
    //     char c = getchar();
    //     std::cout << c << std::endl;
    //     if (c == 'q')
    //     {
    //         break;
    //     }
    // }
}
class second
{
public:
    std::function<void()> cb;
    void Do()
    {
        cb();
    }
};
BOOST_AUTO_TEST_CASE(second_test)
{
    second *s = new second{};
    s->cb = [s]()
    {
        common::print_info("callback");
        std::this_thread::sleep_for(std::chrono::seconds{5});
    };
    std::thread th{[s]()
                   {
                       s->Do();
                   }};
    std::this_thread::sleep_for(std::chrono::seconds{1});
    common::print_info("release object");
    delete s;
    common::print_info("finished");
}