
template <typename T>
class Connections
{
 public:
  /**
   * @brief Callback invoked on each successful connection.
   *
   * @param id Connection identifier
   */
  using connected_cb = std::_Mem_fn<void (T::*)(int id)>;

  /**
   * @brief Callback invoked when data received from connection.
   *
   * @param id          Connection identifier
   * @param data        Data received.
   */
  using read_cb =
    std::_Mem_fn<void (T::*)(int id, const std::stringstream &data)>;

  /**
   * @brief Constructor with member function callbacks.
   *
   * @param ctx Caller class context.
   * @param ccb Callback invoked when connection finished.
   * @param rcb Callback invoked when data read from connection.
   *
   * @throw std::system_error On epoll fd creation failure.
   */
  Connections(T *ctx, connected_cb ccb, read_cb rcb) throw();

  /**
   * @brief Disconnects all connections
   */
  ~Connections();

  /**
   * @brief Connects to given end-point \p dst, optionally from \p src
   *
   * Note that connection might not finish on this call, so caller should wait
   * for a call to connected_cb before sending data.
   *
   * @param dst End-point to connect to.
   * @param src Source address to use.
   *
   * @return Connection identifier.
   *
   * @throw std::system_error           Connection failure.
   * @throw std::invalid_argument       Null arguments.
   */
  int connect_to_endpoint(struct addrinfo *dst, struct addrinfo *src = nullptr);

  /**
   * @brief Process pending events in Connections.
   *
   * @param timeout     Timeout for epoll_wait.
   *
   * @return >= 0       OK
   * @return < 0        Disconnected
   */
  int process(int timeout)
  {
    int ret = -1;
    const int n_events = (connections.size() > 128 ? 128 : connections.size());

    if (n_events)
      {
        struct epoll_event ev[n_events];

        ret = epoll_wait(efd, ev, n_events, timeout);
        if (ret >= 0)
          {
            unsigned int n_recv = ret, i;

            for (i = 0; i < n_recv; i++)
              {
                auto &conn = connections.at(ev[i].data.fd);
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

  /**
   * @brief Send data to all connections
   *
   * @param data        Data.
   */
  void send_yall(const std::string &data);

  /**
   * @brief Get the pollable event fd used by connections.
   *
   * Do not add fds directly to it, rather add it to your own efd.
   *
   * @return Pollable fd.
   */
  int get_efd()
  {
    return efd;
  }


  std::map<int, std::unique_ptr<Socket>> connections;

  int efd;
  connected_cb ccb;
  read_cb rcb;
  T *ctx;
};

#include "socket_internal.hpp"
