#include "socket.hpp"

extern "C"
{
#include <netdb.h>
}

using namespace Sukat;

Fd::~Fd()
{
  if (mFd != -1)
    {
      int retry = 5;

      while (::close(mFd) == -1 && --retry)
        {
          // Generate a core for bad fd investigating.
          assert(errno != EBADFD);
        }
    }
}

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

const socklen_t Socket::inAnyAddrLen = sizeof(struct sockaddr_in6);
const Socket::sockopts Socket::defaultSockopts =
  std::set{std::make_pair<int, int>(SO_REUSEADDR, 1)};

Socket::Socket(int socktype, Socket::sockopts sockopts,
               const struct sockaddr_storage *src, socklen_t src_len) :
  mFd(::socket(src->ss_family, socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, 0))
{
  std::system_error e;

  LOG_DBG("Creating socket family: ", src->ss_family, ", type: ",
              socktype, ", socklen: ", src_len);

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

      if (!bind(mFd.fd(), reinterpret_cast<const struct sockaddr *>(src),
                src_len))
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

std::ostream &Sukat::operator<<(std::ostream &os, Sukat::Socket const &sock)
{
  struct sockaddr_storage saddr;
  socklen_t saddr_len = sizeof(saddr);

  os << "fd: " << std::to_string(sock.fd());
  if (!getsockname(sock.fd(), reinterpret_cast<struct sockaddr *>(&saddr),
                   &saddr_len))
    {
      os << ", bound: " << saddr_to_string(&saddr, saddr_len);
    }
  return os;
}

SocketConnection::SocketConnection(int socktype,
                                   const struct sockaddr_storage &dst,
                                   socklen_t slen)
  : Socket(socktype), complete(false)
{
  LOG_DBG("Connecting to ", saddr_to_string(&dst, slen));
  if (!::connect(fd(), reinterpret_cast<const struct sockaddr *>(&dst), slen))
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
      LOG_DBG("Connection in progress in socket ", this);
      // TCP handshake in progress.
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
                                   const struct sockaddr_storage &src,
                                   socklen_t slen,
                                   const struct sockaddr_storage &dst,
                                   socklen_t dlen)
  : Socket(socktype, opts, &src, slen), complete(false)
{
  if (!::connect(fd(), reinterpret_cast<const struct sockaddr *>(&dst), dlen))
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
      LOG_DBG("Connection in progress in socket ", this);
      // TCP handshake in progress.
    }
}

int SocketConnection::polloutReady() const
{
  int errcode = 0;

  if (!::setsockopt(fd(), SOL_SOCKET, SO_ERROR, &errcode, sizeof(errcode)))
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
  struct sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  std::vector<uint8_t> data(BUFSIZ);

  while (newClientType ret{getNewClient(&addr, &len, data, cb_access)})
    {
      LOG_DBG("New client: ", &ret.value());
      cb(std::move(ret.value()), data);
      len = sizeof(addr);
      count++;
    }
  return count;
}

SocketListenerTcp::SocketListenerTcp(const struct sockaddr_storage *saddr,
                                     socklen_t slen)
  : SocketListener::SocketListener(SOCK_STREAM, Socket::defaultSockopts, saddr,
                                   slen)
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
  struct sockaddr_storage *dst, socklen_t *slen,
  __attribute__((unused)) std::vector<uint8_t> &data, accessCb cb_access) const
{
  if (int new_fd = ::accept4(fd(), reinterpret_cast<struct sockaddr *>(dst),
                             slen, SOCK_NONBLOCK | SOCK_CLOEXEC);
      new_fd != -1)
    {
      if (std::vector<uint8_t> empty_handshake(0);
          !cb_access || cb_access(dst, *slen, empty_handshake) ==
                          SocketListener::accessReturn::ACCESS_NEW)
        {
          return std::make_optional<SocketConnection>(new_fd);
        }
      else
        {
          LOG_DBG("New client ", saddr_to_string(dst, *slen), " denied");
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

SocketListenerUdp::SocketListenerUdp(const struct sockaddr_storage *saddr,
                                     socklen_t slen)
  : SocketListener::SocketListener(
      SOCK_DGRAM,
      std::set{std::make_pair<int, int>(SO_REUSEADDR, 1),
               std::make_pair<int, int>(SO_REUSEPORT, 1)},
      saddr, slen)
{
  LOG_DBG("Listening UDP on: ", this);
}

bool Socket::getSource(struct sockaddr_storage &src, socklen_t &slen) const
{
  if (!::getsockname(fd(), reinterpret_cast<struct sockaddr *>(&src), &slen))
      {
      return true;
      }
  else
    {
      LOG_ERR("Failed to get ", fd(), " bound source: ", ::strerror(errno));
    }
  return false;
}

std::optional<SocketConnection> SocketListenerUdp::getNewClient(
  struct sockaddr_storage *dst, socklen_t *slen, std::vector<uint8_t> &data,
  accessCb cb_access) const
{
  struct iovec iov =
    {
      .iov_base = data.data(),
      .iov_len = data.capacity()
    };
  struct msghdr hdr =
    {
      .msg_name = dst,
      .msg_namelen = *slen,
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_control = nullptr,
      .msg_controllen = 0,
      .msg_flags = 0
    };
  struct sockaddr_storage sockname;
  socklen_t sockname_len = sizeof(sockname);

  if (!::getsockname(fd(), reinterpret_cast<struct sockaddr *>(&sockname),
                   &sockname_len))
    {
      if (int ret = ::recvmsg(fd(), &hdr, 0); ret >= 0)
        {
          SocketListener::accessReturn access_ret =
            SocketListener::accessReturn::ACCESS_NEW;

          data.resize(ret);
          if (cb_access)
            {
              access_ret = cb_access(dst, hdr.msg_namelen, data);
            }
          if (access_ret == SocketListener::accessReturn::ACCESS_NEW)
            {
              return std::make_optional<SocketConnection>(
                SOCK_DGRAM, defaultSockopts, sockname, sockname_len, *dst,
                hdr.msg_namelen);
            }
          else
            {
              LOG_DBG("Peer ", saddr_to_string(dst, *slen), " ",
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
