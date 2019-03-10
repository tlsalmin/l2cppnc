#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>

extern "C"
{
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>
}

#include "socket.hpp"

/*
template <typename T>
Connections<T>::SocketConnection::SocketConnection(SocketConnection &&other)
  : fd(other.fd),
    source_len(other.source_len),
    destination_len(other.destination_len)
{
  memcpy(&source, &other.source, sizeof(source));
  memcpy(&destination, &other.destination, sizeof(destination));
  other.fd = -1;
}
*/

#if 0
template <typename T>
int Connections<T>::connect_to_endpoint(struct addrinfo *dst,
                                        struct addrinfo *src)
{
  int ret = -1;

  try
    {
      auto new_con = SocketConnection(dst, src);
      int fd = new_con.fd;
      auto inserted = connections.emplace(fd, std::move(new_con));

      if (inserted.second)
        {
          struct epoll_event ev = {};

          ev.data.fd = fd;
          ev.events = EPOLLIN;

          if (!inserted.first->second.finished)
            {
              ev.events |= EPOLLOUT;
            }

          if (!epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev))
            {
              ret = fd;
              if (inserted.first->second.finished)
                {
                  ccb(ctx, fd);
                }
            }
          else
            {
              throw std::system_error(
                errno, std::system_category(),
                std::string("Couldn't add a new connection to epoll: ") +
                  std::to_string(fd));
            }
        }
      else
        {
          // TODO proper exception.
          throw std::invalid_argument("Couldn't insert a new connection");
        }
    }
  catch (std::system_error &e)
    {
      std::cerr << "Failed to create connection: ";
      std::cerr << e.what() << ": ";
      std::cerr << std::strerror(e.code().value());
      std::cerr << std::endl;
    }
  return ret;
}

template <typename T>
void Connections<T>::send_yall(const std::string &data)
{
  auto iter = connections.begin();

  while (iter != connections.end())
    {
      if (!iter->second.write(data))
        {
          iter = connections.erase(iter);
        }
      else
        {
          iter++;
        }
    }
}

template <typename T>
Connections<T>::Connections(T *ctx, connected_cb ccb, read_cb rcb) throw()
  : efd(-1), ccb(ccb), rcb(rcb), ctx(ctx)
{
  std::exception e;

  efd = epoll_create1(EPOLL_CLOEXEC);
  if (efd != -1)
    {
      std::cout << "Created main context with efd: " << std::to_string(efd)
                << std::endl;
      return;
    }
  else
    {
      e = std::system_error(errno, std::system_category(), "epoll create");
    }
  throw(e);
}

template <typename T>
Connections<T>::~Connections()
{
  int retry = 5;

  std::cout << "Closing connection library" << std::endl;
  while (close(efd) && retry--)
    {
      if (errno == EBADFD)
        {
          abort();
        }
    }
}
#endif
