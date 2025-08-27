#include "consumer.hpp"
#include "consumers.hpp"

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

	AsyncTask consumer::call(std::string payload)
	{
		on_call(payload).detach();

		co_return;
	}
}