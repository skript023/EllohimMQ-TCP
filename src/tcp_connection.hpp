#pragma once
#include <common.hpp>

namespace ellohim
{
    class TcpConnection : public std::enable_shared_from_this<TcpConnection> 
    {
    public:
        using ReadHandler = std::function<void(const std::string&)>;
        using CloseHandler = std::function<void()>;

        explicit TcpConnection(socket_t socket_fd);

        async<void> start();   // coroutine start

        void send(const std::string& message);

        void on_message(ReadHandler handler);
        void on_close(CloseHandler handler);

    private:
        socket_t sock_fd;
        ReadHandler read_handler;
        CloseHandler close_handler;
    };
}