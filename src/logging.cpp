#include "logging.hpp"

using namespace Sukat;

std::unique_ptr<Logger> Logger::log_instance = nullptr;
std::once_flag Logger::onceFlag;
