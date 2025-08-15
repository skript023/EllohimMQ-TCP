#include "consumer.hpp"
#include "consumers.hpp"

#include "util/joaat.hpp"

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

	async<> consumer::call(std::string uuid)
	{
		on_call().then([=] {

		});

		co_return;
	}
}