#include "consumer.hpp"
#include "consumers.hpp"

namespace ellohim::topic
{
	class Hello : public consumer
	{
		using consumer::consumer;

		virtual Task<> on_call(std::string payload) override
		{
            LOG(INFO) << "Execute topic " << get_name();
            LOG(INFO) << "[Handler] Task started with payload: " << payload;

            try
            {
                LOG(INFO) << "[Handler] About to sleep";
                for (size_t i = 0; i < 10; i++)
                {
                    LOG(INFO) << "[Handler] Sleeping for 1 second, iteration " << i + 1;
                    co_await sleepCoro(1s);
                }
                //co_await sleep_for(2s);
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
		}
	};

    static Hello _Hello{ "task.hello", "Hello", "Hello" };
}