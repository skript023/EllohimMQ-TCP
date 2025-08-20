#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <file_manager.hpp>

#define TRACE spdlog::level::level_enum::trace
#define VERBOSE spdlog::level::level_enum::debug
#define INFO spdlog::level::level_enum::info
#define WARNING spdlog::level::level_enum::warn
#define FATAL spdlog::level::level_enum::err
#define CRITICAL spdlog::level::level_enum::critical

namespace ellohim
{
    class logger 
    {
    public:
        // --- Kelas Bersarang LogStream ---
        // Kelas ini bertanggung jawab untuk membangun pesan dan mengirimkannya ke spdlog
        class LogStream
        {
        public:
            // Constructor: Menerima shared_ptr ke spdlog::logger dan level log
            LogStream(std::shared_ptr<spdlog::logger> logger_ptr, spdlog::level::level_enum level, std::source_location location = std::source_location::current());

            // Destructor: Ini adalah titik di mana pesan log akhir dikirimkan ke spdlog
            ~LogStream() 
            {
                if (logger_ && logger_->should_log(level_)) 
                {
                    logger_->log(level_, ss_.str()); // spdlog bisa menerima std::string
                }
            }

            // Operator<< overload: Memungkinkan chaining untuk membangun pesan
            template<typename T>
            LogStream& operator<<(const T& value) 
            {
                // Hanya tambahkan ke stringstream jika logger ada dan levelnya sesuai
                if (logger_ && logger_->should_log(level_)) 
                {
                    ss_ << value;
                }
                return *this; // Kembalikan referensi ke objek itu sendiri untuk chaining
            }

        private:
            std::shared_ptr<spdlog::logger> logger_;
            spdlog::level::level_enum level_;
            std::stringstream ss_;
        };
        // --- Akhir Kelas Bersarang LogStream ---
        void create_backup();
    public:
        // Fungsi inisialisasi statis
        static void init(file name)
        {
            instance().init_impl(name);
        }

        // Aksesor untuk mendapatkan shared_ptr ke spdlog::logger
        // Dibutuhkan oleh LogStream untuk mengakses logger yang diinisialisasi
        std::shared_ptr<spdlog::logger> get_logger() const {
            return spd_logger_;
        }
        // Makro LOG: Membuat objek LogStream sementara untuk logging
        // Penting: Makro ini harus diletakkan di luar definisi kelas untuk sintaks yang benar,
        // tetapi akan memanggil LogStream yang bersarang.
        // Kita akan definisikan makro ini setelah definisi kelas logger.
        
        // Fungsi statik untuk mendapatkan instance Singleton
        static logger& instance() {
            static logger i{};
            return i;
        }
    private:
        // shared_ptr untuk spdlog::logger
        std::shared_ptr<spdlog::logger> spd_logger_;
        file m_file;

        // Implementasi inisialisasi
        void init_impl(file name);

        // Konstruktor pribadi untuk Singleton pattern
        logger() = default;

        // Hapus copy constructor dan assignment operator untuk Singleton pattern
        logger(const logger&) = delete;
        logger& operator=(const logger&) = delete;

        // Friendship untuk LogStream agar bisa mengakses get_logger()
        // Karena LogStream sekarang bersarang, ini tidak sepenuhnya diperlukan
        // tapi bisa menjaga konsistensi jika suatu saat LogStream dipisah lagi.
        // Namun, jika LogStream dibuat public, maka tidak butuh friend.
        // Jika LogStream private, maka friend class logger::LogStream diperlukan.
        // Untuk tujuan ini, kita buat LogStream public agar makro mudah diakses.
    };
}

#define LOG(level) \
    ellohim::logger::LogStream(ellohim::logger::instance().get_logger(), level)