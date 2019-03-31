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
     : mSourceLen(sizeof(mSource))
{
  std::system_error e;

  LOG_DBG("Creating socket family: ", src->ss_family, ", type: ",
              socktype, ", socklen: ", src_len);

  mFd = ::socket(src->ss_family, socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
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

      if (!bind(mFd, reinterpret_cast<const struct sockaddr *>(src), src_len))
        {
          // Extra if already known.
          if (!getsockname(mFd, reinterpret_cast<struct sockaddr *>(&mSource),
                           &mSourceLen))

            {
              LOG_DBG("Created socket: ", this);
              return;
            }
          else
            {
              e =
                std::system_error(errno, std::system_category(), "getsockname");
            }
        }
      else
        {
          e = std::system_error(errno, std::system_category(), "bind");
        }
      close(mFd);
      mFd = -1;
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
  if (mFd != -1)
    {
      int retry = 5;

      LOG_DBG("Closing socket ", this);
      while (::close(mFd) == -1 && --retry)
        {
          // Generate a core for bad fd investigating.
          assert(errno != EBADFD);
        }
    }
}

void Socket::addToEfd(int efd) const
{
  if (struct epoll_event ev =
        {
          .events = EPOLLIN,
          .data = {.fd = mFd},
        };
      epoll_ctl(efd, EPOLL_CTL_ADD, mFd, &ev))
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
  LOG_DBG("Connecting to ", saddr_to_string(&mDestination, mDestinationLen));
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
  LOG_DBG("Sending ", len, " bytes to ", this);
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

SocketConnection::SocketConnection(int socktype, sockopts opts,
                                   const struct sockaddr_storage &src,
                                   socklen_t slen,
                                   const struct sockaddr_storage &dst,
                                   socklen_t dlen)
  : Socket(socktype, opts, &src, slen), mDestinationLen(dlen), complete(false)
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

bool SocketConnection::finish()
{
  if (int ret =
        connect(fd(), reinterpret_cast<const struct sockaddr *>(&mDestination),
                mDestinationLen);
      !ret || (ret == -1 && errno == EINPROGRESS))
    {
      LOG_DBG("Connection ", this, (complete) ? "in progress" : " finished");
      complete = (ret != -1);
    }
  else
    {
      return false;
    }
  return true;
}

unsigned int SocketListener::acceptNew(newClientCb cb,
                                       accessCb cb_access) const
{
  std::unique_ptr<SocketConnection> ret(nullptr);
  unsigned int count = 0;
  struct sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  std::vector<uint8_t> data(BUFSIZ);

  while ((ret = getNewClient(&addr, &len, data, cb_access)) != nullptr)
    {
      LOG_DBG("New fd: ", ret->fd(),
              ", connected from: ", saddr_to_string(&addr, len));
      cb(std::move(ret), data);
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

std::unique_ptr<SocketConnection> SocketListenerTcp::getNewClient(
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
          return std::make_unique<SocketConnection>(new_fd, *this, *slen, dst);
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
  return nullptr;
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

std::unique_ptr<SocketConnection> SocketListenerUdp::getNewClient(
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
          return std::make_unique<SocketConnection>(
            SOCK_DGRAM, defaultSockopts, this->mSource, this->mSourceLen, *dst,
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
      LOG_ERR("Failed to read ", this, ": ", strerror(errno));
    }
  return nullptr;
}
