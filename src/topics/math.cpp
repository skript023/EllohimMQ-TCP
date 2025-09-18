#include "consumer.hpp"
#include "consumers.hpp"

namespace ellohim::topic
{
    class Math : public consumer
    {
        using consumer::consumer;

        static Task<int> test()
        {
            LOG(INFO) << "exec test function";
            ///co_await sleep_for(1s); // Tambahkan delay untuk testing
            LOG(INFO) << "test function completed";
            co_return 11;
        }

        virtual Task<> on_call(std::string payload) override
        {
            LOG(INFO) << "[Task] Starting math.add with payload: " << payload;

            LOG(INFO) << "[Handler] About to call test()";
            auto v = co_await test();
            LOG(INFO) << "[Handler] test() completed value " << v;

            auto pos = payload.find(',');
            if (pos != std::string::npos) 
            {
                int a = std::stoi(payload.substr(0, pos));
                int b = std::stoi(payload.substr(pos + 1));
                int sum = a + b + v;
                LOG(INFO) << "[Task] math.add result: " << sum;
            }
            //LOG(INFO) << "[Handler] test() called, waiting for completion";

            co_return;
        }
    };

    static Math _Math{ "math.add", "Math Add", "Math Add" };
}