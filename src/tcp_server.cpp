#include "tcp_server.hpp"

namespace ellohim
{
    TcpServer::TcpServer(int port) 
    {
        socket_init();

        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1) {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        int opt = 1;
#ifdef _WIN32
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) 
        {
            perror("bind");
            exit(EXIT_FAILURE);
        }
        if (listen(server_fd, 10) < 0) 
        {
            perror("listen");
            exit(EXIT_FAILURE);
        }
    }

    void TcpServer::on_new_connection(ConnectionHandler handler) 
    {
        connection_handler = handler;
    }

    void TcpServer::start() 
    {
        while (true) 
		{
			LOG(INFO) << "[Server] Waiting for new connection...";
            socket_t client_fd = accept(server_fd, nullptr, nullptr);
            if (client_fd == -1) 
            {
                perror("accept");
                continue;
            }
            auto conn = std::make_shared<TcpConnection>(client_fd);
            if (connection_handler) 
            {
                connection_handler(conn);
            }
        }
    }

    TcpServer::~TcpServer()
    {
        CLOSESOCKET(server_fd);
        socket_cleanup();
    }
}