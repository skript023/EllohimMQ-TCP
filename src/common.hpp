#pragma once

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <deque>
#include <queue>
#include <vector>
#include <optional>
#include <exception>
#include <functional>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <algorithm>
#include <csignal>
#include <memory>
#include <source_location>
#include <filesystem>
#include <future>
#include <random>
#ifdef __linux__
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
typedef int socket_t;
#define CLOSESOCKET close
#else
#include <process.h>    // _getpid() on Windows
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib") // link dengan winsock
typedef SOCKET socket_t;
#define CLOSESOCKET closesocket
#endif

using namespace std::chrono_literals;

#include "logger.hpp"
#include "orchestrator.hpp"
#include "async.hpp"
#include <nlohmann/json.hpp>

#include <dotenv.h>


namespace ellohim
{
    inline std::atomic_bool g_running{ true };

    inline std::string get_unique_consumer_name(int index) {
        int pid = getpid();

        return std::format("worker-{}-{}", index, pid);
    }

    inline void socket_init() {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed\n";
            exit(EXIT_FAILURE);
        }
#endif
    }

    inline void socket_cleanup() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
}