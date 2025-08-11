#pragma once
#include <common.hpp>
#include <thread_pool.hpp>

namespace ellohim
{
    template <typename T>
    struct async;

    class scheduler
    {
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

    public:
        static void schedule(std::coroutine_handle<> h)
        {
            if (h && !h.done()) 
            {
                instance().schedule_impl(h);
            }
        }

        static void schedule_after(std::chrono::milliseconds delay, std::coroutine_handle<> h)
        {
            if (h && !h.done())
            {
                instance().schedule_after_impl(delay, h);
            }
        }

        static void start()
        {
            instance().start_impl();
        }

        static void stop()
        {
            instance().stop_impl();
        }

        static void run()
        {
            instance().run_impl();
        }

        // Metode statis baru untuk memberikan sinyal bahwa sebuah coroutine telah selesai
        static void cleanup_handle(std::coroutine_handle<> h)
        {
            if (h && h.address()) {
                instance().cleanup_handle_impl(h);
            }
        }

    private:
        static scheduler& instance()
        {
            static scheduler s;
            return s;
        }

        void run_impl()
        {
            LOG(VERBOSE) << "[Scheduler] Run() called. Processing tasks until all queues are empty.";
            std::unique_lock<std::mutex> lock(tasks_mutex);
            while (!tasks.empty() || !delayed_tasks.empty() || !cleanup_tasks.empty())
            {
                lock.unlock();
                tick_impl();
                lock.lock();
            }
        }

        void start_impl()
        {
            bool expected = false;
            if (!running.compare_exchange_strong(expected, true)) 
            {
                LOG(WARNING) << "[Scheduler] Already running";
                return;
            }

            LOG(INFO) << "[Scheduler] Starting scheduler";

            g_thread_pool->queue_job([this] {
                LOG(INFO) << "[Scheduler] Scheduler thread started";
                while (running.load())
                {
                    try
                    {
                        tick_impl();
                    }
                    catch (const std::exception& e)
                    {
                        LOG(FATAL) << "[Scheduler] Exception in tick: " << e.what();
                    }
                    catch (...)
                    {
                        LOG(FATAL) << "[Scheduler] Unknown exception in tick";
                    }
                    std::this_thread::yield();
                }
                LOG(INFO) << "[Scheduler] Scheduler thread stopped";
            });
        }

        void stop_impl()
        {
            running.store(false);
        }

        void schedule_impl(std::coroutine_handle<> h) 
        {
            if (!h || h.address() == nullptr) 
            {
                LOG(WARNING) << "[Scheduler] Attempted to schedule invalid handle";
                return;
            }

            if (h.done()) 
            {
                LOG(VERBOSE) << "[Scheduler] Handle already done, not scheduling " << h.address();
                return;
            }

            std::unique_lock<std::mutex> lock(tasks_mutex);

            if (h.done()) 
            {
                LOG(VERBOSE) << "[Scheduler] Handle became done while waiting for lock " << h.address();
                return;
            }

            auto addr = h.address();
            if (handle_refs.find(addr) == handle_refs.end()) 
            {
                handle_refs[addr] = 1;
                tasks.push(h);
                LOG(VERBOSE) << "[Scheduler] Scheduled new task " << addr << " (queue size: " << tasks.size() << ")";
            }
            else 
            {
                LOG(WARNING) << "[Scheduler] Handle already in queue " << addr;
            }
        }

        void schedule_after_impl(std::chrono::milliseconds delay, std::coroutine_handle<> h) 
        {
            if (!h || h.address() == nullptr)
            {
                LOG(WARNING) << "[Scheduler] Attempted to schedule_after invalid handle";
                return;
            }

            if (h.done())
            {
                LOG(VERBOSE) << "[Scheduler] Handle already done, not scheduling delayed " << h.address();
                return;
            }

            std::lock_guard<std::mutex> lock(delayed_mutex);
            auto time = Clock::now() + delay;
            delayed_tasks.emplace(time, h);
            LOG(VERBOSE) << "[Scheduler] Scheduled delayed task " << h.address() << " for " << delay.count() << "ms";
        }

        void cleanup_handle_impl(std::coroutine_handle<> h) 
        {
            std::lock_guard<std::mutex> lock(cleanup_mutex);
            cleanup_tasks.push(h);
            LOG(VERBOSE) << "[Scheduler] Task signaled for cleanup " << h.address();
        }

        void tick_impl()
        {
            process_cleanup_tasks();
            process_immediate_tasks();
            process_delayed_tasks();
        }

