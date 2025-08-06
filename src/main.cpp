#include "logger.hpp"
#include "tcp_server.hpp"
#include "tcp_connection.hpp"
#include "message_broker.hpp"
#include "protocol_handler.hpp"

using namespace ellohim;
constexpr auto PORT = 8123;

int main()
{
    dotenv::init();

    logger::init("Ellohim Worker");

    auto broker = std::make_shared<MessageBroker>();
    auto protocol = std::make_shared<ProtocolHandler>(broker);

    TcpServer server(PORT);
    broker->register_topic_handler("math.add", [](const std::string& payload) -> async<> {
        // misalnya payload: "3,5"
        auto pos = payload.find(',');
        if (pos != std::string::npos) {
            int a = std::stoi(payload.substr(0, pos));
            int b = std::stoi(payload.substr(pos + 1));
            int sum = a + b;
            LOG(INFO) << "[Task] math.add result: " << sum;
        }

        co_return;
    });

    broker->register_topic_handler("task.hello", [](const std::string& payload) -> async<> {
        LOG(INFO) << "[Task] Hello task: " << payload;

        co_return;
    });


    // Event saat ada koneksi baru
    server.on_new_connection([protocol](std::shared_ptr<TcpConnection> conn) {
        // Event saat menerima pesan
        conn->on_message([protocol, conn](const std::string& msg) {
            protocol->handle_message(conn, msg);
        });

        // Event saat koneksi tutup
        conn->on_close([]() {
            LOG(INFO) << "[Server] Client disconnected";
        });

        run_sync(conn->start());
        LOG(INFO) << "[Server] New client connected";
    });

    LOG(INFO) << "[Server] Broker started on port " << PORT;
    server.start();

    return 0;
}
