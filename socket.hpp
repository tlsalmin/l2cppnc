#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <map>
#include <sstream>

extern "C"
{
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
}

template <typename T>
class Connections
{
 public:
  class SocketConnection
  {
    friend class Connections;

    socklen_t source_len;
    struct sockaddr_storage source;
    socklen_t destination_len;
    struct sockaddr_storage destination;
    bool finished;

   protected:
    int fd;

   public:
    bool operator<(const SocketConnection &other) const
    {
      return fd < other.fd;
    }
    bool operator==(const int other) const
    {
      return fd == other;
    }

    SocketConnection(struct addrinfo *dst, struct addrinfo *src = nullptr)
      : source_len(src ? src->ai_addrlen : 0),
        destination_len((dst) ? dst->ai_addrlen : 0),
        fd(-1)
    {
      std::system_error e;
      std::memset(&source, 0, sizeof(source));
      std::memset(&destination, 0, sizeof(destination));

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
                  source.ss_family = family;
                }
              if (!bind(fd, reinterpret_cast<struct sockaddr *>(&source),
                        source_len))
                {
                  int conret = 0;

                  if (destination_len)
                    {
                      std::memcpy(&destination, dst->ai_addr, destination_len);
                    }

                  if ((!dst && ((socktype != SOCK_STREAM &&
                                 socktype != SOCK_SEQPACKET) ||
                                !listen(fd, 16))) ||
                      (dst &&
                       (!(conret = connect(
                            fd,
                            reinterpret_cast<struct sockaddr *>(&destination),
                            destination_len)) ||
                        (conret == -1 && errno == EINPROGRESS))))
                    {
                      std::cout << ((!dst) ? std::string("Listening on")
                                           : "Connected to")
                                << " with fd " << std::to_string(fd)
                                << std::endl;
                      finished = (conret != -1);
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
              e = std::system_error(errno, std::system_category(),
                                    "socket create");
            }
        }
      else
        {
          throw std::invalid_argument(
            "No source or destination given for socket");
        }
      throw(e);
    }

    bool finish()
    {
      int ret =
        connect(fd, reinterpret_cast<const struct sockaddr *>(&destination),
                destination_len);
      if (!ret || (ret == -1 && errno == EINPROGRESS))
        {
          finished = !ret;
          return true;
        }
      std::cerr << "Failed to finish connect: " << strerror(errno);
      return false;
    }

    bool read(std::stringstream &data) const
    {
      char buf[BUFSIZ];
      int ret;

      while ((ret = recv(fd, buf, sizeof(buf), 0)) > 0)
        {
          data.write(buf, ret);
        }
      if (!(ret == -1 &&
            (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)))
        {
          std::cerr << "Connection " << std::to_string(fd) << " disconnected: "
                    << std::string((ret == -1) ? strerror(errno) : "Closed")
                    << std::endl;
          return false;
        }
      return true;
    }

    bool write(const std::string &data) const
      {
        int ret = send(fd, data.c_str(), data.length(), 0);

        if (ret != data.length() &&
            !(ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)))
          {
            std::cerr << "Connection " << std::to_string(fd)
                      << " write failed: "
                      << std::string((!ret) ? "disconnected" : strerror(errno))
                      << std::endl;
            return false;
          }
        return true;
      }

    ~SocketConnection()
    {
      if (fd != -1)
        {
          int retry = 5;

          std::cout << "Closing connection with fd ";
          std::cout << std::to_string(fd) << std::endl;
          while (close(fd) && retry--)
            {
              if (errno == EBADFD)
                {
                  throw std::system_error(
                    errno, std::system_category(),
                    std::string("Trying to close bad fd") + std::to_string(fd));
                }
            }
        }
    }
    SocketConnection(SocketConnection&& other) : fd(other.fd),
    source_len(other.source_len), destination_len(other.destination_len)
    {
      memcpy(&source, &other.source, sizeof(source));
      memcpy(&destination, &other.destination, sizeof(destination));
      other.fd = -1;
    }
  };
  int connect_to_endpoint(struct addrinfo *dst, struct addrinfo *src = nullptr)
  {
    int ret = -1;

    try
      {
        auto new_con = SocketConnection(dst, src);
        int fd = new_con.fd;
        auto inserted = connections.emplace(fd, std::move(new_con));

        if (inserted.second)
          {
            struct epoll_event ev = {};

            ev.data.fd = fd;
            ev.events = EPOLLIN;

            if (!epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev))
              {
                ret = fd;
                if (inserted.first->second.finished)
                  {
                    //ccb(fd);
                  }
              }
            else
              {
                throw std::system_error(
                  errno, std::system_category(),
                  std::string("Couldn't add a new connection to epoll: ") +
                    std::to_string(fd));
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

  int process(int timeout)
  {
    int ret = -1;
    const int n_events = connections.size();

    if (n_events)
      {
        struct epoll_event ev[n_events];

        ret = epoll_wait(efd, ev, n_events, timeout);
        if (ret >= 0)
          {
            unsigned int n_recv = ret, i;

            for (i = 0; i < n_recv; i++)
              {
                std::cout << "Event " << std::to_string(ev[i].events)
                          << " from " << std::to_string(ev[i].data.fd)
                          << std::endl;

                // Throws on bad fd.
                auto &conn = connections.at(ev[i].data.fd);
                std::stringstream data;

                if (!conn.finished)
                  {
                    if (!conn.finish())
                      {
                        connections.erase(ev[i].data.fd);
                      }
                  }
                else if (conn.finished && !conn.read(data))
                  {
                    connections.erase(ev[i].data.fd);
                  }
                else
                  {
                    // rcb(data);
                  }
              }
          }
      }
    else
      {
        std::cerr << "No connection to listen to" << std::endl;
        errno = ENOENT;
      }
    return ret;
  }

  void send_yall(const std::string &data)
    {
      auto iter = connections.begin();

      while (iter != connections.end())
        {
          if (!iter->second.write(data))
            {
              iter = connections.erase(iter);
            }
          else
            {
              iter++;
            }
        }
    }

  int get_efd()
    {
      return efd;
    }

  // std::unique_ptr<SocketConnection> add_listener(struct addrinfo *src);
  using connected_cb = std::_Mem_fn<void(T::*)(int)>;
  //using read_cb = std::function<T(const std::stringstream &data)>;
  using read_cb = std::_Mem_fn<void (T::*)(const std::stringstream &data)>;

  Connections(connected_cb ccb, read_cb rcb) throw()
    : efd(-1), ccb(ccb), rcb(rcb)
  {
    std::exception e;

    efd = epoll_create1(EPOLL_CLOEXEC);
    if (efd != -1)
      {
        std::cout << "Created main context with efd: " << std::to_string(efd)
                  << std::endl;
        return;
      }
    else
      {
        e = std::system_error(errno, std::system_category(), "epoll create");
      }
    throw(e);
  }
  ~Connections() throw()
  {
    int retry = 5;

    std::cout << "Closing connection library" << std::endl;
    while (close(efd) && retry--)
      {
        if (errno == EBADFD)
          {
            throw std::system_error(
              errno, std::system_category(),
              std::string("Trying to close bad fd") + std::to_string(efd));
          }
      }
  }

 private:
  std::map<int, SocketConnection> connections;

  int efd;
  connected_cb ccb;
  read_cb rcb;
};