        void process_cleanup_tasks() {
            std::queue<std::coroutine_handle<>> local_cleanup_tasks;
            {
                std::lock_guard<std::mutex> lock(cleanup_mutex);
                local_cleanup_tasks.swap(cleanup_tasks);
            }

            std::lock_guard<std::mutex> tasks_lock(tasks_mutex);
            while (!local_cleanup_tasks.empty())
            {
                auto h = local_cleanup_tasks.front();
                local_cleanup_tasks.pop();

                auto addr = h.address();
                auto it = handle_refs.find(addr);
                if (it != handle_refs.end())
                {
                    it->second--;
                    if (it->second <= 0) 
                    {
                        handle_refs.erase(it);
                        if (h && h.address())
                        {
                            LOG(INFO) << "[Scheduler] Destroying coroutine handle " << addr;
                            h.destroy();
                        }
                    }
                }
            }
        }

        void process_immediate_tasks()
        {
            std::vector<std::coroutine_handle<>> local_tasks;

            {
                std::lock_guard<std::mutex> lock(tasks_mutex);
                if (tasks.empty()) return;

                while (!tasks.empty())
                {
                    auto h = tasks.front();
                    tasks.pop();

                    if (!h || !h.address() || h.done()) 
                    {
                        LOG(WARNING) << "[Scheduler] Invalid or done handle in queue, skipping";
                        continue;
                    }

                    auto addr = h.address();
                    if (handle_refs.count(addr) > 0)
                    {
                        local_tasks.push_back(h);
                    }
                    else
                    {
                        LOG(WARNING) << "[Scheduler] Handle not in refs, skipping " << addr;
                    }
                }

                LOG(VERBOSE) << "[Scheduler] Processing " << local_tasks.size() << " valid tasks";
            }

            for (auto h : local_tasks) 
            {
                if (!h || !h.address())
                {
                    continue;
                }

                try 
                {
                    if (h.done()) 
                    {
                        LOG(VERBOSE) << "[Scheduler] Task completed before resume " << h.address();
                        continue;
                    }

                    LOG(VERBOSE) << "[Scheduler] Resuming task " << h.address();
                    h.resume();
                    LOG(VERBOSE) << "[Scheduler] Successfully resumed task " << h.address();
                }
                catch (const std::exception& e)
                {
                    LOG(FATAL) << "[Scheduler] Exception during resume " << h.address() << ": " << e.what();
                    std::lock_guard<std::mutex> lock(tasks_mutex);
                    auto addr = h.address();
                    auto it = handle_refs.find(addr);
                    if (it != handle_refs.end())
                    {
                        it->second--;
                        if (it->second <= 0) 
                        {
                            handle_refs.erase(it);
                            if (h && h.address()) 
                            {
                                h.destroy();
                            }
                        }
                    }
                }
                catch (...)
                {
                    LOG(FATAL) << "[Scheduler] Unknown exception during resume " << h.address();
                    std::lock_guard<std::mutex> lock(tasks_mutex);
                    auto addr = h.address();
                    auto it = handle_refs.find(addr);
                    if (it != handle_refs.end())
                    {
                        it->second--;
                        if (it->second <= 0)
                        {
                            handle_refs.erase(it);
                            if (h && h.address()) 
                            {
                                h.destroy();
                            }
                        }
                    }
                }
            }
        }

        void process_delayed_tasks()
        {
            auto now = Clock::now();
            std::vector<std::coroutine_handle<>> ready_tasks;

            {
                std::lock_guard<std::mutex> lock(delayed_mutex);
                while (!delayed_tasks.empty() && delayed_tasks.top().first <= now)
                {
                    auto h = delayed_tasks.top().second;
                    delayed_tasks.pop();

                    if (!h || h.address() == nullptr)
                    {
                        LOG(WARNING) << "[Scheduler] Invalid delayed task handle, skipping";
                        continue;
                    }

                    try 
                    {
                        if (!h.done())
                        {
                            ready_tasks.push_back(h);
							LOG(VERBOSE) << "[Scheduler] Delayed task ready to run " << h.address();
                        }
                        else 
                        {
                            LOG(VERBOSE) << "[Scheduler] Delayed task already done, skipping " << h.address();
                        }
                    }
                    catch (...) 
                    {
                        LOG(FATAL) << "[Scheduler] Exception checking delayed task status, skipping";
                    }
                }
            }

            for (auto h : ready_tasks)
            {
                //schedule_impl(h);
				h.resume();
            }
        }

    private:
        std::queue<std::coroutine_handle<>> tasks;
        std::priority_queue<
            std::pair<TimePoint, std::coroutine_handle<>>,
            std::vector<std::pair<TimePoint, std::coroutine_handle<>>>,
            std::greater<>
        > delayed_tasks;
        std::queue<std::coroutine_handle<>> cleanup_tasks;
        std::unordered_map<void*, int> handle_refs;

        std::mutex tasks_mutex;
        std::mutex delayed_mutex;
        std::mutex cleanup_mutex;
        std::atomic<bool> running{ false };
    };

