#pragma once
#include <common.hpp>

namespace ellohim
{
    // Forward declaration for async struct
    template <typename T>
    struct async;

    /**
     * @class scheduler
     * @brief A scheduler for C++20 coroutines with Job Stealing.
     * This version optimizes delayed task processing.
     */
    class scheduler
    {
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

    public:
        // --- Public Methods (User Interface) ---

        /**
         * @brief Schedules a coroutine for immediate execution.
         * @param h The coroutine handle to schedule.
         */
        static void schedule(std::coroutine_handle<> h)
        {
            if (h && !h.done())
            {
                instance().schedule_impl(h);
            }
        }

        /**
         * @brief Schedules a coroutine to run after a specific delay.
         * @param delay The delay in milliseconds.
         * @param h The coroutine handle to schedule.
         */
        static void schedule_after(std::chrono::milliseconds delay, std::coroutine_handle<> h)
        {
            if (h && !h.done())
            {
                instance().schedule_after_impl(delay, h);
            }
        }

        /**
         * @brief Starts the scheduler with a given number of worker threads.
         * @param max_thread The number of threads to use. Default is 1.
         */
        static void start(int max_thread = 1)
        {
            instance().start_impl(max_thread);
        }

        /**
         * @brief Stops all scheduler threads.
         */
        static void stop()
        {
            instance().stop_impl();
        }

        /**
         * @brief Runs the scheduler in synchronous mode until all tasks are complete.
         */
        static void run()
        {
            instance().run_impl();
        }

        /**
         * @brief Marks a coroutine handle for cleanup (destruction).
         * @param h The handle to clean up.
         */
        static void cleanup_handle(std::coroutine_handle<> h)
        {
            if (h && h.address()) {
                instance().cleanup_handle_impl(h);
            }
        }

    private:
        // --- Internal Scheduler Methods ---

        /**
         * @brief Gets the singleton instance of the scheduler.
         */
        static scheduler& instance()
        {
            static scheduler s;
            return s;
        }

        /**
         * @brief Main function for each worker thread, including job stealing.
         * This version uses polling instead of a condition variable.
         * @param thread_idx The index of the current thread.
         * @return The next coroutine handle to run, or nullptr if none.
         */
        std::coroutine_handle<> get_next_task(std::size_t thread_idx)
        {
            // 1. Try to get a task from the local queue.
            std::coroutine_handle<> h = nullptr;
            {
                std::lock_guard<std::mutex> lock(*local_queue_mutexes[thread_idx]);
                if (!local_task_queues[thread_idx].empty())
                {
                    h = local_task_queues[thread_idx].front();
                    local_task_queues[thread_idx].pop_front();
                    return h;
                }
            }

            // 2. If local queue is empty, try to steal a task from another queue.
            for (std::size_t i = 0; i < num_threads.load(); ++i)
            {
                if (i == thread_idx) continue;

                if (local_queue_mutexes[i]->try_lock())
                {
                    if (!local_task_queues[i].empty())
                    {
                        h = local_task_queues[i].back();
                        local_task_queues[i].pop_back();
                        local_queue_mutexes[i]->unlock();
                        LOG(VERBOSE) << "[Scheduler] Thread " << thread_idx << " stole a task from " << i;
                        return h;
                    }
                    local_queue_mutexes[i]->unlock();
                }
            }

            // Polling: Instead of waiting, just return nullptr if no task is found.
            return h;
        }

