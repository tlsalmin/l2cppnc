#pragma once

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

extern "C"
{
#include <assert.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
}

namespace Sukat
{
class Socket
{
 public:
  Socket(int socktype,
         std::set<std::pair<int, int>> sockopts = std::set<std::pair<int, int>>(
           std::make_pair<int, int>(SO_REUSEADDR, 1)),
         const struct sockaddr_storage *src = &inAnyAddr,
         socklen_t src_len = inAnyAddrLen());
  ~Socket()
  {
    int retry = 5;

    while (::close(mFd) == -1 && --retry)
      {
        assert(errno != EBADFD);
      }
  }

  int fd() const
  {
    return mFd;
  }

  void addToEfd(int efd) const
  {
    struct epoll_event ev = {};

    ev.data.fd = mFd;
    ev.events = EPOLLIN;

    if (epoll_ctl(efd, EPOLL_CTL_MOD, mFd, &ev))
      {
        throw std::system_error(errno, std::system_category(), "Epoll add");
      }
  }
  /** @brief For std::container */
  bool operator<(const Socket &other) const
  {
    return mFd < other.mFd;
  }

 protected:
  static constexpr socklen_t inAnyAddrLen = sizeof(struct sockaddr_in6);
  static constexpr struct sockaddr_storage inAnyAddr = {.ss_family = AF_INET6};

  int mFd;

  socklen_t mSourceLen;
  struct sockaddr_storage mSource;
};

class SocketConnection : public Socket
{
 public:
  virtual std::stringstream readData() const
  {
    std::stringstream data;
    char buf[BUFSIZ];
    int ret;

    while ((ret = ::recv(fd(), buf, sizeof(buf), 0)) > 0)
      {
        data.write(buf, ret);
      }
    if (!(ret == -1 &&
          (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)))
      {
        throw std::system_error(errno, std::system_category(), "Read");
      }
  };

  virtual int writeData(const uint8_t *data, size_t len) const
  {
    return ::send(fd(), data, len, 0);
  }

  bool ready() const
  {
    return complete;
  };
  bool finish()
  {
    int ret =
      connect(fd(), reinterpret_cast<const struct sockaddr *>(&mDestination),
              mDestinationLen);
    if (!ret || (ret == -1 && errno == EINPROGRESS))
      {
        complete = (ret != -1);
      }
    else
      {
        return false;
      }
    return true;
  }

 protected:
  SocketConnection(int fd, const Socket &main, socklen_t dst_len = 0,
                   struct sockaddr_storage *dst = nullptr)
    : mFd(fd), mSourceLen(main.mSourceLen), mDestinationLen(dst_len)
  {
    memcpy(&mSource, main.mSource, sizeof(mSource));
    if (dst)
      {
        memcpy(mDestination, dst, sizeof(mDestination));
      }
  }
  SocketConnection(int socktype, struct sockaddr_storage &dst, socklen_t slen)
    : Socket(socktype), mDestinationLen(slen), complete(false)
  {
    memcpy(&mDestination, &dst, sizeof(mDestination));
    if (!connect(fd(), reinterpret_cast<struct sockaddr *>(&dst), slen))
      {
        complete = true;
      }
    else if (errno != EINPROGRESS)
      {
        throw std::system_error(errno, std::system_category(), "Connect");
      }
    else
      {
        // TCP handshake in progress.
      }
  }

 private:
  socklen_t mDestinationLen;
  struct sockaddr_storage mDestination;
  bool complete; //!< Connect complete.
};

class SocketListener : public Socket
{
 public:
  std::unique_ptr<SocketConnection> acceptNew(
    std::function<void(std::unique_ptr<SocketConnection>)> new_fn) const
  {
    int ret;
    struct sockaddr_storage *addr;
    socklen_t len = sizeof(addr);

    while ((ret = getNewClient(addr, &len)) != -1)
      {
        new_fn(std::make_unique<SocketConnection>(ret, this, addr, len));
        len = sizeof(addr);
      }
  }

 protected:
  virtual int getNewClient(struct sockaddr_storage *dst,
                           socklen_t *slen) const = 0;
};

class SocketListenerTcp : public SocketListener
{
 public:
  SocketListenerTcp(const struct sockaddr_storage *saddr = &Socket::inAnyAddr,
                    socklen_t slen = Socket::inAnyAddrLen())
    : Socket(SOCK_STREAM, std::make_pair<int, int>(SO_REUSEADDR, 1), saddr,
             slen)
  {
    if (!listen(fd(), 16))
      {
        // Success.
      }
    else
      {
        throw std::system_error(errno, std::system_category(), "Listen");
      }
  }

 protected:
  virtual int getNewClient(struct sockaddr_storage *dst,
                           socklen_t *slen) const override
  {
    int new_fd = ::accept4(fd(), reinterpret_cast<struct sockaddr *>(dst), slen,
                           SOCK_NONBLOCK | SOCK_CLOEXEC);

    if (new_fd != -1)
      {
        return new_fd;
      }
    else
      {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
          {
            throw std::system_error(errno, std::system_category(), "Accept");
          }
      }
    return -1;
  }
};

} // namespace Sukat
