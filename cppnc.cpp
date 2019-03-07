#include <system_error>

#include "socket.hpp"

extern "C"
{
#include <assert.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
}

class NetCat
{
  Connections<NetCat> conns;
  int main_fd;
  int efd;

 public:
  NetCat(std::string &dst, std::string &port, std::string &src,
         int type = SOCK_STREAM)
    : conns(this, std::mem_fn(&NetCat::connected),
            std::mem_fn(&NetCat::read_cb))
  {
    struct epoll_event ev = {};
    std::system_error e;
    struct addrinfo hints = {}, *res;
    int ret;

    efd = epoll_create1(EPOLL_CLOEXEC);
    // Just do it.
    assert(efd != -1);
    ev.data.fd = STDIN_FILENO;
    ev.events = EPOLLIN;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = type;

    ret = epoll_ctl(efd, EPOLL_CTL_ADD, STDIN_FILENO, &ev);
    assert(!ret);

    ev.data.fd = conns.get_efd();
    ev.events = EPOLLIN;
    ret = epoll_ctl(efd, EPOLL_CTL_ADD, ev.data.fd, &ev);
    assert(!ret);

    ret = getaddrinfo(dst.c_str(), port.c_str(), &hints, &res);
    if (!ret)
      {
        int src_ret;
        struct addrinfo *src_res = nullptr;

        if (src.empty() || !(src_ret = getaddrinfo(src.c_str(), port.c_str(),
                                                   &hints, &src_res)))
          {
            main_fd = conns.connect_to_endpoint(res, src_res);

            if (main_fd != -1)
              {
              }
            else
              {
                e = std::system_error(ret, std::system_category(),
                                      "Failed to connect");
              }
            if (src_res)
              {
                freeaddrinfo(src_res);
              }
          }
        freeaddrinfo(res);
      }
    else
      {
        e = std::system_error(ret, std::system_category(), "Failed to solve");
      }
    if (main_fd == -1)
      {
        throw e;
      }
  };
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
                    conns.send_yall(std::string(buf, ret));
                  }
                else
                  {
                    std::cout << "stdin closed" << std::endl;
                    ret = -1;
                  }
              }
            else
              {
                ret = conns.process(0);
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

          NetCat catter(dst, port, src);

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
