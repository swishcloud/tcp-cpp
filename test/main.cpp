#include <iostream>
#include <tcp.h>
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
        uuid.c_str(), uuid.size(), [](size_t written_size, XTCP::tcp_session *session, bool completed, const char *error, void *p) {
            assert(!error);
            if (error)
            {
                common::print_debug(common::string_format("write failed:%s", error));
            }
            if (completed)
            {
                common::print_debug(common::string_format("sent successfully"));
            }
        },
        NULL);
    std::tuple<std::stringstream *, std::string> *tuple = new std::tuple<std::stringstream *, std::string>{new std::stringstream{}, uuid};
    client->session.read(
        uuid.size(), [](size_t read_size, XTCP::tcp_session *session, bool completed, const char *error, void *p) {
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
int main(int argc, char *argv[])
{
    if (std::string(argv[1]) == "server")
    {
        std::thread server_thread([]() {
            XTCP::tcp_server tcp_server(8080);
            tcp_server.on_accepted = [](XTCP::tcp_session *session, XTCP::tcp_server *server) {
                session->read(
                    3600, [](size_t read_size, XTCP::tcp_session *session, bool completed, const char *error, void *p) {
                        auto server = (XTCP::tcp_server *)p;
                        auto read{common::strcpy(session->buffer.get(), read_size)};
                        //common::print_debug(common::string_format("read succed:%s", read.get()));
                        session->write(
                            read, read_size, [](size_t written_size, XTCP::tcp_session *session, bool completed, const char *error, void *p) {
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
    else
    {
        common::print_info("parameter error.");
    }
    //server_thread.join();
}