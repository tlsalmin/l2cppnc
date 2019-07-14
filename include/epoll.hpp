#pragma once

#include <functional>
#include <map>
#include <sstream>
#include <optional>

extern "C"
{
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
}

#include "fd.hpp"
#include "logging.hpp"

namespace Sukat
{
class Epoll
{
 public:
  Epoll() : mEfd(epoll_create1(EPOLL_CLOEXEC))
  {
    LOG_DBG("Created epoll fd ", mEfd.fd());
  };
  ~Epoll() = default;

  [[nodiscard]] bool ctl(const int fd, int op = EPOLL_CTL_ADD,
                         uint32_t events = EPOLLIN,
                         std::optional<epoll_data_t> data = {}) const
  {
    struct epoll_event ev = {};

    ev.events = events;
    if (data)
      {
        ev.data = data.value();
      }
    else
      {
        ev.data.fd = fd;
      }
    if (!::epoll_ctl(mEfd.fd(), op, fd, &ev))
      {
        LOG_DBG(op, " fd ", fd, " to ", mEfd.fd());
        return true;
      }
    else
      {
        LOG_ERR("Failed to ", op, " fd ", fd, " to ", mEfd.fd(), ": ",
                strerror(errno));
      }
    return false;
  };

  // TODO Do this with a span when it's ready.
  std::optional<int> wait(
    std::function<std::optional<int>(const struct epoll_event &)> func,
    int timeout = 0)
  {
    const unsigned int max_events = 128;
    struct epoll_event ev[max_events];

    if (int ret = epoll_wait(mEfd.fd(), ev, max_events, timeout) >= 0)
      {
        const unsigned int n_events = ret;
        unsigned int i;

        for (i = 0; i < n_events; i++)
          {
            LOG_DBG("Events: ", ev[i].events, ", data: ptr:", ev[i].data.ptr,
                    ", fd: ", ev[i].data.fd);
            auto func_ret = func(ev[i]);

            if (func_ret)
              {
                return func_ret;
              }
          }
      }
    else
      {
        throw std::system_error(errno, std::system_category(),
                                "failed to wait for events");
      }
    return {};
  };

  int fd()
  {
    return mEfd.fd();
  };

  static std::string event_to_string(const struct epoll_event &ev)
    {
    static const std::map<int, const std::string> ev_to_mapping = {
      {EPOLLIN, "EPOLLIN"},       {EPOLLOUT, "EPOLLOUT"},
      {EPOLLRDHUP, "EPOLLRDHUP"}, {EPOLLPRI, "EPOLLPRI"},
      {EPOLLERR, "EPOLLERR"},     {EPOLLHUP, "EPOLLHUP"},
      {EPOLLET, "EPOLLET"},
    };
    std::stringstream os;

    os << "event: fd: " << ev.data.fd << " events: ";

    for (const auto &[key, value] : ev_to_mapping)
      {
        if (key & ev.events)
          {
            os << value << "|";
          }
      };
    return os.str();
    }

  friend std::ostream &operator<<(std::ostream &os,
                                  const struct epoll_event &ev)
  {
    os << event_to_string(ev);
    return os;
  };

 private:
  const Fd mEfd{-1};
};
}; // namespace Sukat
