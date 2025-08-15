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
        void set_nonblocking(int fd);
        void handle_accept();
        void handle_read(int client_fd);
        void close_connection(int client_fd);

        socket_t server_fd;
        ConnectionHandler connection_handler;
        std::unordered_map<int, std::shared_ptr<TcpConnection>> connections;

        // Abstraksi Event Loop
#ifdef __linux__
        int epoll_fd;
        std::vector<epoll_event> events;
#elif _WIN32
        // Di Windows, WSAPoll akan menggunakan struktur POLLFD
        // Kita akan menyimpan soket server dan semua soket klien di sini.
        std::vector<pollfd> poll_fds;
#endif
    };
}