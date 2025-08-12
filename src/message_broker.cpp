#include "message_broker.hpp"
#include "tcp_connection.hpp"

namespace ellohim
{
    void MessageBroker::publish(const std::string& topic, const std::string& message)
    {
        // Kalau ada handler, jalankan langsung
        if (auto it = topic_handlers.find(topic); it != topic_handlers.end())
        {
            it->second(message).detach();
            return;
        }

        // Kalau tidak ada handler, push ke subscriber
        topic_queues[topic].push_back({ topic, message });
        dispatch(topic);
    }

    void MessageBroker::subscribe(const std::string& topic, std::shared_ptr<TcpConnection> conn) 
    {
        subscribers[topic].push_back(conn); // otomatis jadi weak_ptr (dari shared_ptr)
        LOG(INFO) << "[Broker] New subscriber on topic: " << topic;
    }

    void MessageBroker::register_topic_handler(const std::string& topic, TopicHandler handler)
    {
        topic_handlers[topic] = std::move(handler);
    }

    void MessageBroker::dispatch(const std::string& topic) 
    {
        auto& queue = topic_queues[topic];
        if (queue.empty()) return;

        auto& subs = subscribers[topic];

        while (!queue.empty()) 
        {
            auto message = queue.front();
            queue.pop_front();

            for (auto it = subs.begin(); it != subs.end();)
            {
                if (auto conn = it->lock()) 
                {
                    conn->send(message.payload + "\n");
                    ++it;
                }
                else
                {
                    it = subs.erase(it); // remove disconnected
                }
            }
        }
    }

}
