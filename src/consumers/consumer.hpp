#pragma once
#include "util/joaat.hpp"

namespace ellohim
{
	class consumer
	{
	private:
		std::string m_name;
		std::string m_label;
		std::string m_description;
		joaat_t m_hash;

		int m_num_args = 0; // TODO: currently unused

	protected:
		virtual async<> on_call() = 0;

	public:
		consumer(std::string name, std::string label, std::string description, int num_args = 0);
		async<> call(std::string param);

		const std::string& get_name()
		{
			return m_name;
		}

		const std::string& get_label()
		{
			return m_label;
		}

		const std::string& get_description()
		{
			return m_description;
		}

		joaat_t get_hash()
		{
			return m_hash;
		}
	};
}