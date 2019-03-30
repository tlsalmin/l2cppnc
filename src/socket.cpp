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

const socklen_t Socket::inAnyAddrLen = sizeof(struct sockaddr_in6);
const Socket::sockopts Socket::defaultSockopts =
  std::set{std::make_pair<int, int>(SO_REUSEADDR, 1)};

Socket::Socket(int socktype, Socket::sockopts sockopts,
               const struct sockaddr_storage *src, socklen_t src_len)
  :  mSourceLen(src_len)
{
  std::system_error e;
  memset(&mSource, 0, sizeof(mSource));
  if (mSourceLen)
    {
      memcpy(&mSource, src, mSourceLen);
    }

  LOG_DBG("Creating socket family: ", mSource.ss_family, ", type: ",
              socktype, ", socklen: ", mSourceLen);

  mFd = ::socket(mSource.ss_family, socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (mFd != -1)
    {
      for (const auto &opt : sockopts)
        {
          if (::setsockopt(mFd, SOL_SOCKET, opt.first, &opt.second,
                           sizeof(opt.second)))
            {
              e = std::system_error(errno, std::system_category(), "sockopts");
            }
        }

      if (!bind(mFd, reinterpret_cast<struct sockaddr *>(&mSource), mSourceLen))
        {
          if (src != &inAnyAddr ||
              ((mSourceLen = sizeof(mSource)) &&
               getsockname(mFd, reinterpret_cast<struct sockaddr *>(&mSource),
                           &mSourceLen)))

            {
              e =
                std::system_error(errno, std::system_category(), "getsockname");
            }
          else
            {
              LOG_DBG("Created socket: ", this);
              return;
            }
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

Socket::Socket(int fd, const struct sockaddr_storage *src, socklen_t src_len)
  :  mSourceLen(src_len), mFd(fd)
{
  ::memset(&mSource, 0, sizeof(mSource));
  ::memcpy(&mSource, src, mSourceLen);
}

Socket::~Socket()
{
  int retry = 5;

  LOG_DBG("Closing socket ", this);
  while (::close(mFd) == -1 && --retry)
    {
      // Generate a core for bad fd investigating.
      assert(errno != EBADFD);
    }
}

void Socket::addToEfd(int efd) const
{
  struct epoll_event ev = {};

  ev.data.fd = mFd;
  ev.events = EPOLLIN;

  if (epoll_ctl(efd, EPOLL_CTL_ADD, mFd, &ev))
    {
      throw std::system_error(errno, std::system_category(), "Epoll add");
    }
}

SocketConnection::SocketConnection(int socktype,
                                   const struct sockaddr_storage &dst,
                                   socklen_t slen)
  : Socket(socktype), mDestinationLen(slen), complete(false)
{
  memcpy(&mDestination, &dst, sizeof(mDestination));
  if (!connect(fd(), reinterpret_cast<const struct sockaddr *>(&dst), slen))
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
  return ::send(fd(), data, len, 0);
}

SocketConnection::SocketConnection(int fd, const SocketListener &main,
                                   socklen_t dst_len,
                                   const struct sockaddr_storage *dst)
  : Socket(fd, &main.mSource, main.mSourceLen)
{
  ::memset(&mDestination, 0, sizeof(mDestination));
  if (dst && dst_len)
    {
      ::memcpy(&mDestination, dst, dst_len);
    }
}

bool SocketConnection::finish()
{
  int ret =
    connect(fd(), reinterpret_cast<const struct sockaddr *>(&mDestination),
            mDestinationLen);
  if (!ret || (ret == -1 && errno == EINPROGRESS))
    {
      LOG_DBG("Connection ", this,
                  (complete) ? "in progress" : " finished");
      complete = (ret != -1);
    }
  else
    {
      return false;
    }
  return true;
}

unsigned int SocketListener::acceptNew(
  std::function<void(std::unique_ptr<SocketConnection> &&)> new_fn) const
{
  int ret;
  unsigned int count = 0;
  struct sockaddr_storage addr;
  socklen_t len = sizeof(addr);

  while ((ret = getNewClient(&addr, &len)) != -1)
    {
      LOG_DBG("New fd: ", ret,
              ", connected from: ", saddr_to_string(&addr, len));
      new_fn(std::make_unique<SocketConnection>(ret, *this, len, &addr));
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
      LOG_DBG("Listening on: ", this);
      // Success.
    }
  else
    {
      throw std::system_error(errno, std::system_category(), "Listen");
    }
}

int SocketListenerTcp::getNewClient(struct sockaddr_storage *dst,
                                    socklen_t *slen) const
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
