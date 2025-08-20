#include "logger.hpp"

namespace ellohim
{
    template<typename TP>
    std::time_t to_time_t(TP tp)
    {
        using namespace std::chrono;
        auto sctp = time_point_cast<system_clock::duration>(tp - TP::clock::now() + system_clock::now());
        return system_clock::to_time_t(sctp);
    }

    void logger::create_backup()
    {
        if (m_file.exists())
        {
            auto file_time = std::filesystem::last_write_time(m_file.get_path());
            auto time_t = to_time_t(file_time);
            auto local_time = std::localtime(&time_t);

            // Pindahkan file lama ke folder backup dengan format timestamp
            m_file.move(std::format("./backup/{:0>2}-{:0>2}-{}-{:0>2}-{:0>2}-{:0>2}_{}",
                local_time->tm_mon + 1,
                local_time->tm_mday,
                local_time->tm_year + 1900,
                local_time->tm_hour,
                local_time->tm_min,
                local_time->tm_sec,
                m_file.get_path().filename().string().c_str()));
        }
    }

    void logger::init_impl(file name)
    {
        m_file = name;
        // Cek apakah logger sudah diinisialisasi
        if (spd_logger_) 
        {
            spdlog::warn("ellohim::logger already initialized!");
            return;
        }

        create_backup();

        auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(name.get_path().string(), true);

        console->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%l] %v%$");
        file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v"); // Terapkan juga untuk file
        
        // Setup sinks: console dan file
        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(console);
        sinks.push_back(file); // Log ke file, overwrite setiap run

        // Buat multi-sink logger
        spd_logger_ = std::make_shared<spdlog::logger>("console", begin(sinks), end(sinks));

        // Daftarkan logger ini agar bisa diakses dengan spdlog::get()
        spdlog::set_default_logger(spd_logger_);
        //spdlog::register_logger(spd_logger_);

        // Set level default (misal: trace)
        spd_logger_->set_level(spdlog::level::info);
        spd_logger_->flush_on(spdlog::level::info);
    }
    logger::LogStream::LogStream(std::shared_ptr<spdlog::logger> logger_ptr, spdlog::level::level_enum level, std::source_location location) : logger_(logger_ptr), level_(level)
    {
        // Hanya bangun string jika logger ada dan levelnya sesuai
        if (logger_ && logger_->should_log(level_)) {
            ss_ << "[" << std::filesystem::path(location.file_name()).filename().string() << ":" << location.line() << "] ";
        }
    }
}