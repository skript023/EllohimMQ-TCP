#include "logger.hpp"

namespace ellohim
{
    void logger::init_impl(std::string_view name)
    {
        // Cek apakah logger sudah diinisialisasi
        if (spd_logger_) {
            spdlog::warn("ellohim::logger already initialized!");
            return;
        }

        auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(std::format("{}.txt", name), true);

        console->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%l] %v%$");
        file->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%l] %v%$"); // Terapkan juga untuk file

        // Setup sinks: console dan file
        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(console);
        sinks.push_back(file); // Log ke file, overwrite setiap run

        // Buat multi-sink logger
        spd_logger_ = std::make_shared<spdlog::logger>(std::string(name), begin(sinks), end(sinks));

        // Daftarkan logger ini agar bisa diakses dengan spdlog::get()
        spdlog::set_default_logger(spd_logger_);
        //spdlog::register_logger(spd_logger_);

        // Set level default (misal: trace)
        spd_logger_->set_level(spdlog::level::trace);
        spd_logger_->flush_on(spdlog::level::trace);
    }
    logger::LogStream::LogStream(std::shared_ptr<spdlog::logger> logger_ptr, spdlog::level::level_enum level, std::source_location location) : logger_(logger_ptr), level_(level)
    {
        // Hanya bangun string jika logger ada dan levelnya sesuai
        if (logger_ && logger_->should_log(level_)) {
            ss_ << "[" << std::filesystem::path(location.file_name()).filename().string() << ":" << location.line() << "] ";
        }
    }
}