#include "socket.hpp"

extern "C"
{
#include <netdb.h>
#include <sys/un.h>
}

using namespace Sukat;

std::string Sukat::saddr_to_string(const struct sockaddr_storage *addr,
                                   socklen_t len)
{
  std::stringstream desc;
  if (addr->ss_family == AF_UNIX)
    {
      const struct sockaddr_un *sun =
        reinterpret_cast<const struct sockaddr_un *>(addr);

      if (len > sizeof(sun->sun_family))
        {
          bool is_abstract = (sun->sun_path[0] == '\0');
          socklen_t string_len = len - (sizeof(sun->sun_family) + is_abstract);

          if (string_len > 0)
            {
              std::string socket_path(sun->sun_path[is_abstract], string_len);

              desc << "(" << (is_abstract ? "@" : "") << socket_path << ")";
            }
          {
            desc << "Not enough data for unix path";
          }
        }
      else
        {
          desc << "Not enough data in sockaddr";
        }
    }
  else
    {
      char host[INET6_ADDRSTRLEN], service[32];
      std::integral_constant<int, NI_NUMERICHOST | NI_NUMERICSERV> flags;
      socklen_t hostlen = sizeof(host), servlen = sizeof(service);

      if (!::getnameinfo(reinterpret_cast<const struct sockaddr *>(addr), len,
                         host, hostlen, service, servlen, flags))
        {
          desc << "(" << host << " [" << service << "])";
        }
      else
        {
          desc << "getnameinfo failed with " << strerror(errno);
        }
    }
  return desc.str();
}

const Socket::sockopts Socket::defaultSockopts = { };

Socket::Socket(__socket_type socktype, Socket::sockopts sockopts, bindopt src)
  : mFd(::socket(src.index()
                   ? std::get<int>(src)
                   : std::get<endpoint>(src).first.ss_family,
                 socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, 0))
{
  std::system_error e;
  bool do_bind = !src.index(); // Only bind if specific end-point given.

  LOG_DBG("Creating socket type: ", socktype);

  if (mFd.fd() != -1)
    {
      if (sockopts.has_value())
        {
          for (const auto &opt : sockopts.value())
            {
              if (::setsockopt(mFd.fd(), SOL_SOCKET, opt.first, &opt.second,
                               sizeof(opt.second)))
                {
                  e = std::system_error(errno, std::system_category(),
                                        "sockopts");
                }
            }
        }

      if (!do_bind || !::bind(mFd.fd(),
                              reinterpret_cast<const struct sockaddr *>(
                                &std::get<endpoint>(src).first),
                              std::get<endpoint>(src).second))
        {
          return;
        }
      else
        {
          e = std::system_error(errno, std::system_category(), "bind");
        }
    }
  else
    {
      e = std::system_error(errno, std::system_category(), "socket create");
    }
  throw(e);
}

Socket::~Socket()
{
  if (fd() != -1)
    {
      LOG_DBG("Closing socket ", this);
    }
}

void Socket::addToEfd(int efd, uint32_t events) const
{
  if (struct epoll_event ev =
        {
          .events = events,
          .data = {.fd = mFd.fd()},
        };
      epoll_ctl(efd, EPOLL_CTL_ADD, mFd.fd(), &ev))
    {
      throw std::system_error(errno, std::system_category(), "Epoll add");
    }
}

std::stringstream SocketConnection::readData() const
{
  std::stringstream data;
  char buf[BUFSIZ];
  int ret;

  while ((ret = ::recv(fd(), buf, sizeof(buf), 0)) > 0)
    {
      LOG_DBG("Read ", ret, " bytes from ", this);
      data.write(buf, ret);
    }
  if (!(ret == -1 &&
        (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)))
    {
      throw std::system_error(errno, std::system_category(), "Read");
    }
  return data;
};

int SocketConnection::write(const struct msghdr &hdr, int flags) const
{
  return ::sendmsg(fd(), &hdr, flags);
}

int SocketConnection::write(void *data, size_t len, int flags) const
{
  struct iovec iov =
    {
      .iov_base = data,
      .iov_len = len
    };
  struct msghdr hdr =
    {
      .msg_iov = &iov,
      .msg_iovlen = 1,
    };

  LOG_DBG("Sending ", len, " bytes to ", this);
  return write(hdr, flags);
}

