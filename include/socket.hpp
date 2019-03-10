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

#include "logging.hpp"

namespace Sukat
{
/** @brief Utility: Stringifies a sockaddr */
std::string saddr_to_string(const struct sockaddr_storage *addr, socklen_t len);

/** @brief Main socket class contaning the file descriptor */
class Socket
{
 public:
  /** @brief Sockopts passed before bind. Pair as {optname, val} */
  using sockopts = std::set<std::pair<int, int>>;

  /** @brief Creates a new socket and binds it. */
  Socket(int socktype, sockopts opts = defaultSockopts,
         const struct sockaddr_storage *src = &inAnyAddr,
         socklen_t src_len = inAnyAddrLen);
  /** @brief Creates a new fd from an accepted connection. */
  Socket(int fd, const struct sockaddr_storage *src, socklen_t src_len);

  /** @brief Closes a file descriptor */
  ~Socket();

  int fd() const
  {
    return mFd;
  }

  /** @brief Add given socket to epoll fd */
  void addToEfd(int efd) const;

  /** @brief For std::container */
  bool operator<(const Socket &other) const
  {
    return mFd < other.mFd;
  }

  /** @brief Fetches the source address and address len */
  const struct sockaddr_storage *getSource(socklen_t *slen)
  {
    if (slen)
      {
        *slen = mSourceLen;
      }
    return &mSource;
  }

  /** @brief Stringifies the socket */
  friend std::ostream &operator<<(std::ostream &os, Socket const &sock)
  {
    os << "fd: " << std::to_string(sock.mFd)
       << ", bound: " << saddr_to_string(&sock.mSource, sock.mSourceLen);
    return os;
  }

  /** @brief Stringifies a pointer to the socket */
  friend std::ostream &operator<<(std::ostream &os, Socket const *sock)
  {
    os << *sock;
    return os;
  }

  socklen_t mSourceLen;            //!< Length of source bound to.
  struct sockaddr_storage mSource; //!< Storage of bound data.

 protected:
  static const socklen_t inAnyAddrLen;   //!< Default socket address length.
  static const sockopts defaultSockopts; //!< Default options for socket.
  /**!< Default sockaddr for a new socket . */
  static constexpr struct sockaddr_storage inAnyAddr = {.ss_family = AF_INET6};

 private:
  int mFd{-1}; //!< File descriptor
};

/** Forward decl */
class SocketListener;

/** @brief Socket connection describing a connected socket */
class SocketConnection : public Socket
{
 public:
  /** @brief Read data from connection */
  virtual std::stringstream readData() const;

  /** @brief Write data to connection */
  virtual int writeData(const uint8_t *data, size_t len) const;

  /** @brief Checker to determine if socket is writable/readable yet. */
  bool ready() const
  {
    return complete;
  };

  /** @brief Finish the connection (3-way handshake on TCP */
  bool finish();

  /** @brief Create a new connection from an accepted fd. */
  SocketConnection(int fd, const SocketListener &main, socklen_t dst_len = 0,
                   const struct sockaddr_storage *dst = nullptr);

  /** @brief Create a new connection by connecting to a given end-point */
  SocketConnection(int socktype, const struct sockaddr_storage &dst,
                   socklen_t slen);

 private:
  socklen_t mDestinationLen;            //!< Socket destination.
  struct sockaddr_storage mDestination; //!< Destination length.
  bool complete;                        //!< Connect complete.
};

/** @brief A listening socket .*/
class SocketListener : public Socket
{
 public:
   /** @brief Accept any new pending connections */
  unsigned int acceptNew(
    std::function<void(std::unique_ptr<SocketConnection> &&)> new_fn) const;

 protected:
  /** @brief Fetch a new connection from derived class */
  virtual int getNewClient(struct sockaddr_storage *dst,
                           socklen_t *slen) const = 0;

  /** @brief Create a new listening socket */
  SocketListener(__socket_type socktype, Socket::sockopts opts,
                 const struct sockaddr_storage *saddr, socklen_t slen)
    : Socket(socktype, opts, saddr, slen){};
};

/** @brief A TCP listening socket */
class SocketListenerTcp : public SocketListener
{
 public:
   /** @brief Create a new TCP listening socket */
  SocketListenerTcp(const struct sockaddr_storage *saddr = &Socket::inAnyAddr,
                    socklen_t slen = Socket::inAnyAddrLen);

 protected:
  /** @brief accept a new connection */
  virtual int getNewClient(struct sockaddr_storage *dst,
                           socklen_t *slen) const override;
};

} // namespace Sukat
