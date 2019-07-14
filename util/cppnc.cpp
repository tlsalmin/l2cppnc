#include <system_error>

#include "socket.hpp"
#include "logging.hpp"

extern "C"
{
#include <assert.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
}

using namespace Sukat;

class NetCat
{
  std::map<int, std::unique_ptr<Sukat::Socket>> conns;
  int efd;

 public:
  NetCat(std::string log_file = "", Logger::LogLevel lvl =
         Logger::LogLevel::ERROR)
    : efd(epoll_create1(EPOLL_CLOEXEC))
  {
    std::system_error e;
    int ret;

    Logger::initialize(lvl, log_file);
    if (struct epoll_event ev = {.events = EPOLLIN,
                                 .data = {.fd = STDIN_FILENO}};
        (ret = epoll_ctl(efd, EPOLL_CTL_ADD, STDIN_FILENO, &ev)) != -1)
      {
        LOG_INF("Created Netcat instance");
        return;
      }
    else
      {
        e = std::system_error(ret, std::system_category(),
                              "Failed to listen on stdin");
      }
    throw e;
  }

  int connect(int type, const std::string &dst, const std::string &port)
  {
    std::system_error e;
    struct addrinfo hints = {}, *res = NULL;
    int ret;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = type;

    if (ret = getaddrinfo(dst.c_str(), port.c_str(), &hints, &res); ret != -1)
      {
        struct addrinfo *iter;

        for (iter = res; iter; iter = iter->ai_next)
          {
            if (std::unique_ptr<SocketConnection> conn(new SocketConnection(
                  type,
                  reinterpret_cast<const struct sockaddr_storage &>(
                    *res->ai_addr),
                  res->ai_addrlen));
                conn != nullptr)
              {
                struct epoll_event ev = {
                  .events = EPOLLIN,
                  .data = {.fd = STDIN_FILENO}};

                if (!conn->ready())
                  {
                    ev.events |= EPOLLOUT;
                  }

                if (ret = epoll_ctl(efd, EPOLL_CTL_ADD, conn->fd(), &ev);
                    ret != -1)
                  {
                    ret = 0;
                    break;
                  }
                else
                  {
                    e = std::system_error(ret, std::system_category(),
                                          "Failed to epoll");
                  }
              }
            else
              {
                e = std::system_error(ret, std::system_category(),
                                      "Failed to create connection");
              }
          }
        freeaddrinfo(res);
      }
    else
      {
        e = std::system_error(ret, std::system_category(), "Failed to solve");
      }
    if (ret == -1)
      {
        throw e;
      }
    return ret;
  }

  ~NetCat()
  {
    close(efd);
  };

  int process(int timeout)
  {
    struct epoll_event ev[2];
    int ret;

    ret = epoll_wait(efd, ev, 2, timeout);
    if (ret >= 0)
      {
        unsigned int i;
        unsigned int n_events = ret;

        for (i = 0; i < n_events; i++)
          {
            if (ev[i].data.fd == STDIN_FILENO)
              {
                char buf[BUFSIZ];

                ret = read(STDIN_FILENO, buf, sizeof(buf));
                if (ret > 0)
                  {
                    //conns.send_yall(std::string(buf, ret));
                  }
                else
                  {
                    std::cout << "stdin closed" << std::endl;
                    ret = -1;
                  }
              }
            else
              {
                //ret = conns.process(0);
              }
          }
      }
    return ret;
  };

  void read_cb(__attribute__((unused)) int id, const std::stringstream &data)
  {
    std::cout << data.str();
  }

  void connected(int id)
  {
    std::cout << "Connection " << std::to_string(id) << " ready!" << std::endl;
  };
};

int main(int argc, char *argv[])
{
  int exit_ret = EXIT_FAILURE;
  std::string dst, port, src;

  if (argc >= 3)
    {
      dst = std::string(argv[1]);
      port = std::string(argv[2]);
      try
        {
          int ret;

          NetCat catter;

          std::cout << "Ready to rock" << std::endl;
          exit_ret = EXIT_SUCCESS;
          do
            {
              ret = catter.process(-1);
            }
          while (ret >= 0);
        }
      catch (std::system_error &e)
        {
          std::cerr << "Failed to connect to " << dst << "port : " << port
                    << e.what() << ": ";
          std::cerr << strerror(e.code().value());
          std::cerr << std::endl;
        }
      catch (std::exception &e)
        {
          std::cerr << "Noes: ";
          std::cerr << e.what();
          std::cerr << std::endl;
        }
    }
  return exit_ret;
}