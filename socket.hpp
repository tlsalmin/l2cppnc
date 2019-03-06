#pragma once

#include <functional>
#include <map>
#include <iostream>
#include <memory>

extern "C"
{
#include <sys/socket.h>
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

    SocketConnection(struct addrinfo *dst, struct addrinfo *src = nullptr);

    bool finish();

    bool read(std::stringstream &data) const;

    bool write(const std::string &data) const;

    ~SocketConnection();

    SocketConnection(SocketConnection&& other);
  };
  int connect_to_endpoint(struct addrinfo *dst, struct addrinfo *src = nullptr);

  int process(int timeout);

  void send_yall(const std::string &data);

  int get_efd()
    {
      return efd;
    }

  // std::unique_ptr<SocketConnection> add_listener(struct addrinfo *src);
  using connected_cb = std::_Mem_fn<void(T::*)(int)>;
  //using connected_cb = std::function<void(T, int)>;
  //using read_cb = std::function<T(const std::stringstream &data)>;
  using read_cb = std::_Mem_fn<void (T::*)(const std::stringstream &data)>;

  explicit Connections();
  Connections(T *ctx, connected_cb ccb, read_cb rcb) throw();

  ~Connections();

  Connections(const Connections&) = delete;
  Connections& operator=(const Connections&) = delete;

 private:
  std::map<int, SocketConnection> connections;

  int efd;
  connected_cb ccb;
  read_cb rcb;
  T *ctx;
};

#include "socket_internal.hpp"
