#pragma once
#include "common.hpp"
#include "util/joaat.hpp"

namespace ellohim
{
	class consumer;

	class consumers
	{
	private:
		std::unordered_map<joaat_t, consumer*> m_consumers;
		//std::unordered_map<joaat_t, std::function<std::unique_ptr<consumer>()>> m_factories;

		consumers();

	public:
		static void add_consumer(consumer* consumer)
		{
			get_instance().add_consumer_impl(consumer);
		}
		
		template<typename T = consumer>
		static T* get_consumer(joaat_t hash)
		{
			return reinterpret_cast<T*>(get_instance().get_consumer_impl(hash));
		}

		/*template<typename T>
		static void register_consumer(const std::string& name, const std::string& label, const std::string& desc, int num_args = 0)
		{
			get_instance().register_consumer_impl<T>(name, label, desc, num_args);
		}

		static std::unique_ptr<consumer> create_consumer(joaat_t hash)
		{
			return get_instance().create_consumer_impl(hash);
		}*/
	private:
		/*template<typename T>
		void register_consumer_impl(const std::string& name, const std::string& label, const std::string& desc, int num_args = 0)
		{
			joaat_t hash = joaat(name);
			m_factories[hash] = [=]() {
				return std::make_unique<T>(name, label, desc, num_args);
			};
		}

		std::unique_ptr<consumer> create_consumer_impl(joaat_t hash);*/
		void add_consumer_impl(consumer* consumer);
		consumer* get_consumer_impl(joaat_t hash);

		static consumers& get_instance()
		{
			static consumers instance{};
			return instance;
		}
	};
}