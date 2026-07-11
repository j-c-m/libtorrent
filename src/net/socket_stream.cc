#include "config.h"

#include "socket_stream.h"

namespace torrent {

char* SocketStream::m_nullBuffer = new char[SocketStream::null_buffer_size];

SocketStream::~SocketStream() = default;

uint32_t
SocketStream::read_stream_throws(void* buf, uint32_t length) {
  int r = read_stream(buf, length);

  if (r == 0)
    throw close_connection();

  if (r < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return 0;
    else if (errno == ECONNRESET || errno == ECONNABORTED)
      throw close_connection();
    else if (errno == EDEADLK)
      throw blocked_connection();
    else
      throw connection_error(errno);
  }

  return r;
}

uint32_t
SocketStream::write_stream_throws(const void* buf, uint32_t length) {
  // write_stream is a one-entry writev; share its throw policy.
  struct iovec iov;
  iov.iov_base = const_cast<void*>(buf);
  iov.iov_len  = length;
  return write_streamv_throws(&iov, 1);
}

uint32_t
SocketStream::write_streamv_throws(const struct iovec* iov, int iovcnt) {
  int r = write_streamv(iov, iovcnt);

  // writev(2) is not send(2): zero means no progress, not peer close.
  if (r == 0)
    return 0;

  if (r < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return 0;
    else if (errno == ECONNRESET || errno == ECONNABORTED || errno == EPIPE || errno == ENOTCONN)
      throw close_connection();
    else if (errno == EDEADLK)
      throw blocked_connection();
    else
      throw connection_error(errno);
  }

  return r;
}

} // namespace torrent
