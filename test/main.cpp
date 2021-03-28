#include <iostream>
#include <tcp.h>
#include <fstream>
void read_some(XTCP::tcp_session *session, int size)
{
}
void on_connect_success(XTCP::tcp_client *client)
{
    auto uuid = common::uuid();
    auto temp = std::string{};
    for (int i = 0; i < 100; i++)
    {
        temp += uuid;
    }
    uuid = temp;
    common::print_debug(common::string_format("connect succeed"));
    client->session.write(
        uuid.c_str(), uuid.size(), [](size_t written_size, XTCP::tcp_session *session, bool completed, common::error error, void *p) {
            assert(!error);
            if (error)
            {
                common::print_debug(common::string_format("write failed:%s", error.message()));
            }
            if (completed)
            {
                common::print_debug(common::string_format("sent successfully"));
            }
        },
        NULL);
    std::tuple<std::stringstream *, std::string> *tuple = new std::tuple<std::stringstream *, std::string>{new std::stringstream{}, uuid};
    client->session.read(
        uuid.size(), [](size_t read_size, XTCP::tcp_session *session, bool completed, common::error error, void *p) {
            assert(!error);
            std::unique_ptr<char[]> read{common::strcpy(session->buffer.get(), read_size)};
            common::print_info(common::string_format("read message from server:%s", read.get()));
            auto tuple = (std::tuple<std::stringstream*, std::string> *)p;
            std::stringstream *ss;
             std::string uuid;
             std::tie (ss,uuid)=*tuple;
             if(ss->str().size()<uuid.size()){
                 *ss<<read.get();
             }
             if(ss->str().size()>=uuid.size()){
                 assert(ss->str().size()==uuid.size()&&ss->str()==uuid);
            common::print_info(common::string_format("read uuid:%s",ss->str().c_str()));
            delete ss;
                 delete tuple;
             } },
        tuple);
}
void on_connect_fail(XTCP::tcp_client *client)
{
}
void run_client_thread()
{
    new std::thread{[]() {
        while (1)
        {
            XTCP::tcp_client client;
            client.on_connect_success = on_connect_success;
            client.on_connect_fail = on_connect_fail;
            client.start("127.0.0.1", "8080");
            std::this_thread::sleep_for(std::chrono::seconds{10});
        }
    }};
}
void read_msg(XTCP::tcp_session *session)
{
    common::print_info(common::string_format("reading client message"));
    XTCP::read_message(session, [session](common::error error, XTCP::message &msg) {
        common::print_info(common::string_format("reading client message %s", !error ? "ok" : "failed"));
        if (!error)
        {
            common::print_info(common::string_format("client message:%s", msg.to_json()));
            XTCP::send_message(session, msg, [session](common::error error) {
                common::print_debug(common::string_format("Server send message %s", error ? "failed" : "ok"));
                if (!error)
                {
                    read_msg(session);
                }
            });
        }
    });
}
void run_client_thread2()
{
    XTCP::tcp_client *client = new XTCP::tcp_client{};
    client->on_connect_success = [](XTCP::tcp_client *client) {
        new std::thread{[client]() {
            int i=1;
        while (1)
        {
            XTCP::message msg;
            msg.body_size=i++;
            /*XTCP::send_message(&client->session, msg, [](common::error error){
                if(error){
                    common::print_info(common::string_format("error sending message:%s",error.message()));
                }
            });*/
            common::error error;
              XTCP::send_message(&client->session, msg,error);
             if(error){
                    common::print_info(common::string_format("error sending message:%s",error.message()));
                }
           // std::this_thread::sleep_for(std::chrono::seconds{10});
           //client->session.close();
           return;
        } }};
    };
    //client.on_connect_fail = on_connect_fail;
    client->start("127.0.0.1", "8080");
}
void test_msg()
{
    std::thread server_th = std::thread([]() {
        XTCP::tcp_server tcp_server(8080);
        tcp_server.on_accepted = [](XTCP::tcp_session *session, XTCP::tcp_server *server) {
            common::print_debug("accepted a connection");
            read_msg(session);
        };
        tcp_server.listen();
    });
    run_client_thread2();
    //run_client_thread2();
    // run_client_thread2();
    // run_client_thread2();
    // run_client_thread2();
    common::pause();
}
void test_stream()
{
    std::string source_path = "/media/debian/14BEDD23BEDCFDE4/Users/bigyasuo/Downloads/debian-10.7.0-amd64-netinst.iso";
    //std::string dest_path = "/media/debian/14BEDD23BEDCFDE4/test-123456.vmdk";
    std::string dest_path = "/home/debian/Desktop/test-123456.vmdk";
    size_t file_size = common::file_size(source_path);
    std::string md5 = common::file_md5(source_path.c_str());
    XTCP::tcp_server tcp_server(8080);
    std::thread server_th = std::thread([&tcp_server, md5, dest_path, file_size]() {
        tcp_server.on_accepted = [dest_path, md5, file_size](XTCP::tcp_session *session, XTCP::tcp_server *server) {
            common::print_debug("accepted a connection");
            std::shared_ptr<std::ofstream> out{new std::ofstream{dest_path, std::ios_base::binary}};
            if (out->bad())
            {
                common::print_info(common::string_format("failed to create a file named %s", dest_path.c_str()));
                return;
            }
            session->receive_stream(
                out, file_size, [out, md5, dest_path](size_t read_size, XTCP::tcp_session *session, bool completed, common::error error, void *p) {
                    if (error)
                    {
                        common::print_info(common::string_format("Error:%s", error.message()));
                    }
                    if (completed)
                    {
                        out->flush();
                        out->close();
                        common::print_info(common::string_format("%s    %s", md5.c_str(), common::file_md5(dest_path.c_str()).c_str()));
                        if (!common::compare_md5(common::file_md5(dest_path.c_str()).c_str(), md5.c_str()))
                        {
                            common::print_info(common::string_format("MD5 error"));
                            return;
                        }
                        common::print_info(common::string_format("received a stream successfully:%s", error.message()));
                        XTCP::message msg;
                        msg.msg_type = 1;
                        XTCP::send_message(session, msg, [](common::error error) {
                            common::print_info(common::string_format("%s", error ? "FAILED" : "OK"));
                        });
                    }
                },
                NULL);
        };
        tcp_server.listen();
    });

    //client
    XTCP::tcp_client *client = new XTCP::tcp_client{};
    client->on_connect_success = [source_path](XTCP::tcp_client *client) {
        new std::thread{[source_path, client]() {
            std::shared_ptr<std::istream> in{new std::ifstream{source_path, std::ios_base::binary}};
            if (!*in)
            {
                common::print_info(common::string_format("failed to open a file named %s", source_path.c_str()));
                return;
            }
            client->session.send_stream(
                in, [](size_t written_size, XTCP::tcp_session *session, bool completed, common::error error, void *p) {
                    if (error)
                    {
                        common::print_info(common::string_format("Error:%s", error.message()));
                    }
                    if (completed)
                    {
                        common::print_info(common::string_format("send a stream successfully:%s", error.message()));
                    }
                },
                NULL);
        }};
    };
    //client.on_connect_fail = on_connect_fail;
    client->start("127.0.0.1", "8080");
    XTCP::message msg;
    common::error err;
    XTCP::read_message(&client->session, msg, err);
    if (err || msg.msg_type != 1)
    {
        common::print_info("Test failed.");
    }
    tcp_server.shutdown();
    server_th.join();
    common::print_info("Test OK.");
}
int main(int argc, char *argv[])
{
    if (std::string(argv[1]) == "server")
    {
        std::thread server_thread([]() {
            XTCP::tcp_server tcp_server(8080);
            tcp_server.on_accepted = [](XTCP::tcp_session *session, XTCP::tcp_server *server) {
                session->read(
                    3600, [](size_t read_size, XTCP::tcp_session *session, bool completed, common::error error, void *p) {
                        auto server = (XTCP::tcp_server *)p;
                        auto read{common::strcpy(session->buffer.get(), read_size)};
                        //common::print_debug(common::string_format("read succed:%s", read.get()));
                        session->write(
                            read, read_size, [](size_t written_size, XTCP::tcp_session *session, bool completed, common::error error, void *p) {
                                auto _p = (char *)p;
                                delete[] _p;
                            },
                            read);
                        common::print_debug("sent message to client");
                    },
                    server);
            };
            tcp_server.listen();
        });
        common::pause();
    }
    else if (std::string(argv[1]) == "client")
    {
        for (int i = 0; i < 100; i++)
            run_client_thread();
        common::pause();
    }
    else if (std::string(argv[1]) == "test_msg")
    {
        test_msg();
    }
    else if (std::string(argv[1]) == "test_stream")
    {
        test_stream();
    }
    else
    {
        common::print_info("parameter error.");
    }
    //server_thread.join();
}