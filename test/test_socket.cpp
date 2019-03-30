#include "gtest/gtest.h"

#include "socket.hpp"

class SukatSocketTest : public ::testing::Test
{
protected:
  SukatSocketTest() {
  }

  virtual ~SukatSocketTest() {
  }

  virtual void SetUp() {
  }

  virtual void TearDown() {
  }
};

TEST_F(SukatSocketTest, SukatSocketTestInit)
{
  socklen_t slen;
  std::string data;
  Sukat::SocketListenerTcp tcp_listener;
  int ret;
  const struct sockaddr_storage *saddr = tcp_listener.getSource(&slen);
  std::unique_ptr<Sukat::SocketConnection> conn_from_server(nullptr);

  EXPECT_NE(nullptr, saddr);

  Sukat::SocketConnection client(SOCK_STREAM, *saddr, slen);

  ret = tcp_listener.acceptNew(
    [&](std::unique_ptr<Sukat::SocketConnection> &&conn) {
      EXPECT_NE(nullptr, conn);
      conn_from_server = std::move(conn);
    });
  EXPECT_GT(ret, 0);
  EXPECT_NE(nullptr, conn_from_server);

  while (!client.ready())
    {
      bool bret = client.finish();
      EXPECT_EQ(true, bret);
    }

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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
