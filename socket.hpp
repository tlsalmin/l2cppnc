#pragma once

#include <functional>
#include <iostream>
#include <map>
#include <memory>

extern "C"
{
#include <sys/socket.h>
}

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
  int process(int timeout);

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

 private:
  class SocketConnection
  {
    friend class Connections;

    socklen_t source_len;
    struct sockaddr_storage source;
    socklen_t destination_len;
    struct sockaddr_storage destination;
    bool finished; //<! If TCP handshake is finished.

   protected:
    int fd;

    SocketConnection(struct addrinfo *dst, struct addrinfo *src = nullptr);

    /**
     * @brief Finishes TCP connection.
     *
     * @return true     All ok, but might not be finished.
     * @return false    Connection failure.
     */
    bool finish();

    /**
     * @brief Reads data from connection
     *
     * @param data      Storage for data.
     *
     * @return true     Success.
     * @return false    Read error.
     */
    bool read(std::stringstream &data) const;

    /**
     * @brief Writes data to connection
     *
     * @param data      Data to write.
     *
     * @return true     Success.
     * @return false    Write error.
     */
    bool write(const std::string &data) const;

   public:
    /** @brief Default destructor. Closes fd */
    ~SocketConnection();

    /** @brief Move constructor */
    SocketConnection(SocketConnection &&other);

    /** @brief For std::container */
    bool operator<(const SocketConnection &other) const
    {
      return fd < other.fd;
    }

    /** @brief For std::container */
    bool operator==(const int other) const
    {
      return fd == other;
    }
  };

  std::map<int, SocketConnection> connections;

  int efd;
  connected_cb ccb;
  read_cb rcb;
  T *ctx;
};

#include "socket_internal.hpp"
