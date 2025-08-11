#include "orchestrator.hpp"

namespace ellohim
{
    void orchestrator::enqueue_impl(handle_type h)
    {
        if (!h || h.done()) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(h);
        }
        cv_.notify_one();
    }

    void orchestrator::run_impl()
    {
        running_ = true;
        loop();
    }

    void orchestrator::run_async_impl()
    {
        running_ = true;
        runner_ = std::thread([this] { loop(); });
    }

    void orchestrator::stop_impl()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();

		if (runner_.joinable())
		{
			runner_.join();
		}
    }

    bool orchestrator::empty_impl()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    void orchestrator::loop()
    {
        while (true)
        {
            handle_type h = nullptr;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return !queue_.empty() || stopped_; });

                if (stopped_ && queue_.empty())
                    break;

                h = queue_.front();
                queue_.pop();
            }

			if (!h || h.done())
			{
				continue; // skip if already done
			}
            h.resume();
            LOG(INFO) << "Coroutine resumed: " << h.address();
            if (!h.done())
            {
				LOG(INFO) << "Re-queueing coroutine: " << h.address();
                enqueue(h);
            }
        }
    }
}
