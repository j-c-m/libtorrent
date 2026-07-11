#ifndef LIBTORRENT_NET_SOCKET_STREAM_H
#define LIBTORRENT_NET_SOCKET_STREAM_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include "torrent/event.h"
#include "torrent/exceptions.h"

namespace torrent {

class SocketStream : public Event {
public:
  ~SocketStream() override;

  int                 read_stream(void* buf, uint32_t length);
  int                 write_stream(const void* buf, uint32_t length);
  int                 write_streamv(const struct iovec* iov, int iovcnt);
  int                 write_sendfile(int in_fd, uint64_t offset, uint32_t length,
                                     const void* header, uint32_t header_len);

  // Returns the number of bytes read/written, or zero if the socket is
  // blocking. On errors or closed sockets it will throw an
  // appropriate exception.
  uint32_t            read_stream_throws(void* buf, uint32_t length);
  uint32_t            write_stream_throws(const void* buf, uint32_t length);
  uint32_t            write_streamv_throws(const struct iovec* iov, int iovcnt);
  // Total bytes = header + file (same split as writev for accounting).
  uint32_t            write_sendfile_throws(int in_fd, uint64_t offset, uint32_t length,
                                            const void* header, uint32_t header_len);

  // Handles all the error catching etc. Returns true if the buffer is
  // finished reading/writing.
  bool                read_buffer(void* buf, uint32_t length, uint32_t& pos);
  bool                write_buffer(const void* buf, uint32_t length, uint32_t& pos);

  uint32_t            ignore_stream_throws(uint32_t length) { return read_stream_throws(m_nullBuffer, length); }

protected:
  static constexpr size_t null_buffer_size = 1 << 17;

  static char*        m_nullBuffer;
};

inline bool
SocketStream::read_buffer(void* buf, uint32_t length, uint32_t& pos) {
  pos += read_stream_throws(buf, length - pos);

  return pos == length;
}

inline bool
SocketStream::write_buffer(const void* buf, uint32_t length, uint32_t& pos) {
  pos += write_stream_throws(buf, length - pos);

  return pos == length;
}

inline int
SocketStream::read_stream(void* buf, uint32_t length) {
  if (length == 0)
    throw internal_error("Tried to read to buffer length 0.");

  return ::recv(m_fileDesc, buf, length, 0);
}

// Single-buffer write as a one-entry writev (same path as multi-iov uploads).
inline int
SocketStream::write_stream(const void* buf, uint32_t length) {
  struct iovec iov;
  // POSIX iovec::iov_base is void*, not const void*; writev only reads the memory.
  iov.iov_base = const_cast<void*>(buf);
  iov.iov_len  = length;
  return write_streamv(&iov, 1);
}

// Low-level gather write. May throw on empty iov (programmer error) only.
inline int
SocketStream::write_streamv(const struct iovec* iov, int iovcnt) {
  if (iovcnt <= 0)
    throw internal_error("Tried to writev with invalid iovcnt.");

  size_t total = 0;
  for (int i = 0; i < iovcnt; i++)
    total += iov[i].iov_len;

  if (total == 0)
    throw internal_error("Tried to writev with total length 0.");

  return ::writev(m_fileDesc, iov, iovcnt);
}

} // namespace torrent

#endif