int SocketConnection::write(const char *data, size_t len, int flags) const
{
  void *data_ptr = const_cast<void *>(reinterpret_cast<const void*>(data));

  return write(data_ptr, len, flags);
}

int SocketConnection::write(const char *data, int flags) const
{
  return write(data, strlen(data), flags);
}

int SocketConnection::write(const std::string &data, int flags) const
{
  return write(data.c_str(), data.length(), flags);
}

int SocketConnection::write(struct iovec &iov, size_t n_iov, int flags) const
{
  struct msghdr hdr =
    {
      .msg_iov = &iov,
      .msg_iovlen = n_iov
    };

  return write(hdr, flags);
}

int SocketConnection::operator<<(const std::ostringstream &data)
{
  return write(data.str(), 1);
}

SocketConnection::SocketConnection(__socket_type socktype, sockopts opts,
                                   Socket::bindopt src,
                                   Socket::endpoint dst)
  : Socket(socktype, opts, src), complete(false)
{
  LOG_DBG("Connecting fd ", fd(), " to ", endpoint_to_string(dst));
  if (!::connect(fd(), reinterpret_cast<const struct sockaddr *>(&dst.first),
                 dst.second))
    {
      LOG_DBG("Connected socket ", this);
      complete = true;
    }
  else if (errno != EINPROGRESS)
    {
      throw std::system_error(errno, std::system_category(), "Connect");
    }
  else
    {
      LOG_DBG("Connection socket ", this, ": ", ::strerror(errno));
      // TCP handshake in progress.
    }
}

int SocketConnection::polloutReady() const
{
  int errcode = 0;
  socklen_t err_len = sizeof(errcode);

  if (!::getsockopt(fd(), SOL_SOCKET, SO_ERROR, &errcode, &err_len))
    {
      return errcode;
    }
  else
    {
      LOG_ERR("Failed to query ", fd(), " SO_ERROR ", ::strerror(errno));
    }
  return -1;
}

bool SocketConnection::ready(int timeout) const
{
  bool bret = false;
  int efd = epoll_create1(EPOLL_CLOEXEC);

  if (efd != -1)
    {
      struct epoll_event ev = {
        .events = EPOLLOUT,
        .data = {.fd = fd()},
      };

      if (!epoll_ctl(efd, EPOLL_CTL_ADD, fd(), &ev))
        {
          if (epoll_wait(efd, &ev, 1, timeout) > 0)
            {
              if (ev.events & EPOLLOUT)
                {
                  LOG_DBG("Connection ", this, " finished");
                  bret = true;
                }
            }
        }
      else
        {
          LOG_ERR("Failed to add ", fd(), " to ", efd, ": ", ::strerror(errno));
        }
      close(efd);
    }
  return bret;
}

unsigned int SocketListener::accept(newClientCb cb, accessCb cb_access) const
{
  unsigned int count = 0;
  Socket::endpoint endpoint({}, sizeof(endpoint.first));
  std::vector<uint8_t> data(BUFSIZ);

  while (newClientType ret{getNewClient(endpoint, data, cb_access)})
    {
      LOG_DBG("New client: ", &ret.value());
      cb(std::move(ret.value()), data);
      endpoint.second = sizeof(endpoint.first);
      count++;
    }
  return count;
}

std::vector<SocketConnection> SocketListener::accept(accessCb cb_access) const
{
  Socket::endpoint endpoint({}, sizeof(endpoint.first));
  std::vector<uint8_t> data(BUFSIZ);
  std::vector<SocketConnection> newClients;

  while (newClientType ret{getNewClient(endpoint, data, cb_access)})
    {
      LOG_DBG("New client: ", &ret.value());
      newClients.emplace_back(std::move(ret.value()));
    }
  return newClients;
}

SocketListenerStream::SocketListenerStream(Socket::bindopt opt,
                    Socket::sockopts opts, __socket_type socktype)
  : SocketListener::SocketListener(socktype, opts, opt)
{
  if (!listen(fd(), 16))
    {
      LOG_DBG("Listening on: ", this);
      // Success.
    }
  else
    {
      throw std::system_error(errno, std::system_category(), "Listen");
    }
}

