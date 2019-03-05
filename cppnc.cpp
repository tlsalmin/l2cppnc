#include <system_error>

#include "socket.hpp"

extern "C"
{
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
}

class NetCat
{
  Connections<NetCat> conns;
  std::unique_ptr<Connections<NetCat>::SocketConnection> conn;
public:
 NetCat(std::string &dst, std::string &port, std::string &src,
        int type = SOCK_STREAM)
   : conns(std::unique_ptr<NetCat>(this))
   {
     struct addrinfo hints = {}, *res;
     int ret;

     hints.ai_family = AF_UNSPEC;
     hints.ai_socktype = type;

     ret = getaddrinfo(dst.c_str(), port.c_str(), &hints, &res);
     if (!ret)
       {
         int src_ret;
         struct addrinfo *src_res = nullptr;

         if (src.empty() || !(src_ret = getaddrinfo(src.c_str(), port.c_str(),
                                                    &hints, &src_res)))
           {
             conn = conns.connect_to_endpoint(res, src_res);

             if (src_res)
               {
                 freeaddrinfo(src_res);
               }
           }
         freeaddrinfo(res);
       }
     else
       {
         throw std::system_error(ret, std::system_category(),
                                 "Failed to solve");
       }
   };
 ~NetCat(){};
};

int main(int argc, char *argv[])
{
  std::string dst, port, src;

  if (argc >= 3)
    {
      dst = std::string(argv[1]);
      port = std::string(argv[2]);
      NetCat catter(dst, port, src);
    }
}
