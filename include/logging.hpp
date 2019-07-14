#pragma once

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <mutex>

#define LOG_ANY(_lvl, ...) Logger::Log(_lvl, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DBG(...) LOG_ANY(Logger::LogLevel::DEBUG, __VA_ARGS__)
#define LOG_INF(...) LOG_ANY(Logger::LogLevel::INFORMATIONAL, __VA_ARGS__)
#define LOG_ERR(...) LOG_ANY(Logger::LogLevel::ERROR, __VA_ARGS__)

namespace Sukat
{
class Logger
{
 public:
  enum class LogLevel
  {
    ERROR,
    INFORMATIONAL,
    DEBUG
  };

  Logger(const Logger&) = delete;

  static void initialize(LogLevel lvl, std::string logfile = "")
  {
    std::call_once(onceFlag,
                   [&]() { log_instance.reset(new Logger(lvl, logfile)); });
  }

  static void initialize(int lvl, std::string logfile = "")
  {
    if (lvl > static_cast<int>(LogLevel::DEBUG))
      {
        lvl = static_cast<int>(LogLevel::DEBUG);
      }
    initialize(static_cast<LogLevel>(lvl), logfile);
  }

  template<typename... Args>
    static void Log(LogLevel lvl, std::string_view file, int line, Args... args)
      {
        if (log_instance.get())
          {
            if (lvl <= log_instance->log_lvl)
              {
                log_instance->print_args(lvl, filePlain(file), "(", line,
                                         "): ", args...);
              }
          }
      }

 private:
  Logger(Logger&&) = default;
  Logger(){};
  Logger(LogLevel lvl, std::string logfile) : log_lvl(lvl)
  {
    if (!logfile.empty())
      {
        output_file = std::ofstream(logfile);
      }
  };

  /** @brief Converts a full file path to only the filename without .<ext> */
  static constexpr const std::string_view filePlain(std::string_view full_path)
    {
      auto last_slash_find = full_path.find_last_of('/');
      auto last_slash = (last_slash_find == std::string_view::npos) ? 0 :
        last_slash_find + 1; // Jump over last /
      auto first_dot = full_path.find_first_of('.', last_slash);

      return full_path.substr(last_slash, first_dot - last_slash);
    }

  template <typename... Args>
  void print_args(LogLevel lvl, Args &&... args)
  {
    auto &out = (output_file.is_open()
                   ? output_file
                   : ((lvl == LogLevel::ERROR) ? std::cerr : std::cout));
    (out << ... << args) << std::endl;
  }

  std::ofstream output_file;

  static std::unique_ptr<Logger> log_instance;
  static std::once_flag onceFlag;
  LogLevel log_lvl{LogLevel::INFORMATIONAL};
};
} // namespace Sukat
