#include "config.h"

#include "net/sendfile_stream.h"

#if (defined(HAVE_SENDFILE_LINUX) && (defined(HAVE_SENDFILE_FREEBSD) || defined(HAVE_SENDFILE_DARWIN))) \
 || (defined(HAVE_SENDFILE_FREEBSD) && defined(HAVE_SENDFILE_DARWIN))
#error "Only one of HAVE_SENDFILE_LINUX, HAVE_SENDFILE_FREEBSD, HAVE_SENDFILE_DARWIN may be defined"
#endif

#if defined(HAVE_SENDFILE_LINUX)
#include <sys/sendfile.h>
#include <unistd.h>
#elif defined(HAVE_SENDFILE_FREEBSD) || defined(HAVE_SENDFILE_DARWIN)
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#endif

#include <cerrno>
#include <limits>

#include "torrent/exceptions.h"

namespace torrent {

// Linux: optional prefix write of header, then sendfile for the file body.
// FreeBSD/Darwin: sendfile with sf_hdtr headers when header_len > 0.
//
// Return value is always logical total (header bytes + file bytes) for the
// caller to split the same way as writev.

int
sendfile_stream(int socket_fd, int file_fd, uint64_t offset, uint32_t length,
                const void* header, uint32_t header_len) {
  if (length == 0 && header_len == 0)
    throw internal_error("Tried to sendfile with total length 0.");

  if (offset > (uint64_t)std::numeric_limits<off_t>::max())
    throw internal_error("sendfile offset exceeds off_t maximum.");

  if (header_len > 0 && header == nullptr)
    throw internal_error("sendfile header_len > 0 with null header.");

#if defined(HAVE_SENDFILE_LINUX)
  uint32_t header_written = 0;

  if (header_len > 0) {
    ssize_t hr = ::write(socket_fd, header, header_len);

    if (hr == -1)
      return -1;

    if (hr == 0) {
      errno = EAGAIN;
      return -1;
    }

    header_written = hr;

    if (header_written < header_len)
      return header_written;
  }

  if (length == 0)
    return header_written;

  off_t off = offset;
  ssize_t r = ::sendfile(socket_fd, file_fd, &off, length);

  if (r == -1) {
    // Header already fully sent; report that so the caller does not resend it.
    if (header_written > 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      errno = 0;
      return header_written;
    }
    return -1;
  }

  return header_written + r;

#elif defined(HAVE_SENDFILE_FREEBSD) || defined(HAVE_SENDFILE_DARWIN)
  struct iovec header_iov;
  struct sf_hdtr hdtr;
  struct sf_hdtr* hdtr_ptr = nullptr;

  if (header_len > 0) {
    header_iov.iov_base = const_cast<void*>(header);
    header_iov.iov_len  = header_len;
    hdtr.headers = &header_iov;
    hdtr.hdr_cnt = 1;
    hdtr.trailers = nullptr;
    hdtr.trl_cnt = 0;
    hdtr_ptr = &hdtr;
  }

  off_t sbytes = (off_t)length + header_len;

#if defined(HAVE_SENDFILE_FREEBSD)
  int r = ::sendfile(file_fd, socket_fd, offset, length, hdtr_ptr, &sbytes, 0);
#else
  int r = ::sendfile(file_fd, socket_fd, offset, &sbytes, hdtr_ptr, 0);
#endif

  if (r == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == EBUSY) {
      errno = 0;
      return sbytes;
    }
    return -1;
  }

  if (sbytes == 0) {
    errno = EIO;
    return -1;
  }

  return sbytes;

#else
  (void)socket_fd;
  (void)file_fd;
  throw internal_error("sendfile is not supported on this platform.");
#endif
}

} // namespace torrent