std::optional<SocketConnection> SocketListenerStream::getNewClient(
  Socket::endpoint &sender, __attribute__((unused)) std::vector<uint8_t> &data,
  accessCb cb_access) const
{
  if (int new_fd =
        ::accept4(fd(), reinterpret_cast<struct sockaddr *>(&sender.first),
                  &sender.second, SOCK_NONBLOCK | SOCK_CLOEXEC);
      new_fd != -1)
    {
      if (std::vector<uint8_t> empty_handshake(0);
          !cb_access || cb_access(sender, empty_handshake) ==
                          SocketListener::accessReturn::ACCESS_NEW)
        {
          return std::make_optional<SocketConnection>(new_fd);
        }
      else
        {
          LOG_DBG("New client ", endpoint_to_string(sender), " denied");
          close(new_fd);
        }
    }
  else
    {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        {
          throw std::system_error(errno, std::system_category(), "Accept");
        }
    }
  return {};
}

SocketListenerUdp::SocketListenerUdp(Socket::bindopt src)
  : SocketListener::SocketListener(
      SOCK_DGRAM,
      std::set{std::make_pair<int, int>(SO_REUSEADDR, 1),
               std::make_pair<int, int>(SO_REUSEPORT, 1)},
      src)
{
  LOG_DBG("Listening UDP on: ", this);
}

std::optional<Socket::endpoint> Socket::getSource() const
{
  endpoint ret({}, sizeof(struct sockaddr_storage));

  if (!::getsockname(fd(), reinterpret_cast<struct sockaddr*>(&ret.first),
                     &ret.second))
    {
      return ret;
    }
  else
    {
      LOG_ERR("Failed to get ", fd(), " bound source: ", ::strerror(errno));
    }
  return {};
}

std::optional<SocketConnection> SocketListenerUdp::getNewClient(
  Socket::endpoint &sender, std::vector<uint8_t> &data,
  accessCb cb_access) const
{
  struct iovec iov =
    {
      .iov_base = data.data(),
      .iov_len = data.capacity()
    };
  struct msghdr hdr =
    {
      .msg_name = &sender.first,
      .msg_namelen = sender.second,
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_control = nullptr,
      .msg_controllen = 0,
      .msg_flags = 0
    };
  Socket::endpoint src = this->getSource().value();

  if (!::getsockname(fd(), reinterpret_cast<struct sockaddr *>(&src.first),
                   &src.second))
    {
      if (int ret = ::recvmsg(fd(), &hdr, 0); ret >= 0)
        {
          SocketListener::accessReturn access_ret =
            SocketListener::accessReturn::ACCESS_NEW;

          data.resize(ret);
          sender.second = hdr.msg_namelen;
          if (cb_access)
            {
              access_ret = cb_access(sender, data);
            }
          if (access_ret == SocketListener::accessReturn::ACCESS_NEW)
            {
              return std::make_optional<SocketConnection>(
                SOCK_DGRAM, std::set{std::make_pair<int, int>(SO_REUSEADDR, 1)},
                src, sender);
            }
          else
            {
              LOG_DBG("Peer ", endpoint_to_string(sender), " ",
                      (access_ret == SocketListener::accessReturn::ACCESS_DENY)
                        ? "denied"
                        : "existed");
            }
        }
      else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        {
          LOG_ERR("Failed to read ", this, ": ", ::strerror(errno));
        }
      else
        {
          ret = 0;
        }
    }
  else
    {
      LOG_ERR("Failed to solve ", fd(), " name: ", ::strerror(errno));
    }
  return {};
}

AddrInfo::AddrInfo(const std::string node,
           std::optional<const std::string> service,
           std::optional<int> family,
           std::optional<int> type)
{
  struct ::addrinfo hints = { };

  hints.ai_flags |= AI_ADDRCONFIG;
  hints.ai_family = (family) ? family.value() : AF_UNSPEC;
  hints.ai_socktype = (type) ? type.value() : SOCK_STREAM;

  if (!getaddrinfo(node.c_str(), (service) ? service.value().c_str() : nullptr,
                   &hints, &mRes))
    {
      struct ::addrinfo *iter;

      for (iter = mRes; iter; iter = iter->ai_next)
        {
          mResults.push_back(iter);
        }
    }
  else
    {
      throw std::system_error(errno, std::system_category(), "getaddrinfo");
    }
}

AddrInfo::~AddrInfo()
{
  if (mRes)
    {
      freeaddrinfo(mRes);
    }
}
