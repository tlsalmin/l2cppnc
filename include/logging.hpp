#pragma once

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <mutex>

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
      if (!log_instance.get())
        {
          std::call_once(onceFlag, []() { log_instance.reset(new Logger);});
        }
      if (log_instance.get())
        {
          log_instance->output_file = std::ofstream(logfile);
        }
      global_lvl = lvl;
    }

  template<typename... Args>
    static void Inf(Args... args)
      {
        log_instance->print_args(LogLevel::INFORMATIONAL, args...);
      }

  template<typename... Args>
    static void Dbg(Args... args)
      {
        log_instance->print_args(LogLevel::DEBUG, args...);
      }

  template<typename... Args>
    static void Err(Args... args)
      {
        log_instance->print_args(LogLevel::ERROR, args...);
      }

 private:
  Logger(Logger&&) = default;
  Logger(){};

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
  static LogLevel global_lvl;
};
} // namespace Sukat
