#pragma once
#include "common.hpp"
#include "file_manager/file.hpp"
#include "file_manager/folder.hpp"

namespace ellohim
{
	class file_manager final
	{
		static file_manager& instance()
		{
			static file_manager i{};

			return i;
		}
	public:
		file_manager() = default;
		virtual ~file_manager() = default;
		file_manager(const file_manager&) = delete;
		file_manager(file_manager&&) noexcept = delete;
		file_manager& operator=(const file_manager&) = delete;
		file_manager& operator=(file_manager&&) noexcept = delete;

		static bool init(std::filesystem::path const& base_dir) { return instance().init_impl(base_dir); }
		static std::filesystem::path get_base_dir() { return instance().get_base_dir_impl(); }
		static file get_project_file(std::filesystem::path file_path) { return instance().get_project_file_impl(file_path); }
		static folder get_project_folder(std::filesystem::path folder_path) { return instance().get_project_folder_impl(folder_path); }
		static std::filesystem::path ensure_file_can_be_created(const std::filesystem::path file_path);
		static std::filesystem::path ensure_folder_exists(const std::filesystem::path folder_path);
	private:
		bool init_impl(const std::filesystem::path& base_dir);

		const std::filesystem::path& get_base_dir_impl();

		file get_project_file_impl(std::filesystem::path file_path);

		folder get_project_folder_impl(std::filesystem::path folder_path);
	private:
		std::filesystem::path m_base_dir;
	};
}