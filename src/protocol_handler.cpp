#include "protocol_handler.hpp"
#include "message_broker.hpp"
#include "tcp_connection.hpp"

namespace ellohim
{
    using json = nlohmann::json;

    ProtocolHandler::ProtocolHandler(std::shared_ptr<MessageBroker> broker)
        : broker(std::move(broker)) 
    {}

    void ProtocolHandler::handle_message(std::shared_ptr<TcpConnection> conn, const std::string& data)
    {
        try
        {
            auto j = json::parse(data);
            std::string type = j.value("type", "");
            std::string topic = j.value("topic", "");
            LOG(INFO) << data;
            if (type == "publish") 
            {
                std::string payload = j.value("payload", "");
                broker->publish(topic, payload);
            }
            else if (type == "subscribe") 
            {
                broker->subscribe(topic, conn);
            }
            else 
            {
                conn->send("{\"error\":\"unknown type\"}\n");
            }
        }
        catch (std::exception const& e)
        {
            conn->send(std::string("{\"error\":\"json parse failed: ") + e.what() + "\"}\n");
        }
    }
}
