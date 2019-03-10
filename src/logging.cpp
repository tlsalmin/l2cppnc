#include "logging.hpp"

using namespace Sukat;

std::unique_ptr<Logger> Logger::log_instance = nullptr;
std::once_flag Logger::onceFlag;
Logger::LogLevel Logger::global_lvl;

void __attribute__((constructor))init_stdout()
{
  Logger::initialize(Logger::LogLevel::DEBUG);
}