        /**
         * @brief Processes one scheduler cycle (tick) for a specific thread.
         * @param thread_idx The index of the current thread.
         */
        void tick_impl(std::size_t thread_idx)
        {
            // Check and process delayed tasks first.
            process_delayed_tasks_impl();

            std::coroutine_handle<> h = get_next_task(thread_idx);

            if (h && h.address())
            {
                try
                {
                    if (h.done())
                    {
                        LOG(VERBOSE) << "[Scheduler] Task was already done before resume " << h.address();
                    }
                    else
                    {
                        LOG(VERBOSE) << "[Scheduler] Thread " << thread_idx << " -> Resuming task " << h.address() << "...";
                        h.resume();
                        LOG(VERBOSE) << "[Scheduler] Thread " << thread_idx << " <- Task " << h.address() << " resumed successfully.";
                    }
                }
                catch (const std::exception& e)
                {
                    LOG(FATAL) << "[Scheduler] Exception while resuming " << h.address() << ": " << e.what();
                    cleanup_handle_impl(h);
                }
                catch (...)
                {
                    LOG(FATAL) << "[Scheduler] Unknown exception while resuming " << h.address();
                    cleanup_handle_impl(h);
                }
            }
            else
            {
                // If no task was found, process cleanup tasks and sleep for a short duration
                // to reduce CPU usage. This is the core of the polling loop.
                process_cleanup_tasks_impl();
                std::this_thread::sleep_for(500ms);
            }
        }

        /**
         * @brief Synchronous mode implementation.
         */
        void run_impl()
        {
            LOG(VERBOSE) << "[Scheduler] run() called. Processing tasks until all queues are empty.";
            running.store(true);
            num_threads.store(1);
            local_task_queues.resize(1);
            local_queue_mutexes.clear();
            local_queue_mutexes.emplace_back(std::make_unique<std::mutex>());

            while (true)
            {
                bool has_work = false;
                {
                    std::lock_guard<std::mutex> lock(*local_queue_mutexes[0]);
                    if (!local_task_queues[0].empty()) {
                        has_work = true;
                    }
                }

                // Check all queues before sleeping
                process_delayed_tasks_impl();
                process_cleanup_tasks_impl();

                if (has_work)
                {
                    tick_impl(0);
                }
                else
                {
                    if (local_task_queues[0].empty() && delayed_tasks.empty() && cleanup_tasks.empty()) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            running.store(false);
        }

        /**
         * @brief Starts the scheduler worker threads.
         */
        void start_impl(int max_thread)
        {
            bool expected = false;
            if (!running.compare_exchange_strong(expected, true))
            {
                LOG(WARNING) << "[Scheduler] Already running.";
                return;
            }

            LOG(VERBOSE) << "[Scheduler] Starting scheduler.";
            num_threads.store(max_thread);
            local_task_queues.resize(max_thread);
            local_queue_mutexes.clear();
            local_queue_mutexes.reserve(max_thread);
            for (int i = 0; i < max_thread; ++i) {
                local_queue_mutexes.emplace_back(std::make_unique<std::mutex>());
            }

            for (std::size_t i = 0; i < max_thread; ++i) {
                threads.emplace_back(std::thread(&scheduler::start_pool_impl, this, i));
            }

            LOG(INFO) << "[Scheduler] Successfully started " << max_thread << " scheduler threads.";
        }

        /**
         * @brief Main function for each worker thread.
         */
        void start_pool_impl(std::size_t thread_idx)
        {
            while (running.load())
            {
                tick_impl(thread_idx);
            }
            LOG(VERBOSE) << "[Scheduler] Scheduler thread stopped.";
        }

        /**
         * @brief Stops all worker threads.
         */
        void stop_impl()
        {
            running.store(false);
            // No need to notify, threads will exit naturally due to polling.
            for (auto& t : threads) {
                if (t.joinable()) {
                    t.join();
                }
            }
            threads.clear();
            LOG(INFO) << "[Scheduler] Scheduler stopped.";
        }

        /**
         * @brief Internal implementation for scheduling an immediate task.
         */
        void schedule_impl(std::coroutine_handle<> h)
        {
            if (!h || h.address() == nullptr)
            {
                LOG(WARNING) << "[Scheduler] Attempting to schedule an invalid handle.";
                return;
            }

            if (h.done())
            {
                LOG(VERBOSE) << "[Scheduler] Handle is already done, not scheduling " << h.address();
                return;
            }

            std::size_t thread_idx = next_thread_idx.fetch_add(1) % num_threads.load();
            std::lock_guard<std::mutex> lock(*local_queue_mutexes[thread_idx]);
            auto addr = h.address();

            std::lock_guard<std::mutex> refs_lock(handle_refs_mutex);
            if (handle_refs.find(addr) == handle_refs.end())
            {
                handle_refs[addr] = 1;
                local_task_queues[thread_idx].push_back(h);
                // No need to notify, polling threads will find the task.
                LOG(VERBOSE) << "[Scheduler] Scheduled new task " << addr << " (queue size: " << local_task_queues[thread_idx].size() << ")";
            }
            else
            {
                LOG(WARNING) << "[Scheduler] Handle is already in a queue " << addr;
            }
        }

        /**
         * @brief Internal implementation for scheduling a delayed task.
         */
        void schedule_after_impl(std::chrono::milliseconds delay, std::coroutine_handle<> h)
        {
            if (!h || h.address() == nullptr)
            {
                LOG(WARNING) << "[Scheduler] Attempting to schedule_after an invalid handle.";
                return;
            }

            if (h.done())
            {
                LOG(VERBOSE) << "[Scheduler] Handle is already done, not scheduling delayed task " << h.address();
                return;
            }

            std::lock_guard<std::mutex> lock(delayed_mutex);
            auto time = Clock::now() + delay;
            delayed_tasks.emplace(time, h);
            // No need to notify, polling threads will find the task.
            LOG(VERBOSE) << "[Scheduler] Scheduled delayed task " << h.address() << " for " << delay.count() << "ms";
        }

        /**
         * @brief Internal implementation to mark a handle for cleanup.
         */
        void cleanup_handle_impl(std::coroutine_handle<> h)
        {
            std::lock_guard<std::mutex> lock(cleanup_mutex);
            cleanup_tasks.push(h);
            // No need to notify, polling threads will find the task.
            LOG(VERBOSE) << "[Scheduler] Task marked for cleanup " << h.address();
        }

        /**
         * @brief Processes the handle cleanup queue.
         */
        void process_cleanup_tasks_impl() {
            std::queue<std::coroutine_handle<>> local_cleanup_tasks;
            {
                std::lock_guard<std::mutex> lock(cleanup_mutex);
                local_cleanup_tasks.swap(cleanup_tasks);
            }

            if (local_cleanup_tasks.empty()) {
                return;
            }

            std::lock_guard<std::mutex> refs_lock(handle_refs_mutex);
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
                        if (h && h.address() && h.done())
                        {
                            LOG(VERBOSE) << "[Scheduler] Destroying coroutine handle " << addr;
                            h.destroy();
                        }
                    }
                }
            }
        }

