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
#include <unistd.h>
#include <netdb.h>
}

#include "logging.hpp"
#include "fd.hpp"

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
  using endpoint = std::pair<struct sockaddr_storage, socklen_t>;
  using bindopt = std::variant<endpoint, int>; //!< end-point or family.

  /** @brief Creates a new socket and binds it. */
  Socket(int socktype, sockopts opts = defaultSockopts,
         bindopt src = AF_INET6);
  /** @brief Creates a new fd from an accepted connection. */
  Socket(Fd &&fd) : mFd(std::move(fd)) { };

  /** @brief Closes a file descriptor */
  virtual ~Socket();

  int fd() const
  {
    return mFd.fd();
  }

  static auto make_endpoint(struct sockaddr_storage *saddr, socklen_t slen)
  {
    endpoint ep({}, slen);
    ::memcpy(&ep.first, saddr, slen);
    return ep;
  }

  static auto make_endpoint(struct sockaddr *saddr, socklen_t slen)
  {
    return make_endpoint(reinterpret_cast<struct sockaddr_storage *>(saddr),
                         slen);
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
  std::optional<endpoint> getSource() const;

  /** @brief Stringifies the socket */
  friend std::ostream &operator<<(std::ostream &os, Socket const &sock)
  {
    struct sockaddr_storage saddr;
    socklen_t saddr_len = sizeof(saddr);

    os << "fd: " << std::to_string(sock.fd());
    if (!getsockname(sock.fd(), reinterpret_cast<struct sockaddr *>(&saddr),
                     &saddr_len))
      {
        os << ", bound: " << saddr_to_string(&saddr, saddr_len);
      }
    saddr_len = sizeof(saddr);
    if (!getpeername(sock.fd(), reinterpret_cast<struct sockaddr *>(&saddr),
                     &saddr_len))
      {
        os << ", connected: " << saddr_to_string(&saddr, saddr_len);
      }
    return os;
  }

  /** @brief Stringifies a pointer to the socket */
  friend std::ostream &operator<<(std::ostream &os, Socket const *sock)
  {
    os << *sock;
    return os;
  }

  Socket(Socket &&other) : mFd(std::move(other.mFd)){}

  static std::string endpoint_to_string(const endpoint &endpoint)
    {
      return saddr_to_string(&endpoint.first, endpoint.second);
    }

  friend std::ostream &operator<<(std::ostream &os, const endpoint &ep)
    {
      os << endpoint_to_string(ep);
      return os;
    }

 protected:
  static const sockopts defaultSockopts; //!< Default options for socket.

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

  /** @brief Returns true if connected in constructor */
  bool connComplete() const
    {
      return complete;
    }

  SocketConnection(SocketConnection &&other) : Socket(std::move(other))
  {
    complete = other.complete;
  }

  /** @brief Create a new connection from an accepted fd. */
  SocketConnection(Fd fd) : Socket(std::move(fd)) {} ;

  /** @brief Same but bind to the given source and connect */
  SocketConnection(int socktype, sockopts opts,
                   Socket::bindopt src,
                   Socket::endpoint dst);

  /** @brief Create connection with addrinfo as destination. */
  SocketConnection(const struct addrinfo *info,
                   const struct addrinfo *src,
                   sockopts opts = defaultSockopts)
    : SocketConnection(info->ai_socktype, opts,
                       Socket::make_endpoint(src->ai_addr, src->ai_addrlen),
                       Socket::make_endpoint(info->ai_addr, info->ai_addrlen))
  { }

  /** @brief Same but without a source. TODO: How to do above with both? */
  SocketConnection(const struct addrinfo *info, sockopts opts = defaultSockopts)
    : SocketConnection(info->ai_socktype, opts, info->ai_family,
                       Socket::make_endpoint(info->ai_addr, info->ai_addrlen))
  {
  }

  /** @brief Create a new connection by connecting to a given end-point */
  SocketConnection(int socktype, Socket::endpoint dst,
                   sockopts opts = defaultSockopts)
    : SocketConnection(socktype, opts, dst.first.ss_family, dst){};

  // TODO How to do this without mixing it up with debug call?
  /*
  friend std::ostream &operator<<(std::ostream &os,
                                  SocketConnection const *conn);
                                  */

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
    const Socket::endpoint &peer, std::vector<uint8_t> &data)>;
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
    Socket::endpoint &sender, std::vector<uint8_t> &data,
    accessCb cb_access) const = 0;

  /** @brief Create a new listening socket */
  SocketListener(__socket_type socktype, Socket::sockopts opts,
                 Socket::bindopt opt = AF_INET6)
    : Socket(socktype, opts, opt){};
};

/** @brief A TCP listening socket */
class SocketListenerTcp : public SocketListener
{
 public:
   /** @brief Create a new TCP listening socket */
  SocketListenerTcp(Socket::bindopt opt = AF_INET6,
                    Socket::sockopts opts = defaultSockopts);

 protected:
  /** @brief accept a new connection */
  virtual std::optional<SocketConnection> getNewClient(
    Socket::endpoint &sender, std::vector<uint8_t> &data,
    accessCb cb_access) const override;
};

class SocketListenerUdp : public SocketListener
{
public:
   /** @brief Create a new UDP socket that only listens */
  SocketListenerUdp(Socket::bindopt src = AF_INET6);

 protected:
  /** @brief Accept a new UDP connection */
  virtual std::optional<SocketConnection> getNewClient(
    Socket::endpoint &sender, std::vector<uint8_t> &data,
    accessCb cb_access) const override;
};

class AddrInfo
{
public:

  AddrInfo(const std::string node = "localhost",
           std::optional<const std::string> service = {},
           std::optional<int> family = {},
           std::optional<int> type = {});

  ~AddrInfo();

  AddrInfo(const AddrInfo &) = delete;
  AddrInfo(AddrInfo &&other) : mResults(other.mResults), mRes(other.mRes)
    {
      other.mRes = nullptr;
    }

  std::vector<const struct addrinfo *> mResults;

private:

  struct addrinfo *mRes{nullptr};
};

} // namespace Sukat
