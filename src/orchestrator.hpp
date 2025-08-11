#pragma once

namespace ellohim
{
    class orchestrator
    {
        static orchestrator& instance()
        {
            static orchestrator orch;
            return orch;
        }
    public:
        using handle_type = std::coroutine_handle<>;

		static void enqueue(handle_type h) { instance().enqueue_impl(h); }
		static void run() { instance().run_impl(); }
		static void run_async() { instance().run_async_impl(); }
		static void stop() { instance().stop_impl(); }
		static bool empty() { return instance().empty_impl(); }
    private:
        void enqueue_impl(handle_type h);
        void run_impl();           // blocking loop
        void run_async_impl();     // run in background thread
        void stop_impl();          // stop the loop
        bool empty_impl();
    private:
        void loop();

        std::queue<handle_type> queue_;
        std::mutex mutex_;
        std::condition_variable cv_;
        bool running_ = false;
        bool stopped_ = false;
        std::thread runner_;
    };
}
