#include "tcp_server.hpp"

namespace ellohim
{
    // Fungsi untuk mengatur soket ke mode non-blocking
    void TcpServer::set_nonblocking(int fd) 
    {
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(fd, FIONBIO, &mode);
#else
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
    }

    // Konstruktor
    TcpServer::TcpServer(int port)
    {
        socket_init();
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1)
         {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        set_nonblocking(server_fd);

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

        // Inisialisasi event loop sesuai platform
#ifdef __linux__
        epoll_fd = epoll_create1(0);
        if (epoll_fd == -1) {
            perror("epoll_create1");
            exit(EXIT_FAILURE);
        }
        epoll_event event;
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = server_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
            perror("epoll_ctl: server_fd");
            exit(EXIT_FAILURE);
        }
        events.resize(10);
#elif _WIN32
        // Tambahkan soket server ke daftar poll_fds
        pollfd server_pollfd;
        server_pollfd.fd = server_fd;
        server_pollfd.events = POLLIN;
        poll_fds.push_back(server_pollfd);
#endif
    }

    TcpServer::~TcpServer()
    {
#ifdef __linux__
        close(epoll_fd);
#endif
        CLOSESOCKET(server_fd);
        socket_cleanup();
    }

    void TcpServer::on_new_connection(ConnectionHandler handler)
    {
        connection_handler = handler;
    }

    void TcpServer::handle_accept()
    {
        while (true) 
        {
            sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);
            socket_t client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_addr_len);

            if (client_fd == -1) 
            {
#ifdef _WIN32
                if (WSAGetLastError() == WSAEWOULDBLOCK) 
                {
#else
                if (errno == EAGAIN || errno == EWOULDBLOCK) 
                {
#endif
                    break;
                }
                perror("accept");
                break;
            }

            set_nonblocking(client_fd);
            LOG(INFO) << "[Server] Koneksi baru diterima: " << client_fd;
            
            auto conn = std::make_shared<TcpConnection>(client_fd);
            connections[client_fd] = conn;

            if (connection_handler) 
            {
                connection_handler(conn);
            }

            // Daftarkan soket klien ke event loop
#ifdef __linux__
            epoll_event client_event;
            client_event.events = EPOLLIN | EPOLLET;
            client_event.data.fd = client_fd;
            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event);
#elif _WIN32
            pollfd client_pollfd;
            client_pollfd.fd = client_fd;
            client_pollfd.events = POLLIN;
            poll_fds.push_back(client_pollfd);
#endif
        }
    }

    void TcpServer::handle_read(int client_fd) 
    {
        auto conn_it = connections.find(client_fd);
        if (conn_it == connections.end()) 
        {
            std::cerr << "[Server] Error: Koneksi tidak ditemukan untuk fd " << client_fd << std::endl;
            close_connection(client_fd);
            return;
        }

        auto conn = conn_it->second;
        char buffer[1024];
        size_t bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
        
        if (bytes_read > 0) 
        {
            // Gunakan handler secara langsung
            if (conn->read_handler) 
            {
                conn->read_handler(std::string(buffer, bytes_read));
            }
        } 
        else 
        {
#ifdef _WIN32
            if (bytes_read == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
#else
            if (bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
#endif
                return;
            }
            std::cout << "[Server] Klien terputus: " << client_fd << std::endl;
            // Gunakan handler secara langsung
            if (conn->close_handler) {
                conn->close_handler();
            }
            close_connection(client_fd);
        }
    }

    void TcpServer::close_connection(int client_fd) 
    {
#ifdef __linux__
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
#endif
        CLOSESOCKET(client_fd);
        connections.erase(client_fd);
    }


    void TcpServer::start()
    {
#ifdef __linux__
        while (true)
        {
            int event_count = epoll_wait(epoll_fd, events.data(), events.size(), -1);
            if (event_count == -1) 
            {
                perror("epoll_wait");
                continue;
            }
            for (int i = 0; i < event_count; ++i) 
            {
                if (events[i].data.fd == server_fd) 
                {
                    handle_accept();
                } 
                else 
                {
                    handle_read(events[i].data.fd);
                }
            }

            std::this_thread::sleep_for(500ms);
        }
#elif _WIN32
        while (true)
        {
            int event_count = WSAPoll(poll_fds.data(), poll_fds.size(), -1);
            if (event_count == SOCKET_ERROR) 
            {
                perror("WSAPoll");
                continue;
            }
            
            // Periksa koneksi baru dari soket server
            if (poll_fds[0].revents & POLLIN) 
            {
                handle_accept();
            }

            // Loop untuk menangani data yang masuk dari klien
            for (size_t i = 1; i < poll_fds.size(); )
            {
                if (poll_fds[i].revents & POLLIN) 
                {
                    handle_read(poll_fds[i].fd);
                }
                
                // Jika koneksi terputus, kita harus menghapusnya dari poll_fds
                // dan menggeser elemen-elemen selanjutnya
                if (connections.find(poll_fds[i].fd) == connections.end()) 
                {
                    poll_fds.erase(poll_fds.begin() + i);
                } 
                else 
                {
                    ++i;
                }
            }

            std::this_thread::sleep_for(500ms);
        }
#endif
    }
}
