#pragma once
#include "thread_pool.hpp"
#include "scheduler.hpp"

namespace ellohim
{
    template <typename T>
    auto getAwaiterImpl(T&& value) noexcept(
        noexcept(static_cast<T&&>(value).operator co_await()))
        -> decltype(static_cast<T&&>(value).operator co_await())
    {
        return static_cast<T&&>(value).operator co_await();
    }

    template <typename T>
    auto getAwaiterImpl(T&& value) noexcept(
        noexcept(operator co_await(static_cast<T&&>(value))))
        -> decltype(operator co_await(static_cast<T&&>(value)))
    {
        return operator co_await(static_cast<T&&>(value));
    }

    template <typename T>
    auto getAwaiter(T&& value) noexcept(
        noexcept(getAwaiterImpl(static_cast<T&&>(value))))
        -> decltype(getAwaiterImpl(static_cast<T&&>(value)))
    {
        return getAwaiterImpl(static_cast<T&&>(value));
    }

    template <typename T>
    struct await_result
    {
        using awaiter_t = decltype(getAwaiter(std::declval<T>()));
        using type = decltype(std::declval<awaiter_t>().await_resume());
    };

    template <typename T>
    using await_result_t = typename await_result<T>::type;

    template <typename T, typename = std::void_t<>>
    struct is_awaitable : std::false_type
    {
    };

    template <typename T>
    struct is_awaitable<
        T,
        std::void_t<decltype(getAwaiter(std::declval<T>()))>>
        : std::true_type
    {
    };

    template <typename T>
    constexpr bool is_awaitable_v = is_awaitable<T>::value;

    struct final_awaiter
    {
        bool await_ready() noexcept
        {
            return false;
        }

        template <typename T>
        auto await_suspend(std::coroutine_handle<T> handle) noexcept
        {
            return handle.promise().continuation_;
        }

        void await_resume() noexcept
        {
        }
    };

    template <typename Promise>
    struct task_awaiter
    {
        using handle_type = std::coroutine_handle<Promise>;

    public:
        explicit task_awaiter(handle_type coro) : coro_(coro)
        {
        }

        bool await_ready() noexcept
        {
            return !coro_ || coro_.done();
        }

        auto await_suspend(std::coroutine_handle<> handle) noexcept
        {
            coro_.promise().setContinuation(handle);
            return coro_;
        }

        auto await_resume()
        {
            if constexpr (std::is_void_v<decltype(coro_.promise().result())>)
            {
                coro_.promise().result();  // throw exception if any
                return;
            }
            else
            {
                return std::move(coro_.promise().result());
            }
        }

    private:
        handle_type coro_;
    };

    template <typename T = void>
    struct [[nodiscard]] Task
    {
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;

        Task(handle_type h) : coro_(h)
        {
        }

        Task(const Task&) = delete;

        Task(Task&& other) noexcept
        {
            coro_ = other.coro_;
            other.coro_ = nullptr;
        }

        ~Task()
        {
            if (coro_)
                coro_.destroy();
        }

        Task& operator=(const Task&) = delete;

        Task& operator=(Task&& other) noexcept
        {
            if (std::addressof(other) == this)
                return *this;
            if (coro_)
                coro_.destroy();

            coro_ = other.coro_;
            other.coro_ = nullptr;
            return *this;
        }

        struct promise_type
        {
            Task<T> get_return_object()
            {
                return Task<T>{handle_type::from_promise(*this)};
            }

            std::suspend_always initial_suspend()
            {
                return {};
            }

            void return_value(const T& v)
            {
                value = v;
            }

            void return_value(T&& v)
            {
                value = std::move(v);
            }

            auto final_suspend() noexcept
            {
                return final_awaiter{};
            }

            void unhandled_exception()
            {
                exception_ = std::current_exception();
            }

            T&& result()&&
            {
                if (exception_ != nullptr)
                    std::rethrow_exception(exception_);
                assert(value.has_value() == true);
                return std::move(value.value());
            }

            T& result()&
            {
                if (exception_ != nullptr)
                    std::rethrow_exception(exception_);
                assert(value.has_value() == true);
                return value.value();
            }

            void setContinuation(std::coroutine_handle<> handle)
            {
                continuation_ = handle;
            }

