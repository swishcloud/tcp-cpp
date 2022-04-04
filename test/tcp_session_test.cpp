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
                           } });
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
            // read_msg(&client->session);
            send_msg(&client->session);
        };
        client->session.on_closed.subscribe([&tcp_server, client](XTCP::tcp_session *)
                                            { client->shutdown(); });
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
                           } });
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
                           } });
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
size_t send_message_i = 0;
void send_message(XTCP::tcp_session *session)
{
    auto filepath = ".cache/test";
    auto msg = std::shared_ptr<XTCP::message>{new XTCP::message{}};
    size_t i = send_message_i++;
    msg->addHeader({"index", i});
    msg->addHeader({"md5", common::file_md5(filepath)});
    msg->body_size = common::file_size(filepath);
    XTCP::send_message(session, *msg, [session, filepath, i](common::error error)
                       { 
                                                                                                         if(error)
                                                                                                         {
                                                                                                             common::print_info(common::string_format("failed to send msg %d with error:%s", i,error.message()));
                                                                                                         }
                                                                                                         else
                                                                                                         {
                                                                                                             common::print_info(common::string_format("sent msg %d", i));
                                                                                                             auto fs = std::shared_ptr<std::istream>{new std::ifstream{filepath, std::ios::binary | std::ios::in}};
                                                                                                             session->send_stream(
                                                                                                                 fs, [](size_t written_size, XTCP::tcp_session *session, bool completed, common::error error, void *p)
                                                                                                                 {
                                                                                                                                                   if(error)
                                                                                                         {
                                                                                                             common::print_info(common::string_format("failed to send stream with error:%s",error.message()));
                                                                                                         }
                                                                                                         else
                                                                                                         {
                                                                                                             if(completed)
                                                                                                             {
                                                                                                                 common::print_info(common::string_format("sent stream successfully"));
                                                                                                                 send_message(session);
                                                                                                             }
                                                                                                         } },
                                                                                                                 NULL);
                                                                                                         } });
}
BOOST_AUTO_TEST_CASE(write_test)
{
    XTCP::tcp_server server{8080};
    struct receive_client_msg
    {
        void read_msg(XTCP::tcp_session *session) const
        {
            XTCP::read_message(session, [this, session](common::error error, XTCP::message &msg)
                               {
                if (error)
                {
                    common::print_info(common::string_format("failed to read:%s", error.message()));
                }
                else
                {
                    common::print_info(common::string_format("client message:%s", msg.to_json().get()->data()));
                    if(msg.body_size>0){
                        auto md5=msg.getHeaderValue<std::string>("md5");
                        read_stream(session,msg.body_size,md5);
                    }else
                    {
                        read_msg(session);
                    }
                } });
        };
        void read_stream(XTCP::tcp_session *session, size_t size, std::string md5) const
        {
            auto filepath = common::string_format(".cache/%s", common::uuid().c_str());
            auto out = std::shared_ptr<std::ostream>{new std::ofstream{filepath, std::ios::binary | std::ios::out}};
            if (!out)
            {
                common::print_info(common::string_format("failed to create file:%s", filepath.c_str()));
                return;
            }
            session->receive_stream(
                out, size, [this, out, filepath, md5](size_t read_size, XTCP::tcp_session *session, bool completed, common::error error, void *p)
                {
                     if (error)
                {
                    common::print_info(common::string_format("failed to read stream:%s", error.message()));
                }
                else
                {
                    if (completed){
                        common::print_info(common::string_format("read a stream successfully!!!"));
                        out->flush();
                        std::string m;
                        try
                        {
                            m = common::file_md5(filepath.c_str());
                        }catch(std::exception e)
                        {
                            common::print_info(common::string_format("SERVER ERROR:%s",e.what()));
                            return;
                        }
                        assert(common::delete_file(filepath));
                        if(!out->good())
                        {
                            common::print_info(common::string_format("file error"));
                        }
                        else if (m != md5)
                        {
                            common::print_info(common::string_format("WRONG MD5!!!%s->%s",m.c_str(),md5.c_str()));
                        }
                        else
                        {
                            this->read_msg(session);
                        }
                    }
                } },
                NULL);
        }
    };
    receive_client_msg rcm;

    server.on_accepted = [rcm](XTCP::tcp_session *session, XTCP::tcp_server *server)
    {
        rcm.read_msg(session);
    };
    std::thread server_thread{[&server]()
                              {
                                  server.listen();
                              }};
    std::thread client_thread;
    const char *c_address;
    c_address = "192.168.29.3";

    XTCP::tcp_client client{};
    client.on_connect_fail = [](XTCP::tcp_client *client)
    {
        common::print_info("Failed to connect server");
    };
    client.on_connect_success = [this, &client_thread](XTCP::tcp_client *client)
    {
        common::print_info("Connected to server");
        send_message(&client->session);
        client_thread = std::thread{[client]()
                                    {
                                        // XTCP::message msg{};
                                        std::vector<std::thread *> threads;
                                        // for (size_t i = 0; i < 100; i++)
                                        // {
                                        //     common::print_info(common::string_format("new %dst client sending thread", i + 1));
                                        //     // auto filepath = "/home/flwwd/Desktop/tcp-cpp/build/CMakeCache.txt";
                                        //     threads.push_back(new std::thread{[client, msg, filepath, i]()
                                        //                                       {
                                        //                                           XTCP::send_message(&client->session, *msg, [client, filepath, i](common::error error)
                                        //                                                              {
                                        //                                                                  if(error)
                                        //                                                                  {
                                        //                                                                      common::print_info(common::string_format("failed to send msg %d with error:%s", i,error.message()));
                                        //                                                                  }
                                        //                                                                  else
                                        //                                                                  {
                                        //                                                                      common::print_info(common::string_format("sent msg %d", i));
                                        //                                                                      auto fs = std::shared_ptr<std::istream>{new std::ifstream{filepath, std::ios::binary | std::ios::in}};
                                        //                                                                      client->session.send_stream(
                                        //                                                                          fs, [](size_t written_size, XTCP::tcp_session *session, bool completed, common::error error, void *p)
                                        //                                                                          {
                                        //                                                                                                            if(error)
                                        //                                                                  {
                                        //                                                                      common::print_info(common::string_format("failed to send stream with error:%s",error.message()));
                                        //                                                                  }
                                        //                                                                  else
                                        //                                                                  {
                                        //                                                                      if(completed)
                                        //                                                                      {
                                        //                                                                          common::print_info(common::string_format("sent stream successfully"));
                                        //                                                                      }
                                        //                                                                  } },
                                        //                                                                          NULL);
                                        //                                                                  } });
                                        //                                       }});
                                        // }
                                        for (auto i : threads)
                                        {
                                            common::print_info("joining a client sending thread");
                                            i->join();
                                            common::print_info("join-ed a client sending thread");
                                            delete i;
                                        }
                                        common::print_info("exiting client thread");
                                    }};
    };
    client.session.on_closed.subscribe([&server](XTCP::tcp_session *session)
                                       { server.shutdown(); });
    std::this_thread::sleep_for(std::chrono::seconds{3});
    client.start(c_address, "8080", 20);
    server_thread.join();
    client_thread.join();
}
BOOST_AUTO_TEST_CASE(timeout_test)
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
        read_msg(&client->session);
        // send_msg(&client->session);
    };
    client->session.on_closed.subscribe([client](XTCP::tcp_session *)
                                        { client->shutdown(); });
    client->on_disconnected = [](XTCP::tcp_client *client)
    {
        common::print_info("on_disconnected");
    };
    client->start(c_address, "8080");
    delete client;
}