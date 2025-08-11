#pragma once

namespace ellohim
{
	constexpr auto MAX_POOL_SIZE = 32u;

	struct thread_pool_job
	{
		std::function<void()> m_func;
		std::source_location m_source_location;
	};

	class thread_pool
	{
		std::atomic<bool> m_accept_jobs;
		std::condition_variable m_data_condition;

		std::vector<std::deque<thread_pool_job>> m_job_stack;
		std::mutex m_lock;
		std::vector<std::thread> m_thread_pool;

		std::atomic<size_t> m_allocated_thread_count;
		std::atomic<size_t> m_busy_threads;

	public:
		thread_pool(const std::size_t preallocated_thread_count = 2);
		~thread_pool();

		void destroy();
		void queue_job(std::function<void()> func, std::source_location location = std::source_location::current());

		std::pair<size_t, size_t> usage() const
		{
			return { m_busy_threads, m_allocated_thread_count };
		}

	private:
		void run(size_t index);
		void rescale_thread_pool();
		void task_check();
	};

	inline thread_pool* g_thread_pool{};
}
