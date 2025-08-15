#include "tcp_connection.hpp"

namespace ellohim
{
    TcpConnection::TcpConnection(socket_t socket_fd)
        : sock_fd(socket_fd) 
    {}

    void TcpConnection::start()
    {
        // char buffer[1024];
        // for (;;) 
        // {
		// 	LOG(INFO) << "[Connection] Waiting for data on socket: " << sock_fd;
        //     int len = recv(sock_fd, buffer, sizeof(buffer), 0);
        //     if (len <= 0) 
        //     {
        //         if (close_handler) close_handler();
        //         CLOSESOCKET(sock_fd);
        //         return;
        //     }
        //     if (read_handler) 
        //     {
        //         read_handler(std::string(buffer, len));
        //     }
        // }
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