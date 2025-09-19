#include "logger.hpp"
#include "tcp_server.hpp"
#include "tcp_connection.hpp"
#include "message_broker.hpp"
#include "protocol_handler.hpp"
#include <thread_pool.hpp>
#include <scheduler.hpp>

using namespace ellohim;

constexpr auto PORT = 8123;

static Task<int> test()
{
    LOG(INFO) << "exec test function";
    //co_await sleep_for(1s); // Tambahkan delay untuk testing
    LOG(INFO) << "test function completed";
    co_return 11;
}

int main()
{
    file_manager::init("./");
    dotenv::init();
    logger::init(file_manager::get_project_file("./cout.log"));

    auto broker = std::make_shared<MessageBroker>();
    auto protocol = std::make_shared<ProtocolHandler>(broker);
    auto thread_pool_instance = std::make_unique<thread_pool>();
    scheduler::init();

    TcpServer server(PORT);

    // Event saat ada koneksi baru
    server.on_new_connection([protocol](std::shared_ptr<TcpConnection> conn) {
        conn->on_message([protocol, conn](const std::string& msg) {
            LOG(INFO) << "[Server] Received message: " << msg;
            protocol->handle_message(conn, msg);
        });

        conn->on_close([]() {
            LOG(INFO) << "[Server] Client disconnected";
        });

        conn->start();
        LOG(INFO) << "[Server] New client connected";
    });

    LOG(INFO) << "[Server] Broker started on port " << PORT;
    server.start();

    thread_pool_instance->destroy();
    thread_pool_instance.reset();

    return 0;
}