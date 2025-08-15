#include "consumers.hpp"
#include "consumer.hpp"

namespace ellohim
{
	consumers::consumers()
	{
	}

	/*std::unique_ptr<consumer> consumers::create_consumer_impl(joaat_t hash)
	{
		auto it = m_factories.find(hash);
		if (it != m_factories.end())
			return it->second();
		return nullptr;
	}*/

	void consumers::add_consumer_impl(consumer* consumer)
	{
		m_consumers.insert({consumer->get_hash(), consumer});
	}

	consumer* consumers::get_consumer_impl(joaat_t hash)
	{
		if (auto it = m_consumers.find(hash); it != m_consumers.end())
			return it->second;
		return nullptr;
	}
}