    template <typename T>
    struct CoroutineState
    {
        std::atomic<bool> completed{ false };
        std::atomic<bool> exception_set{ false };
        std::optional<T> value;
        std::exception_ptr exception;
        std::coroutine_handle<> continuation = nullptr;
        mutable std::mutex mutex;

        T result()
        {
            std::unique_lock<std::mutex> lock(mutex);

            auto start = std::chrono::steady_clock::now();
            while (!completed.load()) 
            {
                lock.unlock();

                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(30))
                {
                    throw std::runtime_error("Coroutine result timeout");
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                lock.lock();
            }

            if (exception_set.load() && exception) 
            {
                std::rethrow_exception(exception);
            }

            if (!value.has_value()) {
                throw std::runtime_error("Coroutine completed but no value set");
            }

            return *value;
        }
    };

    template <>
    struct CoroutineState<void>
    {
        std::atomic<bool> completed{ false };
        std::atomic<bool> exception_set{ false };
        std::exception_ptr exception;
        std::coroutine_handle<> continuation = nullptr;
        mutable std::mutex mutex;

        void result()
        {
            std::unique_lock<std::mutex> lock(mutex);

            auto start = std::chrono::steady_clock::now();
            while (!completed.load()) 
            {
                lock.unlock();

                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(30)) 
                {
                    throw std::runtime_error("Coroutine result timeout");
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                lock.lock();
            }

            if (exception_set.load() && exception)
            {
                std::rethrow_exception(exception);
            }
        }
    };

    template <typename T>
    struct TaskPromise
    {
        using CoroHandle = std::coroutine_handle<TaskPromise<T>>;

        TaskPromise() : state(std::make_shared<CoroutineState<T>>()) {}

        async<T> get_return_object();

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter
        {
            bool await_ready() noexcept { return false; }

            template <typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept
            {
                auto& promise = h.promise();
                std::coroutine_handle<> continuation = nullptr;

                try 
                {
                    std::lock_guard<std::mutex> lock(promise.state->mutex);
                    promise.state->completed.store(true);
                    continuation = promise.state->continuation;
                }
                catch (...)
                {
                    promise.state->completed.store(true);
                }

                scheduler::cleanup_handle(h);

                return continuation ? continuation : std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };

        auto final_suspend() noexcept { return FinalAwaiter{}; }

        void return_value(T val)
        {
            try
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->value = std::move(val);
            }
            catch (...)
            {
                state->exception = std::current_exception();
                state->exception_set.store(true);
            }
        }

        void unhandled_exception() 
        {
            try
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->exception = std::current_exception();
                state->exception_set.store(true);
            }
            catch (...) 
            {
                state->exception_set.store(true);
            }
        }

        T result() 
        {
            return state->result();
        }

