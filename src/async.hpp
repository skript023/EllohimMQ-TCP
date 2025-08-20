#pragma once
#include <common.hpp>

namespace ellohim
{
    // Deklarasi maju untuk struct async
    template <typename T>
    struct async;

    /**
     * @class scheduler
     * @brief Implementasi scheduler untuk coroutine C++20.
     * * Scheduler ini mengelola tiga jenis antrean tugas:
     * - Antrean tugas segera (tasks): Untuk tugas yang siap dijalankan.
     * - Antrean tugas tertunda (delayed_tasks): Untuk tugas yang dijadwalkan untuk dijalankan setelah jeda waktu tertentu.
     * - Antrean pembersihan (cleanup_tasks): Untuk menghancurkan handle coroutine yang telah selesai.
     * * Scheduler ini dapat berjalan dalam mode sinkron (single-threaded) atau multi-threaded.
     */
    class scheduler
    {
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

    public:
        // --- Metode Publik (Antarmuka Pengguna) ---

        /**
         * @brief Menjadwalkan coroutine untuk segera dijalankan.
         * @param h Handle coroutine yang akan dijadwalkan.
         */
        static void schedule(std::coroutine_handle<> h)
        {
            if (h && !h.done())
            {
                instance().schedule_impl(h);
            }
        }

        /**
         * @brief Menjadwalkan coroutine untuk dijalankan setelah jeda waktu tertentu.
         * @param delay Jeda waktu dalam milidetik.
         * @param h Handle coroutine yang akan dijadwalkan.
         */
        static void schedule_after(std::chrono::milliseconds delay, std::coroutine_handle<> h)
        {
            if (h && !h.done())
            {
                instance().schedule_after_impl(delay, h);
            }
        }

        /**
         * @brief Memulai scheduler dengan jumlah thread worker tertentu.
         * @param max_thread Jumlah thread yang akan digunakan. Default-nya 1.
         */
        static void start(int max_thread = 1)
        {
            instance().start_impl(max_thread);
        }

        /**
         * @brief Menghentikan semua thread scheduler.
         */
        static void stop()
        {
            instance().stop_impl();
        }

        /**
         * @brief Menjalankan scheduler dalam mode sinkron hingga semua tugas selesai.
         * @note Metode ini memblokir thread pemanggil.
         */
        static void run()
        {
            instance().run_impl();
        }

        /**
         * @brief Menandai handle coroutine untuk pembersihan (penghancuran).
         * @param h Handle coroutine yang akan dibersihkan.
         */
        static void cleanup_handle(std::coroutine_handle<> h)
        {
            if (h && h.address()) {
                instance().cleanup_handle_impl(h);
            }
        }

    private:
        // --- Metode Internal Scheduler ---

        /**
         * @brief Mengambil instance singleton dari scheduler.
         */
        static scheduler& instance()
        {
            static scheduler s;
            return s;
        }

        /**
         * @brief Memproses satu siklus scheduler (tick).
         * * Metode ini adalah inti dari loop scheduler. Ini memproses tugas segera,
         * tugas yang tertunda, dan tugas pembersihan. Metode ini dipanggil
         * oleh thread worker dan `run_impl()`.
         */
        void tick_impl()
        {
            // Menunggu tugas baru atau sinyal penghentian
            std::coroutine_handle<> h;
            {
                std::unique_lock<std::mutex> lock(tasks_mutex);
                data_condition.wait(lock, [this] {
                    return !running.load() || !tasks.empty() || !delayed_tasks.empty() || !cleanup_tasks.empty();
                });

                if (!running.load())
                {
                    return;
                }

                if (!tasks.empty())
                {
                    h = tasks.front();
                    tasks.pop();
                }
            } // Melepas kunci

            // Memproses tugas yang diambil
            if (h && h.address())
            {
                try
                {
                    if (h.done())
                    {
                        LOG(VERBOSE) << "[Scheduler] Tugas selesai sebelum resume " << h.address();
                    }
                    else {
                        LOG(VERBOSE) << "[Scheduler] -> Melanjutkan tugas " << h.address() << "...";
                        h.resume();
                        LOG(VERBOSE) << "[Scheduler] <- Tugas " << h.address() << " berhasil dilanjutkan.";
                    }
                }
                catch (const std::exception& e)
                {
                    LOG(FATAL) << "[Scheduler] Pengecualian saat melanjutkan " << h.address() << ": " << e.what();
                    cleanup_handle_impl(h);
                }
                catch (...)
                {
                    LOG(FATAL) << "[Scheduler] Pengecualian tidak dikenal saat melanjutkan " << h.address();
                    cleanup_handle_impl(h);
                }
            }

            // Memproses antrean lainnya
            process_cleanup_tasks();
            process_delayed_tasks();
        }

