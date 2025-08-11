#include "logger.hpp"
#include "tcp_server.hpp"
#include "tcp_connection.hpp"
#include "message_broker.hpp"
#include "protocol_handler.hpp"
#include <thread_pool.hpp>
using namespace ellohim;

constexpr auto PORT = 8123;

static async<int> test()
{
    LOG(INFO) << "exec test function";
    //co_await sleep_for(100ms); // Tambahkan delay untuk testing
    LOG(INFO) << "test function completed";
    co_return 11;
}

int main()
{
    dotenv::init();
    logger::init("Ellohim Worker");

    auto broker = std::make_shared<MessageBroker>();
    auto protocol = std::make_shared<ProtocolHandler>(broker);
    auto tp = std::make_shared<thread_pool>();

    // PENTING: Pass thread pool ke scheduler
    scheduler::start();

    TcpServer server(PORT);

    broker->register_topic_handler("math.add", [](std::string payload) -> async<void> {
        LOG(INFO) << "[Task] Starting math.add with payload: " << payload;

        LOG(INFO) << "[Handler] About to call test()";
        auto v = co_await test();
        LOG(INFO) << "[Handler] test() completed value " << v;

        auto pos = payload.find(',');
        if (pos != std::string::npos) {
            int a = std::stoi(payload.substr(0, pos));
            int b = std::stoi(payload.substr(pos + 1));
            int sum = a + b + v;
            LOG(INFO) << "[Task] math.add result: " << sum;
        }
        co_return;
    });

    broker->register_topic_handler("task.hello", [](std::string payload) -> async<> {
        LOG(INFO) << "[Handler] Task started with payload: " << payload;

        try 
        {
            LOG(INFO) << "[Handler] About to sleep";
            for (size_t i = 0; i < 10; i++)
            {
				LOG(INFO) << "[Handler] Sleeping for 1 second, iteration " << i + 1;    
                co_await sleep_for(1s);
            }
            LOG(INFO) << "[Handler] Sleep completed";

            LOG(INFO) << "[Handler] Task completed successfully";
        }
        catch (const std::exception& e)
        {
            LOG(FATAL) << "[Handler] Exception: " << e.what();
        }
        catch (...) 
        {
            LOG(FATAL) << "[Handler] Unknown exception";
        }

        co_return;
    });

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

    // Cleanup
    scheduler::stop();
    tp->destroy();
    tp.reset();

    return 0;
}