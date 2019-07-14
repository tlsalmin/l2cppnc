#include "socket.hpp"

extern "C"
{
#include <netdb.h>
}

using namespace Sukat;

std::string Sukat::saddr_to_string(const struct sockaddr_storage *addr,
                                   socklen_t len)
{
  char host[INET6_ADDRSTRLEN], service[32];
  std::integral_constant<int, NI_NUMERICHOST | NI_NUMERICSERV> flags;
  socklen_t hostlen = sizeof(host), servlen = sizeof(service);
  std::stringstream desc;

  if (!::getnameinfo(reinterpret_cast<const struct sockaddr *>(addr), len, host,
                     hostlen, service, servlen, flags))
    {
      desc << "(" << host << " [" << service << "])";
    }
  else
    {
      throw std::system_error(errno, std::system_category(), "getnameinfo");
    }
  return desc.str();
}

const Socket::sockopts Socket::defaultSockopts =
  std::set{std::make_pair<int, int>(SO_REUSEADDR, 1)};

Socket::Socket(int socktype, Socket::sockopts sockopts, bindopt src)
  : mFd(::socket(std::holds_alternative<int>(src)
                   ? std::get<int>(src)
                   : std::get<endpoint>(src).first.ss_family,
                 socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, 0))
{
  std::system_error e;
  struct sockaddr_storage saddr = { };
  socklen_t slen = sizeof(saddr);

  if (!src.index())
    {
      slen = std::get<endpoint>(src).second;
      assert(slen <= sizeof(saddr));
      memcpy(&saddr, &std::get<endpoint>(src).first, slen);
    }
  else if (src.index() == 1)
    {
      saddr.ss_family = std::get<int>(src);
    }

  LOG_DBG("Creating socket family: ", saddr.ss_family, ", type: ",
              socktype, ", socklen: ", slen);

  if (mFd.fd() != -1)
    {
      for (const auto &opt : sockopts)
        {
          if (::setsockopt(mFd.fd(), SOL_SOCKET, opt.first, &opt.second,
                           sizeof(opt.second)))
            {
              e = std::system_error(errno, std::system_category(), "sockopts");
            }
        }

      if (!bind(mFd.fd(), reinterpret_cast<const struct sockaddr *>(&saddr),
                slen))
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
  LOG_DBG("Closing socket ", this);
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

int SocketConnection::writeData(const uint8_t *data, size_t len) const
{
  LOG_DBG("Sending ", len, " bytes to ", this);
  return ::send(fd(), data, len, 0);
}

SocketConnection::SocketConnection(int socktype, sockopts opts,
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

unsigned int SocketListener::acceptNew(newClientCb cb,
                                       accessCb cb_access) const
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

SocketListenerTcp::SocketListenerTcp(Socket::bindopt opt,
                    Socket::sockopts opts)
  : SocketListener::SocketListener(SOCK_STREAM, opts, opt)
{
  if (!listen(fd(), 16))
    {
      LOG_DBG("Listening TCP on: ", this);
      // Success.
    }
  else
    {
      throw std::system_error(errno, std::system_category(), "Listen");
    }
}

std::optional<SocketConnection> SocketListenerTcp::getNewClient(
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
                SOCK_DGRAM, defaultSockopts, src, sender);
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