            std::optional<T> value;
            std::exception_ptr exception_;
            std::coroutine_handle<> continuation_{ std::noop_coroutine() };
        };

        auto operator co_await() const noexcept
        {
            return task_awaiter(coro_);
        }

        handle_type coro_;
    };

    template <>
    struct [[nodiscard]] Task<void>
    {
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;

        Task(handle_type handle) : coro_(handle)
        {
        }

        Task(const Task&) = delete;

        Task(Task&& other) noexcept
        {
            coro_ = other.coro_;
            other.coro_ = nullptr;
        }

        ~Task()
        {
            if (coro_)
                coro_.destroy();
        }

        Task& operator=(const Task&) = delete;

        Task& operator=(Task&& other) noexcept
        {
            if (std::addressof(other) == this)
                return *this;
            if (coro_)
                coro_.destroy();

            coro_ = other.coro_;
            other.coro_ = nullptr;
            return *this;
        }

        struct promise_type
        {
            Task<> get_return_object()
            {
                return Task<>{handle_type::from_promise(*this)};
            }

            std::suspend_always initial_suspend()
            {
                return {};
            }

            void return_void()
            {
            }

            auto final_suspend() noexcept
            {
                return final_awaiter{};
            }

            void unhandled_exception()
            {
                exception_ = std::current_exception();
            }

            void result()
            {
                if (exception_ != nullptr)
                    std::rethrow_exception(exception_);
            }

            void setContinuation(std::coroutine_handle<> handle)
            {
                continuation_ = handle;
            }

            std::exception_ptr exception_;
            std::coroutine_handle<> continuation_{ std::noop_coroutine() };
        };

        auto operator co_await() const noexcept
        {
            return task_awaiter(coro_);
        }

        handle_type coro_;
    };

    /// Fires a coroutine and doesn't force waiting nor deallocates upon promise
    /// destructs
    // NOTE: AsyncTask is designed to be not awaitable. And kills the entire process
    // if exception escaped.
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

    /// Helper class that provides the infrastructure for turning callback into
    /// coroutines
    // The user is responsible to fill in `await_suspend()` and constructors.
    template <typename T = void>
    struct CallbackAwaiter
    {
        bool await_ready() noexcept
        {
            return false;
        }

        const T& await_resume() const noexcept(false)
        {
            // await_resume() should always be called after co_await
            // (await_suspend()) is called. Therefore the value should always be set
            // (or there's an exception)
            assert(result_.has_value() == true || exception_ != nullptr);

            if (exception_)
                std::rethrow_exception(exception_);
            return result_.value();
        }

    private:
        // HACK: Not all desired types are default constructable. But we need the
        // entire struct to be constructed for awaiting. std::optional takes care of
        // that.
        std::optional<T> result_;
        std::exception_ptr exception_{ nullptr };

    protected:
        void setException(const std::exception_ptr& e)
        {
            exception_ = e;
        }

        void setValue(const T& v)
        {
            result_.emplace(v);
        }

        void setValue(T&& v)
        {
            result_.emplace(std::move(v));
        }
    };

    template <>
    struct CallbackAwaiter<void>
    {
        bool await_ready() noexcept
        {
            return false;
        }

        void await_resume() noexcept(false)
        {
            if (exception_)
                std::rethrow_exception(exception_);
        }

    private:
        std::exception_ptr exception_{ nullptr };

    protected:
        void setException(const std::exception_ptr& e)
        {
            exception_ = e;
        }
    };

    template <typename Await>
    auto sync_wait(Await&& await)
    {
        static_assert(is_awaitable_v<std::decay_t<Await>>);
        using value_type = typename await_result<Await>::type;
        std::condition_variable cv;
        std::mutex mtx;
        std::atomic<bool> flag = false;
        std::exception_ptr exception_ptr;
        std::unique_lock lk(mtx);

        if constexpr (std::is_same_v<value_type, void>)
        {
            auto task = [&]() -> AsyncTask {
                try
                {
                    co_await await;
                }
                catch (...)
                {
                    exception_ptr = std::current_exception();
                }
                std::unique_lock lk(mtx);
                flag = true;
                cv.notify_all();
            };

            std::thread thr([&]() { task(); });
            cv.wait(lk, [&]() { return (bool)flag; });
            thr.join();
            if (exception_ptr)
                std::rethrow_exception(exception_ptr);
        }
        else
        {
            std::optional<value_type> value;
            auto task = [&]() -> AsyncTask {
                try
                {
                    value = co_await await;
                }
                catch (...)
                {
                    exception_ptr = std::current_exception();
                }
                std::unique_lock lk(mtx);
                flag = true;
                cv.notify_all();
            };

            std::thread thr([&]() { task(); });
            cv.wait(lk, [&]() { return (bool)flag; });
            assert(value.has_value() == true || exception_ptr);
            thr.join();

            if (exception_ptr)
                std::rethrow_exception(exception_ptr);

            return std::move(value.value());
        }
    }

