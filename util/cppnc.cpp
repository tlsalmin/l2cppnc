#include <system_error>

#include "epoll.hpp"
#include "logging.hpp"
#include "socket.hpp"

extern "C"
{
#include <assert.h>
#include <getopt.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
}

using namespace Sukat;

class NetCat
{
  std::map<int, std::unique_ptr<Sukat::Socket>> conns;
  Sukat::Epoll epIn, epOut, epMain;

 public:
  NetCat()
  {
    if (!epMain.ctl(epIn.fd()) || !epMain.ctl(epOut.fd()))
      {
        throw std::system_error(errno, std::system_category(),
                                "Failed to bind efds");
      }
  }

  void registerStdin()
  {
    if (!epIn.ctl(STDIN_FILENO))
      {
        throw std::system_error(errno, std::system_category(),
                                "Failed to register stdin");
      }
  }

  auto connect(const std::string &dst, const std::string &port,
               int type = SOCK_STREAM)
  {
    AddrInfo endpoint(dst, port, {}, type);
    auto new_conn =
      std::make_unique<SocketConnection>(endpoint.mResults.front());
    auto [ret, inserted] =
      conns.insert(std::pair<int, std::unique_ptr<SocketConnection>>(
        new_conn->fd(), std::move(new_conn)));
    assert(inserted);
    const bool connected =
      dynamic_cast<SocketConnection *>(ret->second.get())->connComplete();
    const auto &efdTarget = (connected) ? epIn : epOut;
    const auto events = (connected) ? EPOLLIN : EPOLLOUT;
    int fd = ret->second->fd();

    if (!efdTarget.ctl(fd, EPOLL_CTL_ADD, events))
      {
        throw std::system_error(errno, std::system_category(),
                                "Failed to register fd");
      }
    else if (connected)
      {
        // Ready to send stuff from stdin.
        registerStdin();
      }

    return ret->second.get();
  }

  ~NetCat() = default;

  std::optional<int> process(int timeout)
  {
    return epMain.wait(
      [&](const struct epoll_event &ev) {
        if (ev.data.fd == epOut.fd())
          {
            return epOut.wait(
              [&](const struct epoll_event &ev) -> std::optional<int> {
                if (auto &iter = conns.at(ev.data.fd))
                  {
                    int ret;

                    if (ev.events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))
                      {
                        LOG_ERR("Failed to connect to ", iter.get(), " events ",
                                Epoll::event_to_string(ev));
                      }
                    else if ((ret =
                                dynamic_cast<SocketConnection *>((iter).get())
                                  ->polloutReady()))
                      {
                        LOG_ERR("Failed to finalize connection: ",
                                strerror(ret));
                      }
                    else if (!epOut.ctl(iter->fd(), EPOLL_CTL_DEL) ||
                             !epIn.ctl(iter->fd()))
                      {
                        LOG_ERR("Failed to modify epoll after connect finish");
                      }
                    else
                      {
                        registerStdin();
                        // All good.
                        return {};
                      }
                    return std::make_optional<int>(-1);
                  }
                return {};
              },
              timeout);
          }
        else if (ev.data.fd == epIn.fd())
          {
            LOG_DBG("Data in epIn ", epIn.fd());
            return epIn.wait(
              [&](const struct epoll_event &ev) -> std::optional<int> {
                if (auto &iter = conns.at(ev.data.fd))
                  {
                    auto data =
                      dynamic_cast<SocketConnection *>(iter.get())->readData();
                    std::cout << data.str() << std::endl;
                    ;
                  }
                else if (ev.data.fd == STDIN_FILENO)
                  {
                    std::ostringstream data;
                    data << std::cin.rdbuf();
                    for (const auto &conn : conns)
                      {
                      //TODO This needs a better way.
                        auto *connection =
                          dynamic_cast<SocketConnection *>(conn.second.get());
                        if (connection->ready())
                          {
                            connection->write(data.str().c_str());
                          }
                      }
                    return {};
                  }
                return std::make_optional<int>(-1);
              },
              timeout);
          }
        abort(); // Not reached.
      },
      timeout);
  }
};

void usage(const std::string bin)
{
  std::cout << bin << ": Netcat utility." << std::endl;
  std::cout << "Usage: " << bin << " <IP> "
            << " <Port>" << std::endl;
  std::cout << "Options: " << std::endl;
  std::cout << "  -h    This help" << std::endl;
  std::cout << "  -v    Increase verbosity" << std::endl;
}

int main(int argc, char *argv[])
{
  int exit_ret = EXIT_FAILURE;
  std::string dst, port, src;
  int c;
  int log_lvl = static_cast<int>(Sukat::Logger::LogLevel::ERROR);

  while ((c = getopt(argc, argv, "vh")) != -1)
    {
      switch (c)
        {
          case 'v':
            ++log_lvl;
            break;
          default:
            std::cerr << "Unknown argument " << c << std::endl;
            [[fallthrough]];
          case 'h':
            usage(argv[0]);
            break;
        }
    }

  Logger::initialize(log_lvl);

  if (optind + 1 < argc)
    {
      dst = std::string(argv[optind]);
      port = std::string(argv[optind + 1]);
      try
        {
          NetCat catter;
          std::optional<int> ret;

          LOG_DBG("Ready to connect");
          auto conn = catter.connect(dst, port);
          exit_ret = EXIT_SUCCESS;
          do
            {
              ret = catter.process(-1);
            }
          while (!ret.has_value());
        }
      catch (std::system_error &e)
        {
          std::cerr << "Failed to connect to " << dst << " port : " << port
                    << " " << e.what() << ": ";
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
