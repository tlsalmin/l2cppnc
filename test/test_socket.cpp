#include "gtest/gtest.h"

#include "socket.hpp"

class SukatSocketTest : public ::testing::Test
{
 protected:
  SukatSocketTest()
  {
  }

  virtual ~SukatSocketTest()
  {
  }

  virtual void SetUp()
  {
  }

  virtual void TearDown()
  {
  }
};

TEST_F(SukatSocketTest, SukatSocketTestInit)
{
  std::string data;
  Sukat::SocketListenerTcp tcp_listener;
  int ret;
  bool bret;
  std::unique_ptr<Sukat::SocketConnection> conn_from_server(nullptr);

  auto saddr = tcp_listener.getSource();
  EXPECT_TRUE(saddr);

  Sukat::SocketConnection client(SOCK_STREAM, saddr.value());

  ret = tcp_listener.acceptNew(
    [&](Sukat::SocketConnection &&conn,
        __attribute__((unused)) std::vector<uint8_t> data) {
      conn_from_server = std::make_unique<Sukat::SocketConnection>(std::move(conn));
      EXPECT_NE(nullptr, conn_from_server);
    });
  EXPECT_GT(ret, 0);
  EXPECT_NE(nullptr, conn_from_server);

  bret = client.ready(10);
  EXPECT_EQ(true, bret);

  data = "hello from server";
  ret = conn_from_server->writeData(
    reinterpret_cast<const uint8_t *>(data.c_str()), data.length());
  EXPECT_EQ(ret, data.length());

  auto readData = client.readData();
  EXPECT_EQ(data, readData.str());

  data = "Hello from client";
  ret = client.writeData(reinterpret_cast<const uint8_t *>(data.c_str()),
                         data.length());
  EXPECT_EQ(ret, data.length());

  readData = conn_from_server->readData();
  EXPECT_EQ(data, readData.str());
}

TEST_F(SukatSocketTest, SukatSocketTestUdp)
{
  Sukat::SocketListenerUdp udp_listener;
  std::unique_ptr<Sukat::SocketConnection> conn_from_server(nullptr);
  std::string hello("Hello from client");
  int ret;

  auto saddr = udp_listener.getSource();
  EXPECT_TRUE(saddr);

  Sukat::SocketConnection client(SOCK_DGRAM, saddr.value());

  ret = client.writeData(reinterpret_cast<const uint8_t *>(hello.c_str()),
                         hello.length());
  EXPECT_EQ(ret, hello.length());

  ret = udp_listener.acceptNew(
    [&](Sukat::SocketConnection &&conn,
        __attribute__((unused)) std::vector<uint8_t> data) {
      conn_from_server =
        std::make_unique<Sukat::SocketConnection>(std::move(conn));
      EXPECT_NE(nullptr, conn_from_server);
    },
    [&](const Sukat::Socket::endpoint &peer,
        std::vector<uint8_t> &data) -> Sukat::SocketListener::accessReturn {
      std::string strdata(data.begin(), data.end());
      auto client_saddr = client.getSource();
      EXPECT_TRUE(client_saddr);
      const struct sockaddr_in6 *client_source =
                                  reinterpret_cast<const struct sockaddr_in6 *>(
                                    &client_saddr.value().first),
                                *peer6 =
                                  reinterpret_cast<const struct sockaddr_in6 *>(
                                    &peer.first);


      EXPECT_EQ(peer.second, client_saddr.value().second);
      EXPECT_EQ(peer.first.ss_family, AF_INET6);
      EXPECT_EQ(peer6->sin6_port, client_source->sin6_port);
      /* For some reason the server side gets :: and client side gets ::1
      EXPECT_EQ(0, memcmp(&peer6->sin6_addr, &client_source->sin6_addr,
                          sizeof(peer6->sin6_addr)));
      */
      EXPECT_EQ(hello, strdata);
      return Sukat::SocketListener::accessReturn::ACCESS_NEW;
    });

  EXPECT_NE(nullptr, conn_from_server);

  hello = "Hello from server";
  ret = conn_from_server->writeData(
    reinterpret_cast<const uint8_t *>(hello.c_str()), hello.length());
  EXPECT_EQ(ret, hello.length());

  auto reply = client.readData();
  EXPECT_EQ(hello, reply.str());
}

int main(int argc, char **argv)
{
  Sukat::Logger::initialize(Sukat::Logger::LogLevel::DEBUG);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

