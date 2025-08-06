#pragma once
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace ellohim
{
    class TcpConnection;
    class MessageBroker;

    class ProtocolHandler {
    public:
        explicit ProtocolHandler(std::shared_ptr<MessageBroker> broker);
        void handle_message(std::shared_ptr<TcpConnection> conn, const std::string& data);

    private:
        std::shared_ptr<MessageBroker> broker;
    };
}