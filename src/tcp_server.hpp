#pragma once
#include <common.hpp>
#include "tcp_connection.hpp"

namespace ellohim
{
    class TcpServer
    {
    public:
        using ConnectionHandler = std::function<void(std::shared_ptr<TcpConnection>)>;

        explicit TcpServer(int port);
        ~TcpServer();
        void start();
        void on_new_connection(ConnectionHandler handler);

    private:
        socket_t server_fd;
        ConnectionHandler connection_handler;
    };
}