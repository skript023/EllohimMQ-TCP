#pragma once
#include <common.hpp>

namespace ellohim
{
    class TcpConnection;

    struct BrokerMessage 
    {
        std::string topic;
        std::string payload;
    };

    class MessageBroker
    {
        using TopicHandler = std::function<AsyncTask(const std::string&)>;
        std::unordered_map<std::string, TopicHandler> topic_handlers;
    public:
        void publish(const std::string& topic, const std::string& message);
        void subscribe(const std::string& topic, std::shared_ptr<TcpConnection> conn);
        void register_topic_handler(const std::string& topic, TopicHandler handler);
    private:
        void dispatch(const std::string& topic);

        std::unordered_map<std::string, std::deque<BrokerMessage>> topic_queues;
        std::unordered_map<std::string, std::vector<std::weak_ptr<TcpConnection>>> subscribers;
        std::mutex broker_mutex;
    };
}