        /**
         * @brief Processes the delayed tasks queue.
         */
        void process_delayed_tasks_impl()
        {
            auto now = Clock::now();
            std::vector<std::coroutine_handle<>> ready_tasks;

            {
                std::lock_guard<std::mutex> lock(delayed_mutex);
                while (!delayed_tasks.empty() && delayed_tasks.top().first <= now)
                {
                    auto h = delayed_tasks.top().second;
                    delayed_tasks.pop();
                    ready_tasks.push_back(h);
                }
            } // Release the lock

            if (!ready_tasks.empty()) {
                // Distribute the ready tasks to the local queues.
                for (auto& h : ready_tasks)
                {
                    if (!h.done())
                    {
                        std::size_t thread_idx = next_thread_idx.fetch_add(1) % num_threads.load();
                        std::lock_guard<std::mutex> local_lock(*local_queue_mutexes[thread_idx]);
                        local_task_queues[thread_idx].push_front(h);
                        // No need to notify again, as the worker thread is already active or about to be awakened by polling.
                        LOG(VERBOSE) << "[Scheduler] Delayed task " << h.address() << " moved to immediate queue.";
                    }
                    else
                    {
                        LOG(VERBOSE) << "[Scheduler] Delayed task was already done, skipping " << h.address();
                    }
                }
            }
        }

