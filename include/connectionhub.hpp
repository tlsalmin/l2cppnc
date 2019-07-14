#pragma once

#include "socket.hpp"

namespace Sukat
{
class ConnectionHub
{
public:
  int connect(int type, const std::string &dst, const std::string &port);
 private:
  int efd;
  std::map<int, std::unique_ptr<Sukat::Socket>> conns;
};
} // namespace Sukat