        /**
         * @brief Implementasi mode sinkron.
         */
        void run_impl()
        {
            LOG(VERBOSE) << "[Scheduler] run() dipanggil. Memproses tugas hingga semua antrean kosong.";
            while (true)
            {
                bool tasks_empty = tasks.empty();
                bool delayed_empty = delayed_tasks.empty();
                bool cleanup_empty = cleanup_tasks.empty();
                if (tasks_empty && delayed_empty && cleanup_empty)
                {
                    break;
                }

                tick_impl();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        /**
         * @brief Memulai thread worker scheduler.
         */
        void start_impl(int max_thread)
        {
            bool expected = false;
            if (!running.compare_exchange_strong(expected, true))
            {
                LOG(WARNING) << "[Scheduler] Sudah berjalan.";
                return;
            }

            LOG(VERBOSE) << "[Scheduler] Memulai scheduler.";

            for (std::size_t i = 0; i < max_thread; ++i) {
                threads.emplace_back(std::thread(&scheduler::start_pool_impl, this));
            }

            LOG(INFO) << "[Scheduler] Berhasil memulai " << max_thread << " thread scheduler.";
        }

        /**
         * @brief Fungsi utama untuk setiap thread worker.
         */
        void start_pool_impl()
        {
            while (running.load())
            {
                tick_impl();
            }
            LOG(VERBOSE) << "[Scheduler] Thread scheduler dihentikan.";
        }

        /**
         * @brief Menghentikan semua thread worker.
         */
        void stop_impl()
        {
            running.store(false);
            data_condition.notify_all();
            for (auto& t : threads) {
                if (t.joinable()) {
                    t.join();
                }
            }
            LOG(INFO) << "[Scheduler] Scheduler dihentikan.";
        }

        /**
         * @brief Implementasi internal untuk menjadwalkan tugas segera.
         */
        void schedule_impl(std::coroutine_handle<> h)
        {
            if (!h || h.address() == nullptr)
            {
                LOG(WARNING) << "[Scheduler] Mencoba menjadwalkan handle yang tidak valid.";
                return;
            }

            if (h.done())
            {
                LOG(VERBOSE) << "[Scheduler] Handle sudah selesai, tidak menjadwalkan " << h.address();
                return;
            }

            std::lock_guard<std::mutex> lock(tasks_mutex);
            auto addr = h.address();
            if (handle_refs.find(addr) == handle_refs.end())
            {
                handle_refs[addr] = 1;
                tasks.push(h);
                data_condition.notify_one();
                LOG(VERBOSE) << "[Scheduler] Menjadwalkan tugas baru " << addr << " (ukuran antrean: " << tasks.size() << ")";
            }
            else
            {
                LOG(WARNING) << "[Scheduler] Handle sudah ada di antrean " << addr;
            }
        }

        /**
         * @brief Implementasi internal untuk menjadwalkan tugas tertunda.
         */
        void schedule_after_impl(std::chrono::milliseconds delay, std::coroutine_handle<> h)
        {
            if (!h || h.address() == nullptr)
            {
                LOG(WARNING) << "[Scheduler] Mencoba menjadwalkan_setelah handle yang tidak valid.";
                return;
            }

            if (h.done())
            {
                LOG(VERBOSE) << "[Scheduler] Handle sudah selesai, tidak menjadwalkan yang tertunda " << h.address();
                return;
            }

            std::lock_guard<std::mutex> lock(delayed_mutex);
            auto time = Clock::now() + delay;
            delayed_tasks.emplace(time, h);
            data_condition.notify_one();
            LOG(VERBOSE) << "[Scheduler] Menjadwalkan tugas tertunda " << h.address() << " untuk " << delay.count() << "ms";
        }

        /**
         * @brief Implementasi internal untuk menandai handle agar dibersihkan.
         */
        void cleanup_handle_impl(std::coroutine_handle<> h)
        {
            std::lock_guard<std::mutex> lock(cleanup_mutex);
            cleanup_tasks.push(h);
            data_condition.notify_one();
            LOG(VERBOSE) << "[Scheduler] Tugas ditandai untuk pembersihan " << h.address();
        }

        /**
         * @brief Memproses antrean pembersihan handle.
         */
        void process_cleanup_tasks() {
            std::queue<std::coroutine_handle<>> local_cleanup_tasks;
            {
                std::lock_guard<std::mutex> lock(cleanup_mutex);
                local_cleanup_tasks.swap(cleanup_tasks);
            }

            if (local_cleanup_tasks.empty()) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(tasks_mutex);
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
                                LOG(VERBOSE) << "[Scheduler] Menghancurkan handle coroutine " << addr;
                                h.destroy();
                            }
                        }
                    }
                }
            }
        }

        /**
         * @brief Memproses antrean tugas segera.
         * @deprecated Sekarang digantikan oleh tick_impl().
         */
        void process_immediate_tasks()
        {
            // Logika ini sekarang ada di tick_impl()
        }

        /**
         * @brief Memproses antrean tugas yang tertunda.
         */
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
                    ready_tasks.push_back(h);
                }
            } // Melepas kunci

            if (!ready_tasks.empty()) {
                std::lock_guard<std::mutex> tasks_lock(tasks_mutex);
                for (auto& h : ready_tasks)
                {
                    if (!h.done()) 
                    {
                        tasks.push(h);
                        LOG(VERBOSE) << "[Scheduler] Tugas tertunda " << h.address() << " dipindahkan ke antrean segera.";
                    }
                    else 
                    {
                        LOG(VERBOSE) << "[Scheduler] Tugas tertunda sudah selesai, melewati " << h.address();
                    }
                }

                data_condition.notify_all();
            }
        }

    private:
        // --- Anggota Data ---
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
        std::condition_variable data_condition;

        std::vector<std::thread> threads;
    };

    // --- Struktur dan Kelas Coroutine ---

    /**
     * @struct CoroutineState
     * @brief Menyimpan status coroutine dan hasil pengembalian.
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
     * @brief Promise type untuk `async`.
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
     * @brief Tipe pengembalian coroutine.
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
                LOG(VERBOSE) << "[Async] Tugas async dibuat " << coro.address();
                scheduler::schedule(coro);
                coro = nullptr; // Mencegah penghancuran ganda
            }
            else
            {
                LOG(FATAL) << "[Async] Gagal membuat tugas async - handle atau state tidak valid";
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
                LOG(WARNING) << "[Await] await_suspend dengan handle atau state tidak valid! Induk: " << h.address();
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
                throw std::runtime_error("State async tidak valid di await_resume");
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
                LOG(VERBOSE) << "[Async] Membersihkan tugas " << coro.address();
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
                LOG(VERBOSE) << "[Async] Tugas async dibuat " << coro.address();
                scheduler::schedule(coro);
                coro = nullptr;
            }
            else
            {
                LOG(FATAL) << "[Async] Gagal membuat tugas async - handle atau state tidak valid";
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
                LOG(WARNING) << "[Await] await_suspend dengan handle atau state tidak valid! Induk: " << h.address();
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
                throw std::runtime_error("State async tidak valid di await_resume");
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
                LOG(VERBOSE) << "[Async] Membersihkan tugas " << coro.address();
                scheduler::cleanup_handle(coro);
                coro = nullptr;
            }
            state = nullptr;
        }

    public:
        handle_type coro = nullptr;
        std::shared_ptr<CoroutineState<void>> state = nullptr;
    };

    // --- Awaiter untuk Kontrol Aliran ---

    /**
     * @struct SleepAwaiter
     * @brief Awaiter untuk menjeda eksekusi coroutine.
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
     * @brief Awaiter untuk mengembalikan kontrol ke scheduler.
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
     * @brief Fungsi pembantu untuk yield.
     */
    inline YieldAwaiter yield()
    {
        return YieldAwaiter{};
    }

    /**
     * @brief Fungsi pembantu untuk menunda eksekusi.
     */
    inline SleepAwaiter sleep_for(std::chrono::milliseconds delay)
    {
        return SleepAwaiter{ delay };
    }

    // --- Fungsi Pembantu untuk Mengelola Coroutine ---

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
     * @brief Menjalankan tugas tanpa menunggu hasilnya.
     */
    template <typename T>
    void fire_and_forget(async<T>&& task)
    {
        if (!task.coro || !task.state) {
            LOG(FATAL) << "[fire_and_forget] Tugas tidak valid.";
            return;
        }

        LOG(VERBOSE) << "[fire_and_forget] Memulai tugas latar belakang " << task.coro.address();

        try
        {
            scheduler::schedule(task.coro);
            LOG(VERBOSE) << "[fire_and_forget] Tugas berhasil dijadwalkan " << task.coro.address();
        }
        catch (const std::exception& e)
        {
            LOG(FATAL) << "[fire_and_forget] Gagal menjadwalkan tugas: " << e.what();
            return;
        }

        // Mencegah penghancuran ganda
        task.coro = nullptr;
        task.state = nullptr;
    }

    /**
     * @brief Menjalankan tugas secara sinkron dan memblokir hingga selesai.
     */
    template <typename T>
    T run_sync(async<T> task)
    {
        if (!task.coro || !task.state) {
            throw std::runtime_error("Handle coroutine tidak valid di run_sync");
        }

        LOG(VERBOSE) << "[run_sync] Memulai tugas " << task.coro.address();

        try {
            scheduler::schedule(task.coro);
        }
        catch (const std::exception& e) {
            LOG(FATAL) << "[run_sync] Gagal menjadwalkan tugas: " << e.what();
            throw;
        }

        scheduler::run();

        LOG(VERBOSE) << "[run_sync] Tugas selesai, mendapatkan hasil";
        return task.await_resume();
    }

    inline void run_sync(async<void> task)
    {
        if (!task.coro || !task.state) {
            throw std::runtime_error("Handle coroutine tidak valid di run_sync");
        }

        LOG(VERBOSE) << "[run_sync] Memulai tugas void " << task.coro.address();

        try {
            scheduler::schedule(task.coro);
        }
        catch (const std::exception& e) {
            LOG(FATAL) << "[run_sync] Gagal menjadwalkan tugas: " << e.what();
            throw;
        }

        scheduler::run();

        LOG(VERBOSE) << "[run_sync] Tugas void selesai";
        task.await_resume();
    }

    /**
     * @brief Membuat dan menjalankan tugas sebagai proses latar belakang.
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
                    LOG(FATAL) << "[spawn_task] Pengecualian: " << e.what();
                    throw;
                }
                catch (...)
                {
                    LOG(FATAL) << "[spawn_task] Pengecualian tidak dikenal";
                    throw;
                }
                }();

            fire_and_forget(std::move(task));
        }
        catch (const std::exception& e) {
            LOG(FATAL) << "[spawn_task] Gagal membuat tugas: " << e.what();
        }
        catch (...) {
            LOG(FATAL) << "[spawn_task] Pengecualian tidak dikenal saat membuat tugas";
        }
    }
}
