#include "tcp_connection.hpp"

namespace ellohim
{
    TcpConnection::TcpConnection(socket_t socket_fd)
        : sock_fd(socket_fd) 
    {}

    async<void> TcpConnection::start()
    {
        char buffer[1024];
        for (;;) 
        {
            int len = recv(sock_fd, buffer, sizeof(buffer), 0);
            if (len <= 0) 
            {
                if (close_handler) close_handler();
                CLOSESOCKET(sock_fd);
                co_return;
            }
            if (read_handler) 
            {
                read_handler(std::string(buffer, len));
            }
            co_await sleep_for(std::chrono::milliseconds(1)); // memberi kesempatan scheduler
        }
    }

    void TcpConnection::send(const std::string& message) {
        ::send(sock_fd, message.c_str(), (int)message.size(), 0);
    }

    void TcpConnection::on_message(ReadHandler handler) {
        read_handler = handler;
    }

    void TcpConnection::on_close(CloseHandler handler) {
        close_handler = handler;
    }
}