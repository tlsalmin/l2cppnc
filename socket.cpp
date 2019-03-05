#include <exception>
#include <cstring>
#include <iostream>
#include <system_error>

extern "C"
{
#include <netdb.h>
#include <unistd.h>
#include <sys/epoll.h>
}

#include "socket.hpp"

template<typename T>
Connections<T>::SocketConnection::SocketConnection(struct addrinfo *dst,
                                                   struct addrinfo *src)
  : source_len(src ? src->ai_addrlen : 0),
    destination_len((dst) ? dst->ai_addrlen : 0),
    fd(-1)
{
  std::exception e;

  if (dst || src)
    {
      struct addrinfo *model = (src) ? src : dst;
      const int family = model->ai_family, socktype = model->ai_socktype;
      fd = socket(family, socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

      if (fd >= 0)
        {
          if (source_len)
            {
              std::memcpy(&source, &src->ai_addr, source_len);
            }
          else
            {
              source_len = dst->ai_addrlen;
            }
          if (!bind(fd, reinterpret_cast<struct sockaddr*>(&source), source_len))
            {
              int conret = 0;

              if (destination_len)
                {
                  std::memcpy(&destination, dst->ai_addr, destination_len);
                }

              if ((!dst &&
                   ((socktype != SOCK_STREAM && socktype != SOCK_SEQPACKET) ||
                    !listen(fd, 16))) ||
                  (dst &&
                   (!(conret = connect(
                        fd, reinterpret_cast<struct sockaddr *>(&destination),
                        destination_len)) ||
                    (conret == -1 && errno == EINPROGRESS))))
                {
                  std::cout
                    << ((dst) ? std::string("Listening on") : "Connected to")
                    << std::endl;
                  // Success.
                  return;
                }
              else
                {
                  e = std::system_error(errno, std::system_category(),
                                        (dst) ? "connect" : "listen");
                }
            }
          else
            {
              e = std::system_error(errno, std::system_category(), "bind");
            }
          close(fd);
          fd = -1;
        }
      else
        {
          e = std::system_error(errno, std::system_category(), "socket create");
        }
    }
  else
    {
      e = std::invalid_argument("No source or destination given for socket");
    }
  throw(e);
}

template<typename T>
Connections<T>::SocketConnection::~SocketConnection()
{
  int retry = 5;

  while (close(fd) && retry--)
    {
      if (errno == EBADFD)
        {
          throw std::system_error(
            errno, std::system_category(),
            std::string({"Trying to close bad fd", std::to_string(fd)}));
        }
    }
}

template<typename T>
Connections<T>::Connections(std::unique_ptr<T> context) : context(context), efd(-1)
{
  std::exception e;

  efd = epoll_create1(EPOLL_CLOEXEC);
  if (efd != -1)
    {
      return;
    }
  else
    {
      e = std::system_error(errno, std::system_category(), "epoll create");
    }
  throw(e);
}

template<typename T>
Connections<T>::~Connections()
{
  int retry = 5;

  while (close(efd) && retry--)
    {
      if (errno == EBADFD)
        {
          throw std::system_error(
            errno, std::system_category(),
            std::string({"Trying to close bad fd", std::to_string(efd)}));
        }
    }
}

template <typename T>
auto Connections<T>::connect_to_endpoint(struct addrinfo *dst,
                                         struct addrinfo *src)
  -> std::unique_ptr<Connections::SocketConnection>
{
  auto ret = std::make_unique<Connections::SocketConnection>(nullptr);

  try
    {
      auto inserted = connections.emplace(SocketConnection(dst, src));

      if (inserted.second)
        {
          struct epoll_event ev = {.events = EPOLLIN,
                                   .data = {.fd = inserted.first.fd}};

          if (!epoll_ctl(efd, EPOLL_CTL_ADD, inserted.first.fd, &ev))
            {
              return std::make_unique(&inserted.second);
            }
          else
            {
              // TODO proper exception.
              throw std::invalid_argument(
                std::string({"Couldn't add a new connection to epoll: %d",
                             std::to_string(inserted.first.fd)}));
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
