#pragma once

#include "logging.hpp"

extern "C"
{
#include <assert.h>
#include <errno.h>
#include <unistd.h>
}

namespace Sukat
{
class Fd
{
 public:
  Fd(int fd) : mFd(fd)
  {
    LOG_DBG("Created fd ", fd);
  };
  Fd() = delete;
  ~Fd()
  {
    if (mFd != -1)
      {
        int retry = 5;

        LOG_DBG("Closing fd ", mFd);
        while (::close(mFd) == -1 && --retry)
          {
            // Generate a core for bad fd investigating.
            assert(errno != EBADFD);
          }
      }
  }

  Fd(Fd &&other)
  {
    mFd = other.mFd;
    other.mFd = -1;
  }
  int fd() const
  {
    return mFd;
  };

 private:
  int mFd{-1};
};
} // namespace Sukat
