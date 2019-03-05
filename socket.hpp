#include <memory>
#include <set>

extern "C"
{
#include <sys/types.h>
#include <sys/socket.h>
}

template<typename T>
class Connections
{
 public:
  class SocketConnection
  {
    socklen_t source_len;
    struct sockaddr_storage source;
    socklen_t destination_len;
    struct sockaddr_storage destination;

   protected:
    int fd;
    bool operator<(const SocketConnection& other) const
      {
        return fd < other.fd;
      }
   public:
    SocketConnection(struct addrinfo *dst, struct addrinfo *src = nullptr);
    ~SocketConnection();
  };
  std::unique_ptr<SocketConnection> connect_to_endpoint(
    struct addrinfo *dst, struct addrinfo *src = nullptr);

  //std::unique_ptr<SocketConnection> add_listener(struct addrinfo *src);

  Connections(std::unique_ptr<T> context);
  ~Connections();

 private:
  std::set<SocketConnection> connections;

  std::unique_ptr<T> context;
  int efd;
};
