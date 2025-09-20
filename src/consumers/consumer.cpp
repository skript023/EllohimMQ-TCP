#include "consumer.hpp"
#include "consumers.hpp"
#include "thread_pool.hpp"

namespace ellohim
{
	consumer::consumer(std::string name, std::string label, std::string description, int num_args) :
	    m_name(name),
	    m_label(label),
	    m_description(description),
	    m_num_args(num_args),
	    m_hash(joaat(name))
	{
		consumers::add_consumer(this);
	}

	void consumer::call(std::string payload)
	{
		auto func = async_func([=]() -> Task<> {
			return on_call(payload);
		});
		g_thread_pool->queue_job(func);
	}
}