        std::shared_ptr<CoroutineState<T>> state;
    };

    template <>
    struct TaskPromise<void> 
    {
        using CoroHandle = std::coroutine_handle<TaskPromise<void>>;

        TaskPromise() : state(std::make_shared<CoroutineState<void>>()) {}

        async<void> get_return_object();

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter
        {
            bool await_ready() noexcept { return false; }

            template <typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
                auto& promise = h.promise();
                std::coroutine_handle<> continuation = nullptr;

                try
                {
                    std::lock_guard<std::mutex> lock(promise.state->mutex);
                    promise.state->completed.store(true);
                    continuation = promise.state->continuation;
                }
                catch (...) 
                {
                    promise.state->completed.store(true);
                }

                scheduler::cleanup_handle(h);

                return continuation ? continuation : std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };

        auto final_suspend() noexcept { return FinalAwaiter{}; }

        void return_void() {}

        void unhandled_exception()
        {
            try 
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->exception = std::current_exception();
                state->exception_set.store(true);
            }
            catch (...) 
            {
                state->exception_set.store(true);
            }
        }

        void result() {
            state->result();
        }

        std::shared_ptr<CoroutineState<void>> state;
    };

    template <typename T = void>
    struct async
    {
        using promise_type = TaskPromise<T>;
        using handle_type = std::coroutine_handle<promise_type>;

        async(handle_type h) : coro(h), state(h ? h.promise().state : nullptr)
        {
            if (state && coro)
            {
                LOG(VERBOSE) << "[Async] Created async task " << coro.address();
            }
            else 
            {
                LOG(FATAL) << "[Async] Failed to create async task - invalid handle or state";
            }
        }

        async(const async&) = delete;
        async& operator=(const async&) = delete;

        async(async&& other) noexcept : coro(other.coro), state(other.state) 
        {
            other.coro = nullptr;
            other.state = nullptr;
        }

        async& operator=(async&& other) noexcept
        {
            if (this != &other) {
                cleanup();
                coro = other.coro;
                state = other.state;
                other.coro = nullptr;
                other.state = nullptr;
            }
            return *this;
        }

        ~async() {
            cleanup();
        }

        bool await_ready() const noexcept
        {
            return !state || state->completed.load();
        }

        void await_suspend(std::coroutine_handle<> h)
        {
            if (!state || !coro || coro.done()) 
            {
                // Jika ada masalah, kembalikan ke scheduler
                scheduler::schedule(h);
                LOG(WARNING) << "[Await] await_suspend with invalid handle or state! Parent: " << h.address();
                return;
            }

            try
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (!state->completed.load()) 
                {
                    state->continuation = h;
                    scheduler::schedule(coro); //kalo dihapus semua co_await tidak akan tereksekusi
                }
                else
                {
                    scheduler::schedule(h);
                }
            }
            catch (...)
            {
                scheduler::schedule(h);
            }
        }

        T await_resume()
        {
            if (!state) {
                throw std::runtime_error("Invalid async state in await_resume");
            }

            return state->result();
        }

        bool valid() const noexcept 
        {
            return coro && state && !state->completed.load();
        }

        bool is_ready() const noexcept
        {
            return !state || state->completed.load();
        }

    private:
        void cleanup() 
        {
            if (coro)
            {
                LOG(VERBOSE) << "[Async] Cleaning up task " << coro.address();
                scheduler::cleanup_handle(coro);
                coro = nullptr;
            }
            state = nullptr;
        }

    public:
        handle_type coro = nullptr;
        std::shared_ptr<CoroutineState<T>> state = nullptr;
    };

    struct SleepAwaiter {
        std::chrono::milliseconds delay;

        bool await_ready() const noexcept { return delay.count() <= 0; }

        void await_suspend(std::coroutine_handle<> h) const
        {
            scheduler::schedule_after(delay, h);
        }

        void await_resume() const noexcept {}
    };

    inline SleepAwaiter sleep_for(std::chrono::milliseconds delay) {
        return SleepAwaiter{ delay };
    }

    template <typename T>
    async<T> TaskPromise<T>::get_return_object() {
        return async<T>{ std::coroutine_handle<TaskPromise<T>>::from_promise(*this) };
    }

    inline async<void> TaskPromise<void>::get_return_object() {
        return async<void>{ std::coroutine_handle<TaskPromise<void>>::from_promise(*this) };
    }

    template <typename T>
    void fire_and_forget(async<T>&& task) {
        if (!task.coro || !task.state) {
            LOG(FATAL) << "[fire_and_forget] Invalid task";
            return;
        }

        LOG(INFO) << "[fire_and_forget] Starting background task " << task.coro.address();

        try
        {
            scheduler::schedule(task.coro);
            LOG(INFO) << "[fire_and_forget] Task scheduled successfully " << task.coro.address();
        }
        catch (const std::exception& e) 
        {
            LOG(FATAL) << "[fire_and_forget] Failed to schedule task: " << e.what();
            return;
        }

        task.coro = nullptr;
        task.state = nullptr;
    }

    template <typename T>
    T run_sync(async<T> task)
    {
        if (!task.coro || !task.state) {
            throw std::runtime_error("Invalid coroutine handle in run_sync");
        }

        LOG(INFO) << "[run_sync] Starting task " << task.coro.address();

        try {
            scheduler::schedule(task.coro);
        }
        catch (const std::exception& e) {
            LOG(FATAL) << "[run_sync] Failed to schedule task: " << e.what();
            throw;
        }

        scheduler::run();

        LOG(INFO) << "[run_sync] Task completed, getting result";
        return task.await_resume();
    }

    inline void run_sync(async<void> task)
    {
        if (!task.coro || !task.state) {
            throw std::runtime_error("Invalid coroutine handle in run_sync");
        }

        LOG(INFO) << "[run_sync] Starting void task " << task.coro.address();

        try {
            scheduler::schedule(task.coro);
        }
        catch (const std::exception& e) {
            LOG(FATAL) << "[run_sync] Failed to schedule task: " << e.what();
            throw;
        }

        scheduler::run();

        LOG(INFO) << "[run_sync] Void task completed";
        task.await_resume();
    }

    template <typename Func>
    void spawn_task(Func&& func) {
        try {
            auto task = [func = std::forward<Func>(func)]() -> async<void> {
                try {
                    co_await func();
                }
                catch (const std::exception& e) {
                    LOG(FATAL) << "[spawn_task] Exception: " << e.what();
                    throw;
                }
                catch (...) {
                    LOG(FATAL) << "[spawn_task] Unknown exception";
                    throw;
                }
                }();

            fire_and_forget(std::move(task));
        }
        catch (const std::exception& e) {
            LOG(FATAL) << "[spawn_task] Failed to create task: " << e.what();
        }
        catch (...) {
            LOG(FATAL) << "[spawn_task] Unknown exception creating task";
        }
    }
}
