#pragma once
#include "thread_pool.hpp"

namespace ellohim
{
    struct scheduled_job
    {
        std::chrono::steady_clock::time_point execute_time;
        std::function<void()> func;

        bool operator>(const scheduled_job& other) const
        {
            return execute_time > other.execute_time;
        }
    };

    class scheduler
    {
        static scheduler& instance()
        {
            static scheduler instance{};
			return instance;
        }

        void init_impl()
        {
            m_running = true;
			m_thread = std::thread(&scheduler::run, this);
        }

        void destroy_impl()
        {
            {
                std::unique_lock lock(m_lock);
                m_running = false;
            }
            m_cv.notify_all();
            if (m_thread.joinable())
                m_thread.join();
        }


        // eksekusi setelah durasi tertentu
        void execute_after_impl(std::chrono::milliseconds duration, std::function<void()> func)
        {
            execute_at_impl(std::chrono::steady_clock::now() + duration, std::move(func));
        }

        // eksekusi pada waktu tertentu
        void execute_at_impl(std::chrono::steady_clock::time_point time, std::function<void()> func)
        {
            {
                std::unique_lock lock(m_lock);
                m_jobs.push(scheduled_job{ time, std::move(func) });
            }
            m_cv.notify_all();
        }
    public:
        static void init()
        {
            instance().init_impl();
        }

        static void destory()
        {
            instance().destroy_impl();
        }

        static void execute_after(std::chrono::milliseconds duration, std::function<void()> func)
		{
			instance().execute_after_impl(duration, std::move(func));
		}

        static void execute_at(std::chrono::steady_clock::time_point time, std::function<void()> func)
        {
			instance().execute_at_impl(time, std::move(func));
        }
    private:
        std::priority_queue<scheduled_job, std::vector<scheduled_job>, std::greater<>> m_jobs;

        std::mutex m_lock;
        std::condition_variable m_cv;
        std::atomic<bool> m_running;
        std::thread m_thread;

        void run()
        {
            while (m_running)
            {
                std::unique_lock lock(m_lock);

                if (m_jobs.empty())
                {
                    m_cv.wait(lock, [this]() { return !m_jobs.empty() || !m_running; });
                }
                else
                {
                    auto now = std::chrono::steady_clock::now();
                    auto next_time = m_jobs.top().execute_time;

                    if (now >= next_time)
                    {
                        auto job = std::move(m_jobs.top());
                        m_jobs.pop();
                        lock.unlock();

                        g_thread_pool->queue_job(job.func); // masukkan ke thread pool
                    }
                    else
                    {
                        m_cv.wait_until(lock, next_time);
                    }
                }
            }
        }
    };
}