    private:
        // --- Data Members ---
        std::vector<std::deque<std::coroutine_handle<>>> local_task_queues;
        std::priority_queue<
            std::pair<TimePoint, std::coroutine_handle<>>,
            std::vector<std::pair<TimePoint, std::coroutine_handle<>>>,
            std::greater<>
        > delayed_tasks;
        std::queue<std::coroutine_handle<>> cleanup_tasks;
        std::unordered_map<void*, int> handle_refs;

        std::vector<std::unique_ptr<std::mutex>> local_queue_mutexes;
        std::mutex delayed_mutex;
        std::mutex cleanup_mutex;
        std::mutex handle_refs_mutex;
        std::mutex data_mutex;
        std::atomic<bool> running{ false };
        // std::condition_variable data_condition; // Dihapus karena menggunakan polling
        std::atomic<std::size_t> num_threads{ 0 };
        std::atomic<std::size_t> next_thread_idx{ 0 };

        std::vector<std::thread> threads;
    };

    // --- Coroutine Structures and Classes ---

    /**
     * @struct CoroutineState
     * @brief Stores the state and return value of a coroutine.
     */
    template <typename T>
    struct CoroutineState
    {
        std::atomic<bool> completed{ false };
        std::atomic<bool> exception_set{ false };
        std::optional<T> value;
        std::exception_ptr exception;
        std::coroutine_handle<> continuation = nullptr;
        std::function<void(T)> on_completed = nullptr;
        mutable std::mutex mutex;
        mutable std::condition_variable cv;

        T result()
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this] { return completed.load(); });

            if (exception_set.load() && exception)
            {
                std::rethrow_exception(exception);
            }

            if (!value.has_value())
            {
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
        std::function<void()> on_completed = nullptr;
        mutable std::mutex mutex;
        mutable std::condition_variable cv;

        void result()
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this] { return completed.load(); });

            if (exception_set.load() && exception)
            {
                std::rethrow_exception(exception);
            }
        }
    };

    /**
     * @struct TaskPromise
     * @brief Promise type for `async`.
     */
    template <typename T>
    struct TaskPromise
    {
        using CoroHandle = std::coroutine_handle<TaskPromise<T>>;
        std::shared_ptr<CoroutineState<T>> state;

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
                    promise.state->cv.notify_all();

                    if (promise.state->on_completed && promise.state->value.has_value())
                    {
                        std::invoke(promise.state->on_completed, promise.state->value.value());
                    }
                }
                catch (...)
                {
                    promise.state->completed.store(true);
                    promise.state->cv.notify_all();
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
    };

    template <>
    struct TaskPromise<void>
    {
        using CoroHandle = std::coroutine_handle<TaskPromise<void>>;
        std::shared_ptr<CoroutineState<void>> state;

        TaskPromise() : state(std::make_shared<CoroutineState<void>>()) {}

        async<void> get_return_object();

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
                    promise.state->cv.notify_all();

                    if (promise.state->on_completed)
                    {
                        std::invoke(promise.state->on_completed);
                    }
                }
                catch (...)
                {
                    promise.state->completed.store(true);
                    promise.state->cv.notify_all();
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

        void result()
        {
            state->result();
        }
    };

    /**
     * @struct async
     * @brief Coroutine return type.
     */
    template <typename T = void>
    struct async
    {
        using promise_type = TaskPromise<T>;
        using handle_type = std::coroutine_handle<promise_type>;

        async(handle_type h) : coro(h), state(h ? h.promise().state : nullptr)
        {
            if (state && coro)
            {
                LOG(VERBOSE) << "[Async] async task created " << coro.address();
            }
            else
            {
                LOG(FATAL) << "[Async] Failed to create async task - invalid handle or state";
            }
        }

        ~async()
        {
            cleanup();
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
            if (this != &other)
            {
                cleanup();
                coro = other.coro;
                state = other.state;
                other.coro = nullptr;
                other.state = nullptr;
            }
            return *this;
        }

        void operator()()
        {
            if (coro && state)
            {
                scheduler::schedule(coro);
                coro = nullptr;
                state = nullptr;
            }
        }

        template <typename... Args>
        void operator()(Args&&... args)
        {
            if (coro && state)
            {
                scheduler::schedule(coro);
                coro = nullptr;
                state = nullptr;
            }
        }

        void detach()
        {
            if (coro && state)
            {
                scheduler::schedule(coro);
                coro = nullptr;
                state = nullptr;
            }
        }

        void then(std::function<void(T)> callback)
        {
            if (!state) return;

            std::lock_guard<std::mutex> lock(state->mutex);
            state->on_completed = std::move(callback);
            if (state->completed.load()) {
                state->on_completed(state->result());
            }

            detach();
        }

        bool await_ready() const noexcept
        {
            return !state || state->completed.load();
        }

        void await_suspend(std::coroutine_handle<> h)
        {
            if (!state || !coro || coro.done())
            {
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
                    scheduler::schedule(coro);
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

    template <>
    struct async<void>
    {
        using promise_type = TaskPromise<void>;
        using handle_type = std::coroutine_handle<promise_type>;

        async(handle_type h) : coro(h), state(h ? h.promise().state : nullptr)
        {
            if (state && coro)
            {
                LOG(VERBOSE) << "[Async] async task created " << coro.address();
            }
            else
            {
                LOG(FATAL) << "[Async] Failed to create async task - invalid handle or state";
            }
        }

        ~async()
        {
            cleanup();
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
            if (this != &other)
            {
                cleanup();
                coro = other.coro;
                state = other.state;
                other.coro = nullptr;
                other.state = nullptr;
            }
            return *this;
        }

        void operator()()
        {
            if (coro && state)
            {
                scheduler::schedule(coro);
                coro = nullptr;
                state = nullptr;
            }
        }

        template <typename... Args>
        void operator()(Args&&... args)
        {
            if (coro && state)
            {
                scheduler::schedule(coro);
                coro = nullptr;
                state = nullptr;
            }
        }

        void detach()
        {
            if (coro && state)
            {
                scheduler::schedule(coro);
                coro = nullptr;
                state = nullptr;
            }
        }

        void then(std::function<void()> callback)
        {
            if (!state) return;

            std::lock_guard<std::mutex> lock(state->mutex);
            state->on_completed = std::move(callback);
            if (state->completed.load()) {
                state->on_completed();
            }

            detach();
        }

        bool await_ready() const noexcept
        {
            return !state || state->completed.load();
        }

        void await_suspend(std::coroutine_handle<> h)
        {
            if (!state || !coro || coro.done())
            {
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
                    scheduler::schedule(coro);
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

        void await_resume()
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
        std::shared_ptr<CoroutineState<void>> state = nullptr;
    };

    struct AsyncTask
    {
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;

        AsyncTask() = default;

        AsyncTask(handle_type h) : coro_(h)
        {
        }

        AsyncTask(const AsyncTask&) = delete;

        AsyncTask(AsyncTask&& other) noexcept
        {
            coro_ = other.coro_;
            other.coro_ = nullptr;
        }

        AsyncTask& operator=(const AsyncTask&) = delete;

        AsyncTask& operator=(AsyncTask&& other) noexcept
        {
            if (std::addressof(other) == this)
                return *this;

            coro_ = other.coro_;
            other.coro_ = nullptr;
            return *this;
        }

        struct promise_type
        {
            AsyncTask get_return_object() noexcept
            {
                return { std::coroutine_handle<promise_type>::from_promise(*this) };
            }

            std::suspend_never initial_suspend() const noexcept
            {
                return {};
            }

            void unhandled_exception()
            {
                LOG(FATAL) << "Exception escaping AsyncTask.";
                std::terminate();
            }

            void return_void() noexcept
            {
            }

            std::suspend_never final_suspend() const noexcept
            {
                return {};
            }
        };

        handle_type coro_;
    };
    // --- Awaiters for Flow Control ---

    /**
     * @struct SleepAwaiter
     * @brief Awaiter to pause a coroutine's execution.
     */
    struct SleepAwaiter
    {
        std::chrono::milliseconds delay;

        bool await_ready() const noexcept { return delay.count() <= 0; }

        void await_suspend(std::coroutine_handle<> h) const
        {
            scheduler::schedule_after(delay, h);
        }

        void await_resume() const noexcept {}
    };

    /**
     * @struct YieldAwaiter
     * @brief Awaiter to yield control back to the scheduler.
     */
    struct YieldAwaiter
    {
        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) const
        {
            scheduler::schedule_after(0ms, h);
        }

        void await_resume() const noexcept {}
    };

    /**
     * @brief Helper function for yielding.
     */
    inline YieldAwaiter yield()
    {
        return YieldAwaiter{};
    }

    /**
     * @brief Helper function to sleep for a duration.
     */
    inline SleepAwaiter sleep_for(std::chrono::milliseconds delay)
    {
        return SleepAwaiter{ delay };
    }

    // --- Helper Functions for Managing Coroutines ---

    template <typename T>
    async<T> TaskPromise<T>::get_return_object()
    {
        return async<T>{ std::coroutine_handle<TaskPromise<T>>::from_promise(*this) };
    }

    inline async<void> TaskPromise<void>::get_return_object()
    {
        return async<void>{ std::coroutine_handle<TaskPromise<void>>::from_promise(*this) };
    }

    /**
     * @brief Runs a task without waiting for its result.
     */
    template <typename T>
    void fire_and_forget(async<T>&& task)
    {
        if (!task.coro || !task.state) {
            LOG(FATAL) << "[fire_and_forget] Invalid task.";
            return;
        }

        LOG(VERBOSE) << "[fire_and_forget] Starting background task " << task.coro.address();

        try
        {
            scheduler::schedule(task.coro);
            LOG(VERBOSE) << "[fire_and_forget] Task successfully scheduled " << task.coro.address();
        }
        catch (const std::exception& e)
        {
            LOG(FATAL) << "[fire_and_forget] Failed to schedule task: " << e.what();
            return;
        }

        // Prevent double destruction
        task.coro = nullptr;
        task.state = nullptr;
    }

    /**
     * @brief Runs a task synchronously, blocking until it completes.
     */
    template <typename T>
    T run_sync(async<T> task)
    {
        if (!task.coro || !task.state) {
            throw std::runtime_error("Invalid coroutine handle in run_sync");
        }

        LOG(VERBOSE) << "[run_sync] Starting task " << task.coro.address();

        try {
            scheduler::schedule(task.coro);
        }
        catch (const std::exception& e) {
            LOG(FATAL) << "[run_sync] Failed to schedule task: " << e.what();
            throw;
        }

        scheduler::run();

        LOG(VERBOSE) << "[run_sync] Task finished, getting result";
        return task.await_resume();
    }

    inline void run_sync(async<void> task)
    {
        if (!task.coro || !task.state) {
            throw std::runtime_error("Invalid coroutine handle in run_sync");
        }

        LOG(VERBOSE) << "[run_sync] Starting void task " << task.coro.address();

        try {
            scheduler::schedule(task.coro);
        }
        catch (const std::exception& e) {
            LOG(FATAL) << "[run_sync] Failed to schedule task: " << e.what();
            throw;
        }

        scheduler::run();

        LOG(VERBOSE) << "[run_sync] Void task finished";
        task.await_resume();
    }

    /**
     * @brief Creates and runs a task as a background process.
     */
    template <typename Func>
    void spawn_task(Func&& func)
    {
        try
        {
            auto task = [func = std::forward<Func>(func)]() -> async<void> {
                try
                {
                    co_await func();
                }
                catch (const std::exception& e)
                {
                    LOG(FATAL) << "[spawn_task] Exception: " << e.what();
                    throw;
                }
                catch (...)
                {
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
            LOG(FATAL) << "[spawn_task] Unknown exception while creating task";
        }
    }
}
