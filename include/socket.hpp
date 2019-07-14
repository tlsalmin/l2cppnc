#pragma once

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <variant>

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
class Fd
{
 public:
  Fd(int fd) : mFd(fd){};
  Fd() = default;
  ~Fd();
  Fd(Fd &&other)
  {
    mFd = other.mFd;
    other.mFd = -1;
  }
  auto fd() const
  {
    return mFd;
  };

 private:
  int mFd{-1};
};

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
  Socket(Fd &&fd) : mFd(std::move(fd)) { };

  /** @brief Closes a file descriptor */
  ~Socket();

  int fd() const
  {
    return mFd.fd();
  }

  /** @brief Checks if object can accept new connections */
  virtual bool canAccept() const = 0;

  /** @brief Add given socket to epoll fd */
  void addToEfd(int efd, uint32_t events = EPOLLIN) const;

  /** @brief For std::container */
  bool operator<(const Socket &other) const
  {
    return mFd.fd() < other.mFd.fd();
  }

  /** @brief Fetches the source address and address len */
  bool getSource(struct sockaddr_storage &src, socklen_t &slen) const;

  /** @brief Stringifies the socket */
  friend std::ostream &operator<<(std::ostream &os, Socket const &sock);

  /** @brief Stringifies a pointer to the socket */
  friend std::ostream &operator<<(std::ostream &os, Socket const *sock)
  {
    os << *sock;
    return os;
  }

  Socket(Socket &&other) : mFd(std::move(other.mFd)){}

 protected:
  static const socklen_t inAnyAddrLen;   //!< Default socket address length.
  static const sockopts defaultSockopts; //!< Default options for socket.
  /**!< Default sockaddr for a new socket . */
  static constexpr struct sockaddr_storage inAnyAddr = {.ss_family = AF_INET6};

 private:
  Fd mFd; //!< File descriptor
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

  /** @brief on PollOut checks SOL_ERROR
   *
   * Used to determine if a non-blocking socket has connected properly.
   *
   * @return == 0       Connected.
   * @return < 0        Failure, error code. (maybe errno compatible?).
   */
  int polloutReady() const;

  /** @brief Checker to determine if socket is writable/readable yet. */
  bool ready(int timeout = 0) const;

  virtual bool canAccept() const override
  {
     return false;
  };

  /** @brief Create a new connection from an accepted fd. */
  SocketConnection(Fd fd) : Socket(std::move(fd)) {} ;

  /** @brief Create a new connection by connecting to a given end-point */
  SocketConnection(int socktype, const struct sockaddr_storage &dst,
                   socklen_t dlen);

  /** @brief Same but bind to the given source and connect */
  SocketConnection(int socktype, sockopts opts,
                   const struct sockaddr_storage &src, socklen_t slen,
                   const struct sockaddr_storage &dst, socklen_t dlen);

 private:
  bool complete;                        //!< Connect complete.
};

/** @brief A listening socket .*/
class SocketListener : public Socket
{
 public:
   /** @brief Return values for accessCb determining what to do with data */
  enum class accessReturn
  {
    ACCESS_NEW,    //!< Create a new connection.
    ACCESS_EXISTS, //!< Peer already exists.
    ACCESS_DENY    //!< Deny peer.
  };
  /**
   * @brief Callback per new client.
   *
   * @param new_conn   Unique pointer to the new connection.
   * @param data       Possible handshake data received while accepting client.
   */
  using newClientCb = std::function<void(
    SocketConnection &&new_conn, std::vector<uint8_t> &data)>;

  using accessCb = std::function<accessReturn(
    const struct sockaddr_storage *peer,
    socklen_t peer_len, std::vector<uint8_t> &data)>;
  /**
   * @brief Accept any new pending connections
   *
   * @param cb         Callback per connection.
   *
   * @return Number of accepted connections.
   */
  unsigned int acceptNew(newClientCb cbj, accessCb cb_access = nullptr) const;

  virtual bool canAccept() const override
  {
     return true;
  };

 protected:

  using newClientType = std::optional<SocketConnection>;
  /**
   * @brief Fetch a new connection from derived class
   *
   * @param dst         Storage for the peers end-point.
   * @param slen        Length of previous storage.
   * @param data        Vector containing possible handshake data received.
   *
   * @return std::any_cast<SocketConnection> New connection.
   * @return std::any_cast<int> 0       Socket exhausted.
   * @return std::any_cast<int> -1      Socket error.
   */
  virtual newClientType getNewClient(
    struct sockaddr_storage *dst, socklen_t *slen, std::vector<uint8_t> &data,
    accessCb cb_access) const = 0;

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
  virtual std::optional<SocketConnection> getNewClient(
    struct sockaddr_storage *dst, socklen_t *slen, std::vector<uint8_t> &data,
    accessCb cb_access) const override;
};

class SocketListenerUdp : public SocketListener
{
public:
   /** @brief Create a new UDP socket that only listens */
  SocketListenerUdp(const struct sockaddr_storage *saddr = &Socket::inAnyAddr,
                    socklen_t slen = Socket::inAnyAddrLen);

 protected:
  /** @brief Accept a new UDP connection */
  virtual std::optional<SocketConnection> getNewClient(
    struct sockaddr_storage *dst, socklen_t *slen, std::vector<uint8_t> &data,
    accessCb cb_access) const override;
};

} // namespace Sukat
