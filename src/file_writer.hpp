#pragma once

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace wld_live_lab {

class FileWriter {
public:
    static constexpr size_t WRITE_BUFFER_SIZE = 1024 * 1024;

private:
    std::ofstream                       file_;
    std::vector<char>                   buffer_;
    size_t                              buf_pos_{0};
    std::string                         output_dir_;
    std::string                         file_prefix_;
    std::chrono::system_clock::time_point last_rotation_{};

public:
    explicit FileWriter(
        std::string output_dir = "data/history",
        std::string file_prefix = "wld_live_lab")
        : buffer_(WRITE_BUFFER_SIZE),
          output_dir_(std::move(output_dir)),
          file_prefix_(std::move(file_prefix)) {}

    ~FileWriter() {
        close();
    }

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;

    void check_rotation() {
        const auto now = std::chrono::system_clock::now();
        const auto elapsed_min =
            std::chrono::duration_cast<std::chrono::minutes>(now - last_rotation_).count();

        if (!file_.is_open() || elapsed_min >= 60) {
            rotate();
        }
    }

    void rotate() {
        if (file_.is_open()) {
            flush(true);
            file_.close();
        }

        std::error_code ec;
        std::filesystem::create_directories(output_dir_, ec);

        const auto now = std::chrono::system_clock::now();
        last_rotation_ = now;
        const std::time_t t_c = std::chrono::system_clock::to_time_t(now);

        std::tm tm_buf{};
#if defined(_POSIX_C_SOURCE) || defined(__unix__) || defined(__APPLE__)
        localtime_r(&t_c, &tm_buf);
#else
        tm_buf = *std::localtime(&t_c);
#endif

        std::ostringstream oss;
        oss << output_dir_ << "/" << file_prefix_ << "_"
            << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << ".bin";

        const std::string path = oss.str();
        file_.open(path, std::ios::binary | std::ios::app);
    }

    void write(const char* data, size_t len) {
        if (!file_.is_open() || len == 0) {
            return;
        }

        if (len > WRITE_BUFFER_SIZE) {
            flush(false);
            file_.write(data, static_cast<std::streamsize>(len));
            return;
        }

        if (buf_pos_ + len > WRITE_BUFFER_SIZE) {
            flush(false);
        }

        std::memcpy(buffer_.data() + buf_pos_, data, len);
        buf_pos_ += len;
    }

    void flush(bool sync_disk = false) {
        if (buf_pos_ > 0 && file_.is_open()) {
            file_.write(buffer_.data(), static_cast<std::streamsize>(buf_pos_));
            buf_pos_ = 0;
            if (sync_disk) {
                file_.flush();
            }
        }
    }

    void close() {
        if (file_.is_open()) {
            flush(true);
            file_.close();
        }
    }
};

}
