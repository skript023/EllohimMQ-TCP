#pragma once
#include <coroutine>
#include <exception>
#include <optional>
#include <queue>
#include <iostream>

namespace ellohim
{
    class scheduler
    {
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
    public:
        static void schedule(std::coroutine_handle<> h)
        {
            instance().schedule_impl(h);
        }
        static void schedule_after(std::chrono::milliseconds delay, std::coroutine_handle<> h)
        {
            instance().schedule_after_impl(delay, h);
        }
        static void run()
        {
            instance().run_impl();
        }
        static void tick()
        {
            instance().tick_impl();
        }
    private:

        static scheduler& instance() {
            static scheduler s;
            return s;
        }

        void schedule_impl(std::coroutine_handle<> h) {
            tasks.push(h);
        }

        void schedule_after_impl(std::chrono::milliseconds delay, std::coroutine_handle<> h) {
            auto time = Clock::now() + delay;
            delayed_tasks.emplace(time, h);
        }

        void run_impl()
        {
            while (!tasks.empty() || !delayed_tasks.empty()) {
                // Resume immediate tasks first
                while (!tasks.empty()) {
                    auto h = tasks.front();
                    tasks.pop();
                    h.resume();
                }

                // Check delayed tasks
                if (!delayed_tasks.empty())
                {
                    auto now = Clock::now();
                    while (!delayed_tasks.empty() && delayed_tasks.top().first <= now) {
                        auto h = delayed_tasks.top().second;
                        delayed_tasks.pop();
                        h.resume();
                    }
                }
            }
        }

        void tick_impl()
        {
            // Resume immediate tasks
            if (!tasks.empty()) {
                auto h = tasks.front();
                tasks.pop();
                h.resume();
            }

            // Resume delayed tasks whose time has come
            auto now = Clock::now();
            while (!delayed_tasks.empty() && delayed_tasks.top().first <= now) {
                auto h = delayed_tasks.top().second;
                delayed_tasks.pop();
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
    };

    template <typename T>
    struct async;

    template <typename T>
    struct TaskPromise {
        using CoroHandle = std::coroutine_handle<TaskPromise<T>>;

        async<T> get_return_object();

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }

            template <typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
                return h.promise().continuation ? h.promise().continuation : std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };

        auto final_suspend() noexcept { return FinalAwaiter{}; }

        void return_value(T val) { value = std::move(val); }

        void unhandled_exception() { exception = std::current_exception(); }

        T result() {
            if (exception) std::rethrow_exception(exception);
            return *value;
        }

        std::optional<T> value;
        std::exception_ptr exception;
        std::coroutine_handle<> continuation = nullptr;
    };

    template <>
    struct TaskPromise<void> {
        using CoroHandle = std::coroutine_handle<TaskPromise<void>>;

        async<void> get_return_object();

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }

            template <typename Promise>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
                return h.promise().continuation ? h.promise().continuation : std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };

        auto final_suspend() noexcept { return FinalAwaiter{}; }

        void return_void() {}

        void unhandled_exception() { exception = std::current_exception(); }

        void result() {
            if (exception) std::rethrow_exception(exception);
        }

        std::exception_ptr exception;
        std::coroutine_handle<> continuation = nullptr;
    };

    template <typename T = void>
    struct async
    {
        using promise_type = TaskPromise<T>;
        using handle_type = std::coroutine_handle<promise_type>;

        async(handle_type h) : coro(h) {}

        async(async&& other) noexcept : coro(other.coro) { other.coro = nullptr; }

        async& operator=(async&& other) noexcept {
            if (this != &other) {
                if (coro) coro.destroy();
                coro = other.coro;
                other.coro = nullptr;
            }
            return *this;
        }

        ~async() {
            if (coro) coro.destroy();
        }

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) {
            coro.promise().continuation = h;
            scheduler::schedule(coro);
        }

        T await_resume() {
            return coro.promise().result();
        }

        handle_type coro;
    };

    struct SleepAwaiter {
        std::chrono::milliseconds delay;

        bool await_ready() const noexcept { return delay.count() <= 0; }

        void await_suspend(std::coroutine_handle<> h) const {
            scheduler::schedule_after(delay, h);
        }

        void await_resume() const noexcept {}
    };

    inline SleepAwaiter sleep_for(std::chrono::milliseconds delay) {
        return SleepAwaiter{ delay };
    }

    // Return object untuk TaskPromise
    template <typename T>
    async<T> TaskPromise<T>::get_return_object() {
        return async<T>{ std::coroutine_handle<TaskPromise<T>>::from_promise(*this) };
    }

    inline async<void> TaskPromise<void>::get_return_object() {
        return async<void>{ std::coroutine_handle<TaskPromise<void>>::from_promise(*this) };
    }

    // Run coroutine synchronously (blocking until done)
    template <typename T>
    T run_sync(async<T> task)
    {
        if (!task.coro || task.coro.done())
        {
            throw std::runtime_error("Invalid coroutine handle in run_sync");
        }
        scheduler::schedule(task.coro);
        scheduler::run();
        return task.coro.promise().result();
    }

    inline void run_sync(async<void> task)
    {
        if (!task.coro || task.coro.done())
        {
            throw std::runtime_error("Invalid coroutine handle in run_sync");
        }
        scheduler::schedule(task.coro);
        scheduler::run();
        task.coro.promise().result();
    }
}