    // Converts a task (or task like) promise into std::future for old-style async
    template <typename Await>
    inline auto co_future(Await&& await) noexcept
        -> std::future<await_result_t<Await>>
    {
        using Result = await_result_t<Await>;
        std::promise<Result> prom;
        auto fut = prom.get_future();
        [](std::promise<Result> prom, Await await) -> AsyncTask {
            try
            {
                if constexpr (std::is_void_v<Result>)
                {
                    co_await std::move(await);
                    prom.set_value();
                }
                else
                    prom.set_value(co_await std::move(await));
            }
            catch (...)
            {
                prom.set_exception(std::current_exception());
            }
            }(std::move(prom), std::move(await));
        return fut;
    }

    template <typename Coro>
    void async_run(Coro&& coro)
    {
        using CoroValueType = std::decay_t<Coro>;
        auto functor = [](CoroValueType coro) -> AsyncTask {
            auto frame = coro();

            using FrameType = std::decay_t<decltype(frame)>;
            static_assert(is_awaitable_v<FrameType>);

            co_await frame;
            co_return;
        };
        functor(std::forward<Coro>(coro));
    }

    template <typename Coro>
    std::function<void()> async_func(Coro&& coro)
    {
        return [coro = std::forward<Coro>(coro)]() mutable {
            async_run(std::move(coro));
        };
    }

    struct [[nodiscard]] TimerAwaiter : CallbackAwaiter<void>
    {
        TimerAwaiter(std::chrono::milliseconds delay): delay_(delay.count())
        {
        }

        TimerAwaiter(int delay) : delay_(delay)
        {
        }

        void await_suspend(std::coroutine_handle<> handle)
        {
			scheduler::execute_after(std::chrono::milliseconds(delay_), [=]() { handle.resume(); });
        }

    private:
        int delay_;
    };

    /*struct [[nodiscard]] LoopAwaiter : CallbackAwaiter<void>
    {
        LoopAwaiter(trantor::EventLoop* workLoop,
            std::function<void()>&& taskFunc,
            trantor::EventLoop* resumeLoop = nullptr)
            : workLoop_(workLoop),
            resumeLoop_(resumeLoop),
            taskFunc_(std::move(taskFunc))
        {
            assert(workLoop);
        }

        void await_suspend(std::coroutine_handle<> handle)
        {
            workLoop_->queueInLoop([handle, this]() {
                try
                {
                    taskFunc_();
                    if (resumeLoop_ && resumeLoop_ != workLoop_)
                        resumeLoop_->queueInLoop([handle]() { handle.resume(); });
                    else
                        handle.resume();
                }
                catch (...)
                {
                    setException(std::current_exception());
                    if (resumeLoop_ && resumeLoop_ != workLoop_)
                        resumeLoop_->queueInLoop([handle]() { handle.resume(); });
                    else
                        handle.resume();
                }
                });
        }

    private:
        trantor::EventLoop* workLoop_{ nullptr };
        trantor::EventLoop* resumeLoop_{ nullptr };
        std::function<void()> taskFunc_;
    };*/

    /*struct [[nodiscard]] SwitchThreadAwaiter : CallbackAwaiter<void>
    {
        explicit SwitchThreadAwaiter(trantor::EventLoop* loop) : loop_(loop)
        {
        }

        void await_suspend(std::coroutine_handle<> handle)
        {
            loop_->runInLoop([handle]() { handle.resume(); });
        }

    private:
        trantor::EventLoop* loop_;
    };*/

    inline TimerAwaiter sleepCoro(std::chrono::milliseconds delay) noexcept
    {
        assert(loop);
        return { delay };
    }

}